#include "software-title-render-session.h"

#include "title-data.h"
#include "title-render-session.h"
#include "title-snapshot.h"
#include "asset-runtime.h"
#include "effect-runtime.h"
#include "layer-transform-3d.h"
#include "chart-data.h"
#include "chart-renderer.h"
#include "path-geometry.h"
#include "stroke-path-geometry.h"
#include "style-preset-runtime.h"
#include "title-rich-text.h"
#include "title-text-layout.h"
#include "title-text-layout-qt-font-registry.h"
#include "title-video-runtime.h"
#include "frame-rate-provider.h"

#include <QColor>
#include <QConicalGradient>
#include <QFont>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QRawFont>
#include <QTransform>
#include <QVector3D>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

QColor argb(std::uint32_t value, double opacity = 1.0)
{
    QColor color = QColor::fromRgba(value);
    color.setAlphaF(std::clamp(color.alphaF() * opacity, 0.0, 1.0));
    return color;
}

const Layer *find_layer(const Title &title, const std::string &id)
{
    if (id.empty())
        return nullptr;
    for (const auto &candidate : title.layers) {
        if (candidate && candidate->id == id)
            return candidate.get();
    }
    return nullptr;
}

double resolved_layer_time_impl(const Title &title, const Layer &layer,
                                double title_time,
                                std::unordered_set<std::string> &visiting)
{
    if (layer.asset_owner_id.empty() ||
        !visiting.insert(layer.asset_owner_id).second)
        return title_time;
    const Layer *owner = find_layer(title, layer.asset_owner_id);
    if (!owner || owner->type != LayerType::Asset) {
        visiting.erase(layer.asset_owner_id);
        return title_time;
    }
    const double owner_time = resolved_layer_time_impl(
        title, *owner, title_time, visiting);
    const double resolved = fxm::asset_runtime::resolve_local_time(
        title.id, *owner, owner_time - owner->in_time);
    visiting.erase(layer.asset_owner_id);
    return resolved;
}

double resolved_layer_time(const Title &title, const Layer &layer,
                           double title_time)
{
    std::unordered_set<std::string> visiting;
    return resolved_layer_time_impl(title, layer, title_time, visiting);
}

double layer_local_time(const Layer &layer, double resolved_time)
{
    return std::clamp(resolved_time - layer.in_time, 0.0,
                      std::max(0.0, layer.out_time - layer.in_time));
}

bool layer_and_parents_visible(const Title &title, const Layer &layer,
                               double title_time)
{
    const Layer *cursor = &layer;
    std::unordered_set<std::string> visiting;
    while (cursor) {
        if (!cursor->visible)
            return false;
        const double resolved = resolved_layer_time(title, *cursor, title_time);
        if (resolved < cursor->in_time || resolved > cursor->out_time)
            return false;
        if (cursor->parent_id.empty())
            break;
        if (!visiting.insert(cursor->id).second)
            return false;
        cursor = find_layer(title, cursor->parent_id);
    }
    return true;
}

double evaluated_box_width(const Layer &layer, double local_time)
{
    const double value = layer.size.is_animated()
        ? layer.size.evaluate(local_time).x
        : static_cast<double>(layer.rect_width);
    return std::max(0.0, value);
}

double evaluated_box_height(const Layer &layer, double local_time)
{
    const double value = layer.size.is_animated()
        ? layer.size.evaluate(local_time).y
        : static_cast<double>(layer.rect_height);
    return std::max(0.0, value);
}

double evaluated_origin_x(const Layer &layer, double local_time)
{
    return std::clamp(layer.origin_prop.is_animated()
                          ? layer.origin_prop.evaluate(local_time).x
                          : static_cast<double>(layer.origin_x),
                      0.0, 1.0);
}

double evaluated_origin_y(const Layer &layer, double local_time)
{
    return std::clamp(layer.origin_prop.is_animated()
                          ? layer.origin_prop.evaluate(local_time).y
                          : static_cast<double>(layer.origin_y),
                      0.0, 1.0);
}

QRectF software_layer_local_rect(const Layer &layer, double local_time)
{
    const double width = evaluated_box_width(layer, local_time);
    const double height = evaluated_box_height(layer, local_time);
    return QRectF(-evaluated_origin_x(layer, local_time) * width,
                  -evaluated_origin_y(layer, local_time) * height,
                  width, height);
}

Vec2Value evaluated_quad_offset(const AnimatedVec2Property &property,
                                float legacy_x, float legacy_y,
                                double local_time)
{
    if (property.is_animated() || property.static_value.x != 0.0 ||
        property.static_value.y != 0.0)
        return property.evaluate(local_time);
    return {legacy_x, legacy_y};
}

QPolygonF warped_local_quad(const Layer &layer, const QRectF &bounds,
                            double local_time)
{
    const Vec2Value tl = evaluated_quad_offset(
        layer.transform_quad_tl, layer.transform_quad_tl_x,
        layer.transform_quad_tl_y, local_time);
    const Vec2Value tr = evaluated_quad_offset(
        layer.transform_quad_tr, layer.transform_quad_tr_x,
        layer.transform_quad_tr_y, local_time);
    const Vec2Value br = evaluated_quad_offset(
        layer.transform_quad_br, layer.transform_quad_br_x,
        layer.transform_quad_br_y, local_time);
    const Vec2Value bl = evaluated_quad_offset(
        layer.transform_quad_bl, layer.transform_quad_bl_x,
        layer.transform_quad_bl_y, local_time);
    QPolygonF quad;
    quad << bounds.topLeft() + QPointF(tl.x * bounds.width(),
                                      tl.y * bounds.height())
         << bounds.topRight() + QPointF(tr.x * bounds.width(),
                                       tr.y * bounds.height())
         << bounds.bottomRight() + QPointF(br.x * bounds.width(),
                                          br.y * bounds.height())
         << bounds.bottomLeft() + QPointF(bl.x * bounds.width(),
                                         bl.y * bounds.height());
    return quad;
}

bool software_layer_canvas_transform(const Title &title, const Layer &layer,
                                     double title_time, double local_time,
                                     const QRectF &bounds,
                                     QTransform &local_to_canvas)
{
    QPolygonF source;
    source << bounds.topLeft() << bounds.topRight()
           << bounds.bottomRight() << bounds.bottomLeft();
    const QPolygonF local_quad = warped_local_quad(layer, bounds, local_time);

    if (fxm::transform3d::layer_or_ancestor_uses_3d(title, layer)) {
        return fxm::transform3d::projected_local_quad_transform(
            title, layer, title_time, source, local_quad, local_to_canvas);
    }

    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        title, layer, title_time);
    QPolygonF canvas_quad;
    canvas_quad.reserve(4);
    for (const QPointF &point : local_quad) {
        const QVector3D mapped = world.map(QVector3D(
            static_cast<float>(point.x()), static_cast<float>(point.y()),
            0.0f));
        canvas_quad << QPointF(mapped.x(), mapped.y());
    }
    return QTransform::quadToQuad(source, canvas_quad, local_to_canvas);
}

int evaluated_channel(const AnimatedProperty &property, int fallback,
                      double local_time)
{
    return static_cast<int>(std::clamp(std::round(
        property.is_animated() ? property.evaluate(local_time) : fallback),
        0.0, 255.0));
}

std::uint32_t evaluated_argb(const AnimatedProperty &alpha,
                             const AnimatedProperty &red,
                             const AnimatedProperty &green,
                             const AnimatedProperty &blue,
                             std::uint32_t fallback, double local_time)
{
    return (static_cast<std::uint32_t>(evaluated_channel(
                alpha, static_cast<int>((fallback >> 24) & 0xff), local_time)) << 24) |
           (static_cast<std::uint32_t>(evaluated_channel(
                red, static_cast<int>((fallback >> 16) & 0xff), local_time)) << 16) |
           (static_cast<std::uint32_t>(evaluated_channel(
                green, static_cast<int>((fallback >> 8) & 0xff), local_time)) << 8) |
           static_cast<std::uint32_t>(evaluated_channel(
                blue, static_cast<int>(fallback & 0xff), local_time));
}

struct SoftwareLight {
    TitleLightType type = TitleLightType::Ambient;
    TitleLightFalloff falloff = TitleLightFalloff::None;
    QVector3D position;
    QVector3D direction {0.0f, 0.0f, 1.0f};
    QVector3D color {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;
    float source_size = 0.0f;
    float falloff_distance = 1.0f;
    float cone_outer_cos = -1.0f;
    float cone_inner_cos = -1.0f;
    bool casts_shadows = false;
    float shadow_darkness = 0.0f;
    float shadow_softness = 0.0f;
};

SoftwareLight evaluated_software_light(const TitleLight &light,
                                       double time)
{
    SoftwareLight result;
    result.type = light.type;
    result.falloff = light.falloff;
    const Vec3Value position = evaluated_light_position(light, time);
    const Vec3Value target = evaluated_light_target(light, time);
    result.position = QVector3D(position.x, position.y, position.z);
    result.direction = QVector3D(target.x - position.x,
                                 target.y - position.y,
                                 target.z - position.z);
    if (result.direction.lengthSquared() <= 0.000001f)
        result.direction = QVector3D(0.0f, 0.0f, 1.0f);
    else
        result.direction.normalize();
    const std::uint32_t packed = evaluated_light_color(light, time);
    const float alpha = static_cast<float>((packed >> 24) & 0xffu) / 255.0f;
    result.color = QVector3D(
        static_cast<float>((packed >> 16) & 0xffu) / 255.0f,
        static_cast<float>((packed >> 8) & 0xffu) / 255.0f,
        static_cast<float>(packed & 0xffu) / 255.0f);
    result.intensity = static_cast<float>(std::max(
        0.0, light.intensity.evaluate(time) / 100.0)) * alpha;
    result.source_size = static_cast<float>(std::max(
        0.0, light.source_size.evaluate(time)));
    result.falloff_distance = static_cast<float>(std::max(
        0.0001, light.falloff_distance.evaluate(time)));
    const double outer = std::clamp(
        light.cone_angle.evaluate(time), 0.1, 179.0) *
        0.5 * 3.14159265358979323846 / 180.0;
    const double feather = std::clamp(
        light.cone_feather.evaluate(time) / 100.0, 0.0, 1.0);
    result.cone_outer_cos = static_cast<float>(std::cos(outer));
    result.cone_inner_cos = static_cast<float>(
        std::cos(outer * (1.0 - feather)));
    result.casts_shadows = light.casts_shadows;
    result.shadow_darkness = static_cast<float>(std::clamp(
        light.shadow_darkness.evaluate(time) / 100.0, 0.0, 1.0));
    result.shadow_softness = static_cast<float>(std::max(
        0.0, light.shadow_softness.evaluate(time)));
    return result;
}

std::vector<SoftwareLight> software_lights(const Title &title, double time)
{
    std::vector<SoftwareLight> result;
    result.reserve(4);
    std::size_t authored_count = 0;
    for (const auto &light_layer : title.layers) {
        if (!light_layer || light_layer->type != LayerType::Light ||
            !layer_and_parents_visible(title, *light_layer, time))
            continue;
        ++authored_count;
        if (!light_layer->light.enabled || result.size() >= 4)
            continue;
        TitleLight evaluated = light_layer->light;
        const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
            title, *light_layer, time);
        const Vec3Value position = evaluated.position.evaluate(time);
        const Vec3Value target = evaluated.target.evaluate(time);
        const QVector3D world_position = world.map(QVector3D(
            position.x, position.y, position.z));
        const QVector3D world_target = world.map(QVector3D(
            target.x, target.y, target.z));
        evaluated.position.static_value = {
            world_position.x(), world_position.y(), world_position.z()};
        evaluated.position.keyframes.clear();
        evaluated.target.static_value = {
            world_target.x(), world_target.y(), world_target.z()};
        evaluated.target.keyframes.clear();
        result.push_back(evaluated_software_light(evaluated, time));
    }
    for (const TitleLight &light : title.lights) {
        ++authored_count;
        if (light.enabled && result.size() < 4)
            result.push_back(evaluated_software_light(light, time));
    }
    if (authored_count == 0 && title.default_light_enabled) {
        SoftwareLight light;
        light.type = TitleLightType::Parallel;
        light.direction = QVector3D(0.0f, 0.0f, 1.0f);
        light.intensity = 1.0f;
        result.push_back(light);
    }
    return result;
}

QVector3D software_lighting_factor(const Title &title, const Layer &layer,
                                   const QRectF &bounds, double title_time,
                                   double local_time,
                                   const std::vector<SoftwareLight> &lights)
{
    if (!title.lighting_enabled || !layer.material_accepts_lights ||
        !fxm::transform3d::layer_supports_3d(layer) ||
        !fxm::transform3d::layer_or_ancestor_uses_3d(title, layer))
        return QVector3D(1.0f, 1.0f, 1.0f);

    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        title, layer, title_time);
    const QVector3D point = world.map(QVector3D(
        static_cast<float>(bounds.center().x()),
        static_cast<float>(bounds.center().y()), 0.0f));
    const fxm::transform3d::EvaluatedTransform evaluated =
        fxm::transform3d::evaluate(title, layer, title_time);
    QVector3D normal = evaluated.world_normal.normalized();
    const QVector3D to_camera = (evaluated.camera_position - point).normalized();
    if (QVector3D::dotProduct(normal, to_camera) < 0.0f)
        normal = -normal;

    QVector3D ambient;
    QVector3D diffuse;
    for (const SoftwareLight &light : lights) {
        if (light.intensity <= 0.000001f)
            continue;
        if (light.type == TitleLightType::Ambient ||
            light.type == TitleLightType::Environment) {
            const float exposure = light.type == TitleLightType::Environment
                ? static_cast<float>(std::pow(2.0, std::clamp(
                      title.environment_exposure, -16.0, 16.0)))
                : 1.0f;
            ambient += light.color * light.intensity * exposure;
            continue;
        }
        QVector3D direction;
        float attenuation = 1.0f;
        if (light.type == TitleLightType::Parallel) {
            direction = -light.direction;
        } else {
            const QVector3D delta = light.position - point;
            const float distance = std::max(0.0001f, delta.length());
            direction = delta / distance;
            const float normalized = std::max(
                0.0f, (distance - light.source_size) /
                          light.falloff_distance);
            if (light.falloff == TitleLightFalloff::Linear)
                attenuation = std::clamp(1.0f - normalized, 0.0f, 1.0f);
            else if (light.falloff == TitleLightFalloff::InverseSquare)
                attenuation = 1.0f / (1.0f + 4.0f * normalized * normalized);
            if (light.type == TitleLightType::Spot) {
                const QVector3D from_light = (point - light.position).normalized();
                const float cone = QVector3D::dotProduct(
                    light.direction, from_light);
                const float denominator = std::max(
                    0.0001f, light.cone_inner_cos - light.cone_outer_cos);
                attenuation *= std::clamp(
                    (cone - light.cone_outer_cos) / denominator, 0.0f, 1.0f);
            }
        }
        const float diffuse_amount = static_cast<float>(std::clamp(
            layer.material_diffuse.evaluate(local_time), 0.0, 4.0));
        const float n_dot_l = std::max(
            0.0f, QVector3D::dotProduct(normal, direction.normalized()));
        diffuse += light.color *
            (light.intensity * attenuation * n_dot_l * diffuse_amount);
    }
    const float ambient_amount = static_cast<float>(std::clamp(
        layer.material_ambient.evaluate(local_time), 0.0, 4.0));
    const float metallic = static_cast<float>(std::clamp(
        layer.material_metallic.evaluate(local_time), 0.0, 1.0));
    QVector3D factor = ambient * ambient_amount + diffuse * (1.0f - metallic);
    const float emissive = static_cast<float>(std::clamp(
        layer.material_emissive_intensity.evaluate(local_time), 0.0, 16.0));
    if (emissive > 0.0f) {
        const std::uint32_t packed = evaluated_argb(
            layer.material_emissive_color_a,
            layer.material_emissive_color_r,
            layer.material_emissive_color_g,
            layer.material_emissive_color_b,
            layer.material_emissive_color, local_time);
        factor += QVector3D(
            static_cast<float>((packed >> 16) & 0xffu) / 255.0f,
            static_cast<float>((packed >> 8) & 0xffu) / 255.0f,
            static_cast<float>(packed & 0xffu) / 255.0f) * emissive;
    }
    factor.setX(std::clamp(factor.x(), 0.0f, 4.0f));
    factor.setY(std::clamp(factor.y(), 0.0f, 4.0f));
    factor.setZ(std::clamp(factor.z(), 0.0f, 4.0f));
    return factor;
}

struct SoftwareLightingContext {
    bool active = false;
    QVector3D world_origin;
    QVector3D world_x;
    QVector3D world_y;
    QVector3D normal {0.0f, 0.0f, -1.0f};
    QVector3D camera_position;
    QVector3D emissive;
    float ambient = 0.0f;
    float diffuse = 1.0f;
    float specular = 0.0f;
    float shininess = 32.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
};

SoftwareLightingContext software_lighting_context(
    const Title &title, const Layer &layer, double title_time,
    double local_time)
{
    SoftwareLightingContext context;
    context.active = title.lighting_enabled && layer.material_accepts_lights &&
        fxm::transform3d::layer_supports_3d(layer) &&
        fxm::transform3d::layer_or_ancestor_uses_3d(title, layer);
    if (!context.active)
        return context;
    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        title, layer, title_time);
    context.world_origin = world.map(QVector3D(0.0f, 0.0f, 0.0f));
    context.world_x = world.map(QVector3D(1.0f, 0.0f, 0.0f)) -
                      context.world_origin;
    context.world_y = world.map(QVector3D(0.0f, 1.0f, 0.0f)) -
                      context.world_origin;
    const fxm::transform3d::EvaluatedTransform evaluated =
        fxm::transform3d::evaluate(title, layer, title_time);
    context.normal = evaluated.world_normal.normalized();
    context.camera_position = evaluated.camera_position;
    context.ambient = static_cast<float>(std::clamp(
        layer.material_ambient.evaluate(local_time), 0.0, 4.0));
    context.diffuse = static_cast<float>(std::clamp(
        layer.material_diffuse.evaluate(local_time), 0.0, 4.0));
    context.specular = static_cast<float>(std::clamp(
        layer.material_specular.evaluate(local_time), 0.0, 4.0));
    context.shininess = static_cast<float>(std::clamp(
        layer.material_shininess.evaluate(local_time), 1.0, 512.0));
    context.metallic = static_cast<float>(std::clamp(
        layer.material_metallic.evaluate(local_time), 0.0, 1.0));
    context.roughness = static_cast<float>(std::clamp(
        layer.material_roughness.evaluate(local_time), 0.02, 1.0));
    const float emissive_amount = static_cast<float>(std::clamp(
        layer.material_emissive_intensity.evaluate(local_time), 0.0, 16.0));
    const std::uint32_t packed = evaluated_argb(
        layer.material_emissive_color_a,
        layer.material_emissive_color_r,
        layer.material_emissive_color_g,
        layer.material_emissive_color_b,
        layer.material_emissive_color, local_time);
    context.emissive = QVector3D(
        static_cast<float>((packed >> 16) & 0xffu) / 255.0f,
        static_cast<float>((packed >> 8) & 0xffu) / 255.0f,
        static_cast<float>(packed & 0xffu) / 255.0f) * emissive_amount *
        (static_cast<float>((packed >> 24) & 0xffu) / 255.0f);
    return context;
}

struct SoftwareLightingSample {
    QVector3D multiplier;
    QVector3D specular;
};

SoftwareLightingSample sample_software_lighting(
    const Title &title, const SoftwareLightingContext &context,
    const std::vector<SoftwareLight> &lights, float local_x, float local_y)
{
    SoftwareLightingSample sample;
    const QVector3D point = context.world_origin +
        context.world_x * local_x + context.world_y * local_y;
    QVector3D normal = context.normal;
    const QVector3D view = (context.camera_position - point).normalized();
    if (QVector3D::dotProduct(normal, view) < 0.0f)
        normal = -normal;
    QVector3D ambient;
    QVector3D diffuse;
    for (const SoftwareLight &light : lights) {
        if (light.intensity <= 0.000001f)
            continue;
        if (light.type == TitleLightType::Ambient ||
            light.type == TitleLightType::Environment) {
            const float exposure = light.type == TitleLightType::Environment
                ? static_cast<float>(std::pow(2.0, std::clamp(
                      title.environment_exposure, -16.0, 16.0)))
                : 1.0f;
            ambient += light.color * light.intensity * exposure;
            continue;
        }
        QVector3D direction;
        float distance = 1000000.0f;
        float attenuation = 1.0f;
        if (light.type == TitleLightType::Parallel) {
            direction = -light.direction;
        } else {
            const QVector3D delta = light.position - point;
            distance = std::max(0.0001f, delta.length());
            direction = delta / distance;
            const float normalized = std::max(
                0.0f, (distance - light.source_size) /
                          light.falloff_distance);
            if (light.falloff == TitleLightFalloff::Linear)
                attenuation = std::clamp(1.0f - normalized, 0.0f, 1.0f);
            else if (light.falloff == TitleLightFalloff::InverseSquare)
                attenuation = 1.0f / (1.0f + 4.0f * normalized * normalized);
            if (light.type == TitleLightType::Spot) {
                const float cone = QVector3D::dotProduct(
                    light.direction, (point - light.position).normalized());
                attenuation *= std::clamp(
                    (cone - light.cone_outer_cos) /
                        std::max(0.0001f,
                            light.cone_inner_cos - light.cone_outer_cos),
                    0.0f, 1.0f);
            }
        }
        const float source_angular = light.type == TitleLightType::Parallel
            ? std::clamp(light.source_size / 1000.0f, 0.0f, 1.0f)
            : std::clamp(light.source_size / distance, 0.0f, 1.0f);
        const float raw_dot = QVector3D::dotProduct(normal, direction);
        const float n_dot_l = source_angular > 0.000001f
            ? std::clamp((raw_dot + source_angular) /
                         (2.0f * source_angular), 0.0f, 1.0f)
            : std::max(0.0f, raw_dot);
        if (n_dot_l <= 0.000001f || attenuation <= 0.000001f)
            continue;
        const QVector3D radiance = light.color *
            (light.intensity * attenuation);
        diffuse += radiance * (n_dot_l * context.diffuse);
        QVector3D half_vector = direction + view;
        if (half_vector.lengthSquared() > 0.000001f)
            half_vector.normalize();
        const float effective_roughness = std::clamp(
            context.roughness + source_angular, 0.02f, 1.0f);
        const float rough_power = 256.0f +
            (2.0f - 256.0f) * effective_roughness * effective_roughness;
        const float power = std::max(
            1.0f, std::min(context.shininess, rough_power));
        const float lobe = std::pow(std::max(
            0.0f, QVector3D::dotProduct(normal, half_vector)), power);
        sample.specular += radiance * (lobe * context.specular);
    }
    sample.multiplier = ambient * context.ambient +
                        diffuse * (1.0f - context.metallic);
    return sample;
}

QColor light_color(QColor color, const QVector3D &factor)
{
    color.setRedF(std::clamp(color.redF() * factor.x(), 0.0f, 1.0f));
    color.setGreenF(std::clamp(color.greenF() * factor.y(), 0.0f, 1.0f));
    color.setBlueF(std::clamp(color.blueF() * factor.z(), 0.0f, 1.0f));
    return color;
}

QGradient::Spread gradient_spread(int spread)
{
    if (spread == 1)
        return QGradient::ReflectSpread;
    if (spread == 2)
        return QGradient::RepeatSpread;
    return QGradient::PadSpread;
}

QColor gradient_color(std::uint32_t value, double opacity,
                      const QVector3D &lighting)
{
    QColor color = QColor::fromRgba(value);
    color.setAlphaF(std::clamp(color.alphaF() * opacity, 0.0, 1.0));
    return light_color(color, lighting);
}

QBrush rich_text_fill_brush(const RichTextFill &fill, const QRectF &bounds,
                            const QVector3D &lighting =
                                QVector3D(1.0f, 1.0f, 1.0f))
{
    if (fill.type != 1)
        return QBrush(light_color(argb(fill.color), lighting));

    const double opacity = std::clamp<double>(fill.gradient_opacity, 0.0, 1.0);
    const QColor start = gradient_color(
        fill.gradient_start_color, fill.gradient_start_opacity * opacity,
        lighting);
    const QColor end = gradient_color(
        fill.gradient_end_color, fill.gradient_end_opacity * opacity,
        lighting);
    const qreal start_pos = std::clamp<qreal>(fill.gradient_start_pos, 0.0, 1.0);
    const qreal end_pos = std::clamp<qreal>(fill.gradient_end_pos, 0.0, 1.0);
    if (fill.gradient_type == 1) {
        const QPointF center(
            bounds.left() + bounds.width() * fill.gradient_center_x,
            bounds.top() + bounds.height() * fill.gradient_center_y);
        const QPointF focal(
            bounds.left() + bounds.width() * fill.gradient_focal_x,
            bounds.top() + bounds.height() * fill.gradient_focal_y);
        QRadialGradient gradient(center,
            std::max<qreal>(1.0, std::max(bounds.width(), bounds.height()) *
                                    0.5 * std::max(0.01f, fill.gradient_scale)),
            focal);
        gradient.setSpread(gradient_spread(fill.gradient_spread));
        gradient.setColorAt(start_pos, start);
        gradient.setColorAt(end_pos, end);
        return QBrush(gradient);
    }
    if (fill.gradient_type == 2) {
        const QPointF center(
            bounds.left() + bounds.width() * fill.gradient_center_x,
            bounds.top() + bounds.height() * fill.gradient_center_y);
        QConicalGradient gradient(center, fill.gradient_angle);
        gradient.setSpread(gradient_spread(fill.gradient_spread));
        gradient.setColorAt(start_pos, start);
        gradient.setColorAt(end_pos, end);
        return QBrush(gradient);
    }
    const double radians = fill.gradient_angle * 3.14159265358979323846 / 180.0;
    const QPointF center = bounds.center();
    const QPointF axis(std::cos(radians) * bounds.width() * 0.5,
                       std::sin(radians) * bounds.height() * 0.5);
    QLinearGradient gradient(center - axis, center + axis);
    gradient.setSpread(gradient_spread(fill.gradient_spread));
    gradient.setColorAt(start_pos, start);
    gradient.setColorAt(end_pos, end);
    return QBrush(gradient);
}

RichTextDocument software_rich_text_model(const Layer &layer,
                                          double local_time)
{
    RichTextDocument canonical = rich_text_document_canonical_copy(layer);
    if (layer.type == LayerType::Clock || layer.type == LayerType::Ticker) {
        const RichTextCharFormat insertion =
            rich_text_effective_typing_format(canonical);
        rich_text_document_replace_text_canonical(
            canonical, layer.text_content, insertion,
            canonical.has_typing_format ? canonical.typing_format_mask : 0);
    }

    RichTextEvaluatedDefaults defaults;
    defaults.font_size = std::max(1, static_cast<int>(std::lround(
        layer.font_size_prop.evaluate(local_time))));
    defaults.tracking = static_cast<float>(
        layer.char_tracking_prop.evaluate(local_time));
    defaults.scale_x = static_cast<float>(
        layer.char_scale_x_prop.evaluate(local_time));
    defaults.scale_y = static_cast<float>(
        layer.char_scale_y_prop.evaluate(local_time));
    defaults.baseline_shift = static_cast<float>(
        layer.baseline_shift_prop.evaluate(local_time));
    defaults.solid_fill_color = evaluated_argb(
        layer.text_color_a, layer.text_color_r, layer.text_color_g,
        layer.text_color_b, layer.text_color, local_time);
    defaults.align_h = canonical.default_paragraph_format.align_h;
    defaults.align_v = canonical.default_paragraph_format.align_v;
    defaults.indent_left = static_cast<float>(
        layer.paragraph_indent_left_prop.evaluate(local_time));
    defaults.indent_right = static_cast<float>(
        layer.paragraph_indent_right_prop.evaluate(local_time));
    defaults.indent_first_line = static_cast<float>(
        layer.paragraph_indent_first_line_prop.evaluate(local_time));
    defaults.line_spacing = canonical.default_paragraph_format.line_spacing;
    defaults.space_before = static_cast<float>(
        layer.paragraph_space_before_prop.evaluate(local_time));
    defaults.space_after = static_cast<float>(
        layer.paragraph_space_after_prop.evaluate(local_time));
    defaults.hyphenate = canonical.default_paragraph_format.hyphenate;
    RichTextDocument model =
        rich_text_document_with_evaluated_defaults_canonical(
            std::move(canonical), defaults);
    return rich_text_document_with_auto_styles_canonical(
        std::move(model), fxm::style_presets::resolve_text_preset);
}

QRawFont software_raw_font(const TextLayoutRun &run)
{
    QRawFont raw = text_layout_registered_raw_font(run.font);
    if (raw.isValid())
        return raw;
    QFont font(QString::fromStdString(run.font.family));
    if (!run.font.style.empty())
        font.setStyleName(QString::fromStdString(run.font.style));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(
        std::max(1.0f, run.font.pixel_size)))));
    raw = QRawFont::fromFont(font);
    if (raw.isValid())
        raw.setPixelSize(std::max(1.0f, run.font.pixel_size));
    return raw;
}

struct SoftwareTextPiece {
    QPainterPath path;
    QRectF clip;
    std::size_t paint_index = 0;
    std::size_t cluster_index = 0;
    bool clipped = false;
};

bool draw_rich_text(QPainter &painter, const Layer &layer,
                    const QRectF &bounds, double local_time,
                    const QVector3D &lighting)
{
    RichTextDocument model = software_rich_text_model(layer, local_time);
    TextLayoutRequest request;
    request.document = model;
    request.max_width = static_cast<float>(std::max(1.0, bounds.width()));
    request.max_height = static_cast<float>(std::max(1.0, bounds.height()));
    request.minimum_horizontal_fit =
        std::clamp(layer.text_fit_min_scale, 0.05f, 1.0f);
    request.overflow_mode = layer.text_overflow_mode;
    const ImmutableTextLayout layout = cached_text_layout(request);
    if (!layout || !layout->valid)
        return false;

    std::vector<TextLayoutPaintRun> paint_runs =
        text_layout_paint_runs_canonical(model);
    if (paint_runs.empty()) {
        TextLayoutPaintRun run;
        run.byte_length = model.plain_text.size();
        run.style.fill = model.default_format.fill;
        run.style.stroke = model.default_format.stroke;
        run.style.underline = model.default_format.underline;
        run.style.strikethrough = model.default_format.strikethrough;
        paint_runs.push_back(run);
    }

    std::vector<std::vector<TextLayoutPaintSlice>> slices_by_cluster;
    slices_by_cluster.reserve(layout->clusters.size());
    for (const TextLayoutCluster &cluster : layout->clusters) {
        auto slices = text_layout_cluster_paint_slices(
            *layout, cluster, paint_runs);
        if (slices.empty())
            slices.push_back({0, cluster.x, cluster.x + cluster.width});
        slices_by_cluster.push_back(std::move(slices));
    }

    std::vector<SoftwareTextPiece> pieces;
    for (const TextLayoutGlyph &glyph : layout->glyphs) {
        if (glyph.run_index >= layout->runs.size() ||
            glyph.cluster_index >= layout->clusters.size())
            continue;
        const TextLayoutRun &run = layout->runs[glyph.run_index];
        const QRawFont raw = software_raw_font(run);
        if (!raw.isValid())
            return false;
        QPainterPath path = raw.pathForGlyph(glyph.glyph_id);
        if (path.isEmpty()) {
            if (!raw.boundingRect(glyph.glyph_id).isEmpty())
                return false;
            continue;
        }
        QTransform placement;
        placement.translate(bounds.left() + glyph.x,
                            bounds.top() + glyph.y);
        placement.scale(std::clamp(glyph.scale_x, 0.01f, 100.0f),
                        std::clamp(glyph.scale_y, 0.01f, 100.0f));
        path = placement.map(path);
        const auto &slices = slices_by_cluster[glyph.cluster_index];
        for (const TextLayoutPaintSlice &slice : slices) {
            SoftwareTextPiece piece;
            piece.path = path;
            piece.paint_index = std::min(slice.paint_index,
                                         paint_runs.size() - 1);
            piece.cluster_index = glyph.cluster_index;
            piece.clipped = run.split_ligature || slices.size() > 1;
            piece.clip = QRectF(bounds.left() + slice.x0,
                                bounds.top() - 100000.0,
                                std::max(0.0f, slice.x1 - slice.x0),
                                bounds.height() + 200000.0);
            if (run.split_ligature && run.clip_width > 0.0f &&
                run.clip_height > 0.0f) {
                piece.clip = piece.clip.intersected(QRectF(
                    bounds.left() + run.clip_x, bounds.top() + run.clip_y,
                    run.clip_width, run.clip_height));
            }
            pieces.push_back(std::move(piece));
        }
    }

    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    const auto draw_strokes = [&](bool on_front) {
        for (const SoftwareTextPiece &piece : pieces) {
            const RichTextStroke &stroke =
                paint_runs[piece.paint_index].style.stroke;
            if (!stroke.enabled || stroke.width <= 0.0f ||
                stroke.on_front != on_front)
                continue;
            painter.save();
            if (piece.clipped)
                painter.setClipRect(piece.clip, Qt::IntersectClip);
            QPen pen(rich_text_fill_brush(stroke.fill, bounds, lighting),
                     std::max(0.0f, stroke.width));
            pen.setJoinStyle(stroke.join_style == 1 ? Qt::RoundJoin
                : (stroke.join_style == 2 ? Qt::BevelJoin : Qt::MiterJoin));
            painter.setOpacity(painter.opacity() *
                               std::clamp(stroke.opacity, 0.0f, 1.0f));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(piece.path);
            painter.restore();
        }
    };
    draw_strokes(false);
    for (const SoftwareTextPiece &piece : pieces) {
        painter.save();
        if (piece.clipped)
            painter.setClipRect(piece.clip, Qt::IntersectClip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(rich_text_fill_brush(
            paint_runs[piece.paint_index].style.fill, bounds, lighting));
        painter.drawPath(piece.path);
        painter.restore();
    }
    draw_strokes(true);

    for (std::size_t cluster_index = 0;
         cluster_index < layout->clusters.size(); ++cluster_index) {
        const TextLayoutCluster &cluster = layout->clusters[cluster_index];
        if (cluster.line_index >= layout->lines.size())
            continue;
        const TextLayoutLine &line = layout->lines[cluster.line_index];
        for (const TextLayoutPaintSlice &slice : slices_by_cluster[cluster_index]) {
            const TextLayoutPaintStyle &style =
                paint_runs[std::min(slice.paint_index,
                                    paint_runs.size() - 1)].style;
            if (!style.underline && !style.strikethrough)
                continue;
            painter.setPen(Qt::NoPen);
            painter.setBrush(rich_text_fill_brush(
                style.fill, bounds, lighting));
            const qreal thickness = std::max<qreal>(
                1.0, (line.ascent + line.descent) / 18.0);
            if (style.underline) {
                const qreal y = bounds.top() + line.y + line.baseline +
                    std::max<qreal>(1.0, line.descent * 0.2);
                painter.drawRect(QRectF(bounds.left() + slice.x0, y,
                    std::max(0.0f, slice.x1 - slice.x0), thickness));
            }
            if (style.strikethrough) {
                const qreal y = bounds.top() + line.y + line.baseline -
                    line.ascent * 0.35;
                painter.drawRect(QRectF(bounds.left() + slice.x0, y,
                    std::max(0.0f, slice.x1 - slice.x0), thickness));
            }
        }
    }
    painter.restore();
    return true;
}

RichTextFill layer_shape_fill(const Layer &layer, double local_time)
{
    RichTextFill fill;
    fill.type = layer.fill_type;
    fill.color = evaluated_argb(
        layer.fill_color_a, layer.fill_color_r, layer.fill_color_g,
        layer.fill_color_b, layer.fill_color, local_time);
    fill.gradient_type = layer.gradient_type;
    fill.gradient_spread = layer.gradient_spread;
    fill.gradient_start_color = layer.gradient_start_color;
    fill.gradient_end_color = layer.gradient_end_color;
    fill.gradient_start_pos = layer.gradient_start_pos;
    fill.gradient_end_pos = layer.gradient_end_pos;
    fill.gradient_start_opacity = layer.gradient_start_opacity;
    fill.gradient_end_opacity = layer.gradient_end_opacity;
    fill.gradient_opacity = layer.gradient_opacity;
    fill.gradient_angle = layer.gradient_angle;
    fill.gradient_center_x = layer.gradient_center_x;
    fill.gradient_center_y = layer.gradient_center_y;
    fill.gradient_scale = layer.gradient_scale;
    fill.gradient_focal_x = layer.gradient_focal_x;
    fill.gradient_focal_y = layer.gradient_focal_y;
    return fill;
}

QImage softened_image(const QImage &source, double radius)
{
    if (source.isNull() || radius <= 0.25)
        return source;
    const double divisor = std::clamp(1.0 + radius * 0.35, 1.0, 32.0);
    const QSize reduced(
        std::max(1, static_cast<int>(std::lround(source.width() / divisor))),
        std::max(1, static_cast<int>(std::lround(source.height() / divisor))));
    QImage result = source.scaled(
        reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(source.size(), Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
    if (radius > 3.0) {
        result = result.scaled(
            reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .scaled(source.size(), Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
    }
    return result;
}

QImage tinted_alpha_image(const QImage &source, std::uint32_t packed,
                          double opacity)
{
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    if (source.isNull())
        return result;
    QPainter painter(&result);
    painter.drawImage(QPoint(), source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), argb(packed, opacity));
    return result;
}

QImage composite_images(const QImage &under, const QImage &over)
{
    QImage result(over.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage(QPoint(), under);
    painter.drawImage(QPoint(), over);
    return result;
}

QImage translated_samples(const QImage &source, const QPointF &delta,
                          int samples, bool centered)
{
    if (source.isNull())
        return source;
    samples = std::clamp(samples, 2, 32);
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setOpacity(1.0 / samples);
    for (int sample = 0; sample < samples; ++sample) {
        double amount = static_cast<double>(sample) / (samples - 1);
        if (centered)
            amount -= 0.5;
        painter.drawImage(delta * amount, source);
    }
    return result;
}

QImage alpha_outline(const QImage &source, double width,
                     std::uint32_t color, double opacity)
{
    const int radius = std::clamp(
        static_cast<int>(std::ceil(width)), 1, 48);
    QImage expanded(source.size(), QImage::Format_ARGB32_Premultiplied);
    expanded.fill(Qt::transparent);
    QPainter painter(&expanded);
    const int samples = std::clamp(radius * 3, 12, 96);
    painter.setOpacity(1.0);
    for (int sample = 0; sample < samples; ++sample) {
        const double angle = sample * 2.0 * 3.14159265358979323846 /
                             samples;
        painter.drawImage(QPointF(std::cos(angle) * radius,
                                  std::sin(angle) * radius), source);
    }
    painter.end();
    QImage outline = tinted_alpha_image(expanded, color, opacity);
    QPainter cut(&outline);
    cut.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    cut.drawImage(QPoint(), source);
    return outline;
}

void apply_color_effect(QImage &image, const ResolvedLayerEffect &effect,
                        double title_time)
{
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
    const QColor effect_color = QColor::fromRgba(effect.effect_color);
    const QColor secondary = QColor::fromRgba(effect.effect_secondary_color);
    const double amount = std::clamp<double>(
        effect.type == LayerEffectType::ColorOverlay
            ? effect.effect_opacity : effect.effect_amount,
        0.0, 1.0);
    for (int y = 0; y < pixels.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(pixels.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            QColor color = QColor::fromRgba(row[x]);
            const double alpha = color.alphaF();
            if (alpha <= 0.0)
                continue;
            double red = color.redF();
            double green = color.greenF();
            double blue = color.blueF();
            const double luma = red * 0.2126 + green * 0.7152 + blue * 0.0722;
            switch (effect.type) {
            case LayerEffectType::BrightnessContrast:
                red = (red - 0.5) * effect.contrast + 0.5 + effect.brightness;
                green = (green - 0.5) * effect.contrast + 0.5 + effect.brightness;
                blue = (blue - 0.5) * effect.contrast + 0.5 + effect.brightness;
                break;
            case LayerEffectType::Saturation:
                red = luma + (red - luma) * effect.saturation;
                green = luma + (green - luma) * effect.saturation;
                blue = luma + (blue - luma) * effect.saturation;
                break;
            case LayerEffectType::ColorOverlay:
                red += (effect_color.redF() - red) * amount;
                green += (effect_color.greenF() - green) * amount;
                blue += (effect_color.blueF() - blue) * amount;
                break;
            case LayerEffectType::Vignette: {
                const double nx = (x + 0.5) / std::max(1, pixels.width()) -
                                  effect.effect_center_x;
                const double ny = (y + 0.5) / std::max(1, pixels.height()) -
                                  effect.effect_center_y;
                const double edge = std::clamp(
                    std::hypot(nx, ny) * (1.0 + effect.effect_size / 50.0),
                    0.0, 1.0);
                const double shade = 1.0 - edge * effect.effect_amount;
                red *= shade; green *= shade; blue *= shade;
                break;
            }
            case LayerEffectType::Posterize: {
                const double levels = std::clamp<double>(
                    std::round(std::max(2.0f, effect.effect_amount)),
                    2.0, 64.0);
                red = std::round(red * (levels - 1.0)) / (levels - 1.0);
                green = std::round(green * (levels - 1.0)) / (levels - 1.0);
                blue = std::round(blue * (levels - 1.0)) / (levels - 1.0);
                break;
            }
            case LayerEffectType::Threshold: {
                const double value = luma >= std::clamp<double>(
                    effect.effect_amount, 0.0, 1.0) ? 1.0 : 0.0;
                red = green = blue = value;
                break;
            }
            case LayerEffectType::Scanlines:
                if ((y / std::max(1, static_cast<int>(effect.effect_scale))) % 2)
                    red *= 1.0 - amount, green *= 1.0 - amount,
                    blue *= 1.0 - amount;
                break;
            case LayerEffectType::Noise:
            case LayerEffectType::Grain:
            case LayerEffectType::FilmDistortion:
            case LayerEffectType::AnalogDistortion:
            case LayerEffectType::DigitalDistortion: {
                std::uint32_t hash = static_cast<std::uint32_t>(
                    x * 374761393u + y * 668265263u +
                    effect.source->effect_seed * 2246822519u +
                    static_cast<int>(title_time * effect.effect_speed * 60.0));
                hash = (hash ^ (hash >> 13)) * 1274126177u;
                const double noise = ((hash & 0xffffu) / 32767.5 - 1.0) *
                                     effect.effect_amount;
                if (effect.effect_monochrome) {
                    red += noise; green += noise; blue += noise;
                } else {
                    red += noise;
                    green += (((hash >> 8) & 0xffffu) / 32767.5 - 1.0) *
                             effect.effect_amount;
                    blue += (((hash >> 16) & 0xffffu) / 32767.5 - 1.0) *
                            effect.effect_amount;
                }
                break;
            }
            case LayerEffectType::ChromaKey:
            case LayerEffectType::ColorRange: {
                const double distance = std::sqrt(
                    std::pow(red - effect_color.redF(), 2.0) +
                    std::pow(green - effect_color.greenF(), 2.0) +
                    std::pow(blue - effect_color.blueF(), 2.0));
                const double threshold = std::max(0.001f, effect.effect_amount);
                if ((distance < threshold) != effect.effect_invert)
                    color.setAlphaF(alpha * std::clamp(
                        distance / threshold, 0.0, 1.0));
                break;
            }
            case LayerEffectType::LumaKey: {
                const bool keyed = luma < effect.effect_amount;
                if (keyed != effect.effect_invert)
                    color.setAlphaF(alpha * std::clamp(
                        luma / std::max(0.001f, effect.effect_amount),
                        0.0, 1.0));
                break;
            }
            case LayerEffectType::SpillSuppression: {
                const double spill = std::max(
                    0.0, green - std::max(red, blue));
                green -= spill * effect.effect_amount;
                red += spill * effect.effect_amount * secondary.redF() * 0.5;
                blue += spill * effect.effect_amount * secondary.blueF() * 0.5;
                break;
            }
            default:
                break;
            }
            color.setRedF(std::clamp(red, 0.0, 1.0));
            color.setGreenF(std::clamp(green, 0.0, 1.0));
            color.setBlueF(std::clamp(blue, 0.0, 1.0));
            row[x] = color.rgba();
        }
    }
    image = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void apply_software_effect(QImage &image, const ResolvedLayerEffect &effect,
                           const QRectF &layer_bounds,
                           const QRectF &raster_bounds, double title_time)
{
    if (!effect.enabled || image.isNull())
        return;
    const double radians = effect.effect_angle *
        3.14159265358979323846 / 180.0;
    const QPointF delta(std::cos(radians) * effect.effect_distance,
                        std::sin(radians) * effect.effect_distance);
    switch (effect.type) {
    case LayerEffectType::BackgroundColor: {
        QImage background(image.size(), QImage::Format_ARGB32_Premultiplied);
        background.fill(Qt::transparent);
        QPainter painter(&background);
        QRectF rect = layer_bounds.adjusted(
            -effect.effect_padding_left, -effect.effect_padding_top,
            effect.effect_padding_right, effect.effect_padding_bottom)
            .translated(-raster_bounds.topLeft());
        painter.setPen(effect.effect_stroke_width > 0.0f
            ? QPen(argb(effect.effect_stroke_color,
                        effect.effect_stroke_opacity),
                   effect.effect_stroke_width)
            : Qt::NoPen);
        painter.setBrush(argb(effect.effect_color, effect.effect_opacity));
        painter.drawRoundedRect(rect, effect.effect_corner_radius_tl,
                                effect.effect_corner_radius_tl);
        image = composite_images(background, image);
        break;
    }
    case LayerEffectType::Outline: {
        QImage outline = alpha_outline(image, effect.effect_size,
            effect.effect_color, effect.effect_opacity);
        image = effect.effect_on_front
            ? composite_images(image, outline)
            : composite_images(outline, image);
        break;
    }
    case LayerEffectType::DropShadow:
    case LayerEffectType::Glow:
    case LayerEffectType::Bloom:
    case LayerEffectType::Halation:
    case LayerEffectType::Glare: {
        QImage halo = softened_image(
            tinted_alpha_image(image, effect.effect_color,
                               effect.effect_opacity),
            effect.effect_size + effect.effect_spread);
        QImage under(image.size(), QImage::Format_ARGB32_Premultiplied);
        under.fill(Qt::transparent);
        QPainter painter(&under);
        const QPointF position = effect.type == LayerEffectType::DropShadow
            ? delta : QPointF();
        painter.drawImage(position, halo);
        if (effect.type == LayerEffectType::Glare) {
            const QImage streak = translated_samples(
                halo, delta, std::max(4, effect.effect_samples), true);
            painter.drawImage(QPoint(), streak);
        }
        painter.end();
        image = composite_images(under, image);
        break;
    }
    case LayerEffectType::LongShadow: {
        QImage shadow = tinted_alpha_image(
            image, effect.effect_color, effect.effect_opacity);
        QImage under = translated_samples(
            shadow, delta, std::max(4, effect.effect_samples), false);
        if (effect.effect_size > 0.0f)
            under = softened_image(under, effect.effect_size);
        image = composite_images(under, image);
        break;
    }
    case LayerEffectType::InnerGlow:
    case LayerEffectType::InnerShadow:
    case LayerEffectType::LightWrap: {
        QImage inner = softened_image(
            tinted_alpha_image(image, effect.effect_color,
                               effect.effect_opacity), effect.effect_size);
        if (effect.type == LayerEffectType::InnerShadow) {
            QImage shifted(image.size(), QImage::Format_ARGB32_Premultiplied);
            shifted.fill(Qt::transparent);
            QPainter shifted_painter(&shifted);
            shifted_painter.drawImage(delta, inner);
            inner = shifted;
        }
        QPainter clip(&inner);
        clip.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        clip.drawImage(QPoint(), image);
        clip.end();
        QPainter over(&image);
        over.drawImage(QPoint(), inner);
        break;
    }
    case LayerEffectType::Blur:
        image = softened_image(image, effect.effect_size);
        break;
    case LayerEffectType::MotionBlur:
    case LayerEffectType::DirectionalBlur:
        image = translated_samples(
            image, QPointF(std::cos(radians) * effect.effect_size,
                           std::sin(radians) * effect.effect_size),
            effect.effect_samples, effect.effect_centered);
        break;
    case LayerEffectType::Pixelate: {
        const int block = std::clamp(
            static_cast<int>(std::lround(std::max(
                effect.effect_size, effect.effect_scale))), 2, 128);
        const QSize reduced(std::max(1, image.width() / block),
                            std::max(1, image.height() / block));
        image = image.scaled(reduced, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation)
                     .scaled(image.size(), Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
        break;
    }
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::HighPass:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen:
    case LayerEffectType::EdgeDetect:
    case LayerEffectType::Emboss: {
        const QImage blur = softened_image(image,
            std::max(1.0f, effect.effect_size));
        QImage original = image.convertToFormat(QImage::Format_ARGB32);
        const QImage low = blur.convertToFormat(QImage::Format_ARGB32);
        const double strength = std::clamp<double>(
            effect.effect_amount > 0.0f ? effect.effect_amount : 1.0f,
            0.0, 4.0);
        for (int y = 0; y < original.height(); ++y) {
            QRgb *destination = reinterpret_cast<QRgb *>(original.scanLine(y));
            const QRgb *soft = reinterpret_cast<const QRgb *>(low.constScanLine(y));
            for (int x = 0; x < original.width(); ++x) {
                const QRgb base = destination[x];
                const auto channel = [&](int value, int blurred) {
                    if (effect.type == LayerEffectType::HighPass ||
                        effect.type == LayerEffectType::EdgeDetect)
                        return std::clamp(128 + static_cast<int>((value - blurred) * strength), 0, 255);
                    return std::clamp(static_cast<int>(
                        value + (value - blurred) * strength), 0, 255);
                };
                destination[x] = qRgba(
                    channel(qRed(base), qRed(soft[x])),
                    channel(qGreen(base), qGreen(soft[x])),
                    channel(qBlue(base), qBlue(soft[x])), qAlpha(base));
            }
        }
        image = original.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        break;
    }
    case LayerEffectType::ZoomBlur:
    case LayerEffectType::RadialBlur:
        image = softened_image(image, std::max(1.0f, effect.effect_size * 0.25f));
        break;
    case LayerEffectType::LensFlare: {
        QImage flare(image.size(), QImage::Format_ARGB32_Premultiplied);
        flare.fill(Qt::transparent);
        const QPointF center(image.width() * effect.effect_center_x,
                             image.height() * effect.effect_center_y);
        QRadialGradient gradient(center, std::max(1.0f, effect.effect_size));
        gradient.setColorAt(0.0, argb(effect.effect_color,
                                     effect.effect_opacity));
        gradient.setColorAt(1.0, Qt::transparent);
        QPainter painter(&flare);
        painter.fillRect(flare.rect(), gradient);
        painter.end();
        QPainter over(&image);
        over.setCompositionMode(QPainter::CompositionMode_Screen);
        over.drawImage(QPoint(), flare);
        break;
    }
    case LayerEffectType::FourColorGradient: {
        QLinearGradient gradient(0.0, 0.0, image.width(), image.height());
        gradient.setColorAt(0.0, argb(effect.effect_color,
                                     effect.effect_opacity));
        gradient.setColorAt(1.0, argb(effect.effect_secondary_color,
                                     effect.effect_opacity));
        QImage overlay(image.size(), QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter painter(&overlay);
        painter.fillRect(overlay.rect(), gradient);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(QPoint(), image);
        painter.end();
        QPainter over(&image);
        over.drawImage(QPoint(), overlay);
        break;
    }
    case LayerEffectType::MatteChoker:
    case LayerEffectType::RoughenEdges: {
        QImage alpha = softened_image(image,
            std::max(1.0f, effect.effect_size));
        QImage result = image.convertToFormat(QImage::Format_ARGB32);
        const QImage mask = alpha.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < result.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
            const QRgb *mask_row = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                QColor color = QColor::fromRgba(row[x]);
                const int threshold = effect.type == LayerEffectType::MatteChoker
                    ? static_cast<int>(128 + effect.effect_amount * 127.0f)
                    : static_cast<int>(128 + std::sin(
                          x * 0.37 + y * 0.61 + effect.effect_evolution) *
                          effect.effect_amount * 64.0f);
                color.setAlpha(qAlpha(mask_row[x]) >= threshold
                    ? color.alpha() : 0);
                row[x] = color.rgba();
            }
        }
        image = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        break;
    }
    case LayerEffectType::LensDistortion:
    case LayerEffectType::ChromaticAberration:
    case LayerEffectType::Ripple:
    case LayerEffectType::WaveWarp:
    case LayerEffectType::DisplacementMap:
        /* A bounded directional warp keeps these controls visible in the
         * standalone preview; OBS remains the exact shader implementation. */
        image = translated_samples(image,
            QPointF(std::cos(radians) * effect.effect_amount,
                    std::sin(radians) * effect.effect_amount),
            std::max(2, effect.effect_samples), true);
        break;
    case LayerEffectType::BrightnessContrast:
    case LayerEffectType::Saturation:
    case LayerEffectType::ColorOverlay:
    case LayerEffectType::Vignette:
    case LayerEffectType::Noise:
    case LayerEffectType::Grain:
    case LayerEffectType::FilmDistortion:
    case LayerEffectType::AnalogDistortion:
    case LayerEffectType::DigitalDistortion:
    case LayerEffectType::Posterize:
    case LayerEffectType::Threshold:
    case LayerEffectType::Scanlines:
    case LayerEffectType::ChromaKey:
    case LayerEffectType::LumaKey:
    case LayerEffectType::ColorRange:
    case LayerEffectType::SpillSuppression:
        apply_color_effect(image, effect, title_time);
        break;
    case LayerEffectType::TrimPaths:
        /* Trim Paths is applied to vector geometry before rasterization. */
        break;
    }
}

struct SoftwareLayerRaster {
    QImage image;
    QRectF local_bounds;
};

struct SoftwareLayerRasterCacheEntry {
    std::string key;
    QRectF source_bounds;
    SoftwareLayerRaster raster;
};

using SoftwareLayerRasterCache =
    std::unordered_map<std::string, SoftwareLayerRasterCacheEntry>;

void paint_software_layer_content(QPainter &painter, const Layer &layer,
                                  const QRectF &bounds, double local_time,
                                  double title_time)
{
    const QVector3D unit_lighting(1.0f, 1.0f, 1.0f);
    if (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
        layer.type == LayerType::Ticker) {
        if (!draw_rich_text(
                painter, layer, bounds, local_time, unit_lighting)) {
            QFont font(QString::fromStdString(layer.font_family));
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(
                layer.font_size_prop.evaluate(local_time)))));
            font.setBold(layer.font_bold);
            font.setItalic(layer.font_italic);
            painter.setFont(font);
            painter.setPen(argb(evaluated_argb(
                layer.text_color_a, layer.text_color_r,
                layer.text_color_g, layer.text_color_b,
                layer.text_color, local_time)));
            painter.drawText(bounds, Qt::AlignLeft | Qt::AlignVCenter |
                Qt::TextWordWrap, QString::fromStdString(layer.text_content));
        }
    } else if (layer.type == LayerType::Video) {
        const fxm::video::VideoFrame frame =
            fxm::video::FrameRuntime::instance().frame_for_layer(
                layer, title_time, fxm::current_frame_rate(),
                fxm::video::VideoDecodeClient::Editor);
        if (!frame.image.isNull())
            painter.drawImage(bounds, frame.image);
    } else if (layer.type == LayerType::Image &&
               !layer.image_path.empty()) {
        QImageReader reader(QString::fromStdString(layer.image_path));
        const QImage source = reader.read();
        if (!source.isNull())
            painter.drawImage(bounds, source);
    } else if (layer.type == LayerType::Chart) {
        fxm::chart::render(painter, layer, bounds, local_time);
    } else if (layer.type != LayerType::Group &&
               layer.type != LayerType::Asset &&
               layer.type != LayerType::Adjustment) {
        const QPen outline = layer.outline_enabled
            ? QPen(argb(evaluated_argb(
                       layer.stroke_color_a, layer.stroke_color_r,
                       layer.stroke_color_g, layer.stroke_color_b,
                       layer.stroke_color, local_time)),
                   std::max(0.0f, layer.stroke_width))
            : QPen(Qt::NoPen);
        const QBrush fill = rich_text_fill_brush(
            layer_shape_fill(layer, local_time), bounds);
        if (layer.type == LayerType::Shape ||
            layer.type == LayerType::SolidRect) {
            const QPainterPath path = fxm::layer_shape_path(layer, bounds);
            if (!path.isEmpty()) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(fill);
                painter.drawPath(path);
                if (layer.outline_enabled && layer.stroke_width > 0.0f) {
                    QPainterPath stroke_path = path;
                    for (const LayerEffect &effect : layer.effects) {
                        const ResolvedLayerEffect resolved =
                            resolve_layer_effect(effect, local_time);
                        if (!resolved.enabled || resolved.type !=
                                LayerEffectType::TrimPaths)
                            continue;
                        fxm::TrimPathsGeometryOptions options;
                        options.start_percent = resolved.effect_trim_start;
                        options.end_percent = resolved.effect_trim_end;
                        options.trim_offset_degrees =
                            resolved.effect_trim_offset;
                        options.individually =
                            resolved.effect_trim_multiple_shapes != 0;
                        stroke_path = fxm::apply_trim_paths_geometry(
                            stroke_path, options);
                    }
                    painter.setPen(outline);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(stroke_path);
                }
            }
        } else {
            painter.setPen(outline);
            painter.setBrush(fill);
            painter.drawRect(bounds);
        }
    }
}

SoftwareLayerRaster software_layer_raster(const Layer &layer,
                                          const QRectF &bounds,
                                          double local_time,
                                          double title_time)
{
    EffectBoundsExpansion expansion;
    std::vector<ResolvedLayerEffect> effects;
    effects.reserve(layer.effects.size());
    for (const LayerEffect &effect : layer.effects) {
        const ResolvedLayerEffect resolved = resolve_layer_effect(
            effect, local_time);
        if (!resolved.enabled)
            continue;
        effects.push_back(resolved);
        expansion.accumulate(effect_bounds_expansion(
            resolved, bounds.width(), bounds.height()));
    }
    const double maximum_padding = std::max(
        64.0, std::max(bounds.width(), bounds.height()) * 2.0);
    const QRectF raster_bounds = bounds.adjusted(
        -std::min(expansion.left, maximum_padding),
        -std::min(expansion.top, maximum_padding),
        std::min(expansion.right, maximum_padding),
        std::min(expansion.bottom, maximum_padding));
    const QRect aligned = raster_bounds.toAlignedRect();
    SoftwareLayerRaster raster;
    raster.local_bounds = QRectF(aligned);
    raster.image = QImage(std::max(1, aligned.width()),
                          std::max(1, aligned.height()),
                          QImage::Format_ARGB32_Premultiplied);
    raster.image.fill(Qt::transparent);
    QPainter painter(&raster.image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(-raster.local_bounds.left(),
                      -raster.local_bounds.top());
    paint_software_layer_content(
        painter, layer, bounds, local_time, title_time);
    painter.end();
    for (const ResolvedLayerEffect &effect : effects)
        apply_software_effect(raster.image, effect, bounds,
                              raster.local_bounds, title_time);
    return raster;
}

SoftwareLayerRaster cached_software_layer_raster(
    const Layer &layer, const QRectF &bounds, double local_time,
    double title_time, SoftwareLayerRasterCache *cache,
    bool transform_only_update)
{
    if (!cache)
        return software_layer_raster(layer, bounds, local_time, title_time);

    /* Decoded video frames are published asynchronously and can change while
     * model/time fingerprints remain identical (notably the first frame after
     * a seek). Always ask FrameRuntime for its newest resident frame. */
    if (layer.type == LayerType::Video)
        return software_layer_raster(layer, bounds, local_time, title_time);

    auto existing = cache->find(layer.id);
    if (transform_only_update && existing != cache->end() &&
        existing->second.source_bounds == bounds &&
        !existing->second.raster.image.isNull()) {
        return existing->second.raster;
    }

    std::string key = layer_render_fingerprint(layer) +
        "|bounds=" + std::to_string(bounds.left()) + ',' +
        std::to_string(bounds.top()) + ',' +
        std::to_string(bounds.width()) + 'x' +
        std::to_string(bounds.height());
    if (layer.type == LayerType::Chart) {
        key += "|chart-data=" + fxm::chart::data_fingerprint(layer);
        if (fxm::chart::transition_active(layer))
            key += "|chart-transition=" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count() / 16);
    }
    if (fxm::asset_runtime::layer_has_raster_animation(layer)) {
        key += "|time-ms=" + std::to_string(
            static_cast<long long>(std::llround(local_time * 1000.0)));
    }

    SoftwareLayerRasterCacheEntry &entry = (*cache)[layer.id];
    if (entry.key != key || entry.raster.image.isNull()) {
        entry.key = std::move(key);
        entry.source_bounds = bounds;
        entry.raster = software_layer_raster(
            layer, bounds, local_time, title_time);
    }
    return entry.raster;
}

QImage light_image(const QImage &source, const QVector3D &lighting)
{
    if (source.isNull() ||
        (std::abs(lighting.x() - 1.0f) < 0.0001f &&
         std::abs(lighting.y() - 1.0f) < 0.0001f &&
         std::abs(lighting.z() - 1.0f) < 0.0001f))
        return source;
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y) {
        QRgb *pixels = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb pixel = pixels[x];
            pixels[x] = qRgba(
                std::clamp(static_cast<int>(std::lround(
                    qRed(pixel) * lighting.x())), 0, 255),
                std::clamp(static_cast<int>(std::lround(
                    qGreen(pixel) * lighting.y())), 0, 255),
                std::clamp(static_cast<int>(std::lround(
                    qBlue(pixel) * lighting.z())), 0, 255),
                qAlpha(pixel));
        }
    }
    return result;
}

QImage shade_software_image(const QImage &source, const QRectF &local_bounds,
                            const Title &title, const Layer &layer,
                            double title_time, double local_time,
                            const std::vector<SoftwareLight> &lights)
{
    const SoftwareLightingContext context = software_lighting_context(
        title, layer, title_time, local_time);
    if (!context.active || source.isNull())
        return source;
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    /* Point/Spot falloff and view-dependent specular are smooth over a planar
     * layer.  Evaluating their complete vector/pow pipeline for every source
     * pixel made a single 1080p material cost roughly an entire frame budget
     * in the standalone editor.  Sample a bounded lighting lattice and
     * bilinearly reconstruct it while retaining per-pixel authored color and
     * alpha. */
    constexpr int kLightingCellPixels = 12;
    const int grid_width = std::max(
        2, (result.width() + kLightingCellPixels - 1) /
               kLightingCellPixels + 1);
    const int grid_height = std::max(
        2, (result.height() + kLightingCellPixels - 1) /
               kLightingCellPixels + 1);
    std::vector<SoftwareLightingSample> grid(
        static_cast<std::size_t>(grid_width * grid_height));
    const double local_width = std::max(0.0001, local_bounds.width());
    const double local_height = std::max(0.0001, local_bounds.height());
    auto local_x_for_pixel = [&](double pixel_x) {
        return static_cast<float>(local_bounds.left() +
            pixel_x * local_width / std::max(1, result.width()));
    };
    auto local_y_for_pixel = [&](double pixel_y) {
        return static_cast<float>(local_bounds.top() +
            pixel_y * local_height / std::max(1, result.height()));
    };
    for (int gy = 0; gy < grid_height; ++gy) {
        const double pixel_y = std::min(
            static_cast<double>(result.height()),
            static_cast<double>(gy * kLightingCellPixels));
        for (int gx = 0; gx < grid_width; ++gx) {
            const double pixel_x = std::min(
                static_cast<double>(result.width()),
                static_cast<double>(gx * kLightingCellPixels));
            grid[static_cast<std::size_t>(gy * grid_width + gx)] =
                sample_software_lighting(
                    title, context, lights, local_x_for_pixel(pixel_x),
                    local_y_for_pixel(pixel_y));
        }
    }
    auto lerp_vector = [](const QVector3D &a, const QVector3D &b, float t) {
        return a + (b - a) * t;
    };
    for (int y = 0; y < result.height(); ++y) {
        QRgb *pixels = reinterpret_cast<QRgb *>(result.scanLine(y));
        const int gy = std::min(y / kLightingCellPixels, grid_height - 2);
        const float fy = static_cast<float>(
            y - gy * kLightingCellPixels) / kLightingCellPixels;
        for (int x = 0; x < result.width(); ++x) {
            if (qAlpha(pixels[x]) == 0)
                continue;
            const int gx = std::min(x / kLightingCellPixels, grid_width - 2);
            const float fx = static_cast<float>(
                x - gx * kLightingCellPixels) / kLightingCellPixels;
            const SoftwareLightingSample &s00 = grid[static_cast<std::size_t>(
                gy * grid_width + gx)];
            const SoftwareLightingSample &s10 = grid[static_cast<std::size_t>(
                gy * grid_width + gx + 1)];
            const SoftwareLightingSample &s01 = grid[static_cast<std::size_t>(
                (gy + 1) * grid_width + gx)];
            const SoftwareLightingSample &s11 = grid[static_cast<std::size_t>(
                (gy + 1) * grid_width + gx + 1)];
            SoftwareLightingSample lighting;
            lighting.multiplier = lerp_vector(
                lerp_vector(s00.multiplier, s10.multiplier, fx),
                lerp_vector(s01.multiplier, s11.multiplier, fx), fy);
            lighting.specular = lerp_vector(
                lerp_vector(s00.specular, s10.specular, fx),
                lerp_vector(s01.specular, s11.specular, fx), fy);
            const QRgb pixel = pixels[x];
            const QVector3D base(
                qRed(pixel) / 255.0f, qGreen(pixel) / 255.0f,
                qBlue(pixel) / 255.0f);
            const QVector3D fresnel = QVector3D(
                0.04f + (base.x() - 0.04f) * context.metallic,
                0.04f + (base.y() - 0.04f) * context.metallic,
                0.04f + (base.z() - 0.04f) * context.metallic);
            const QVector3D shaded(
                base.x() * lighting.multiplier.x() +
                    lighting.specular.x() * fresnel.x() + context.emissive.x(),
                base.y() * lighting.multiplier.y() +
                    lighting.specular.y() * fresnel.y() + context.emissive.y(),
                base.z() * lighting.multiplier.z() +
                    lighting.specular.z() * fresnel.z() + context.emissive.z());
            pixels[x] = qRgba(
                std::clamp(static_cast<int>(std::lround(
                    shaded.x() * 255.0f)), 0, 255),
                std::clamp(static_cast<int>(std::lround(
                    shaded.y() * 255.0f)), 0, 255),
                std::clamp(static_cast<int>(std::lround(
                    shaded.z() * 255.0f)), 0, 255),
                qAlpha(pixel));
        }
    }
    return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage software_layer_shadow_mask(const Layer &layer, const QRectF &bounds,
                                  double local_time)
{
    const int width = std::max(1, static_cast<int>(std::ceil(bounds.width())));
    const int height = std::max(1, static_cast<int>(std::ceil(bounds.height())));
    QImage mask(width, height, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(-bounds.left(), -bounds.top());

    if (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
        layer.type == LayerType::Ticker) {
        if (!draw_rich_text(painter, layer, bounds, local_time,
                            QVector3D(0.0f, 0.0f, 0.0f))) {
            QFont font(QString::fromStdString(layer.font_family));
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(
                layer.font_size_prop.evaluate(local_time)))));
            font.setBold(layer.font_bold);
            font.setItalic(layer.font_italic);
            painter.setFont(font);
            painter.setPen(Qt::black);
            painter.drawText(bounds, Qt::AlignLeft | Qt::AlignVCenter |
                Qt::TextWordWrap, QString::fromStdString(layer.text_content));
        }
    } else if ((layer.type == LayerType::Image ||
                layer.type == LayerType::Video) &&
               !layer.image_path.empty()) {
        QImageReader reader(QString::fromStdString(layer.image_path));
        const QImage source = reader.read();
        if (!source.isNull())
            painter.drawImage(bounds, source);
    } else if (layer.type == LayerType::Chart) {
        fxm::chart::render(painter, layer, bounds, local_time);
    } else if (layer.type != LayerType::Group &&
               layer.type != LayerType::Asset &&
               layer.type != LayerType::Adjustment) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        if (layer.type == LayerType::Shape ||
            layer.type == LayerType::SolidRect) {
            const QPainterPath path = fxm::layer_shape_path(layer, bounds);
            if (!path.isEmpty())
                painter.drawPath(path);
        } else {
            painter.drawRect(bounds);
        }
    }
    painter.end();

    QPainter tint(&mask);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(mask.rect(), Qt::black);
    return mask;
}

QTransform translated_canvas_transform(const QTransform &source,
                                       const QPointF &offset)
{
    return QTransform(
        source.m11() + offset.x() * source.m13(),
        source.m12() + offset.y() * source.m13(), source.m13(),
        source.m21() + offset.x() * source.m23(),
        source.m22() + offset.y() * source.m23(), source.m23(),
        source.m31() + offset.x() * source.m33(),
        source.m32() + offset.y() * source.m33(), source.m33());
}

bool software_shadow_parameters(const Title &title, const Layer &layer,
                                const QRectF &bounds, double title_time,
                                const std::vector<SoftwareLight> &lights,
                                QPointF &offset, float &darkness,
                                float &softness)
{
    if (!title.lighting_enabled || !layer.material_casts_shadows ||
        !fxm::transform3d::layer_supports_3d(layer) ||
        !fxm::transform3d::layer_or_ancestor_uses_3d(title, layer))
        return false;
    const SoftwareLight *shadow_light = nullptr;
    for (const SoftwareLight &candidate : lights) {
        if (candidate.casts_shadows && candidate.intensity > 0.000001f &&
            candidate.type != TitleLightType::Ambient &&
            candidate.type != TitleLightType::Environment) {
            shadow_light = &candidate;
            break;
        }
    }
    if (!shadow_light)
        return false;

    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        title, layer, title_time);
    const QVector3D center = world.map(QVector3D(
        static_cast<float>(bounds.center().x()),
        static_cast<float>(bounds.center().y()), 0.0f));
    QVector3D ray = shadow_light->type == TitleLightType::Parallel
        ? shadow_light->direction
        : center - shadow_light->position;
    if (ray.lengthSquared() <= 0.000001f)
        return false;
    ray.normalize();
    QPointF projected_center;
    QPointF projected_shadow;
    if (!fxm::transform3d::project_world_point(
            title, center, title_time, projected_center) ||
        !fxm::transform3d::project_world_point(
            title, center + ray * 80.0f, title_time, projected_shadow))
        return false;
    offset = projected_shadow - projected_center;
    if (std::hypot(offset.x(), offset.y()) < 0.25)
        return false;
    darkness = std::clamp(
        shadow_light->shadow_darkness * shadow_light->intensity, 0.0f, 1.0f);
    softness = std::clamp(
        shadow_light->shadow_softness + shadow_light->source_size * 0.05f,
        0.0f, 24.0f);
    return darkness > 0.0001f;
}

void draw_software_3d_shadow(QPainter &painter, const Title &title,
                             const Layer &layer, const QRectF &bounds,
                             double title_time, double local_time,
                             const QTransform &local_to_canvas,
                             const std::vector<SoftwareLight> &lights,
                             double layer_opacity, double preview_scale)
{
    QPointF offset;
    float darkness = 0.0f;
    float softness = 0.0f;
    if (!software_shadow_parameters(title, layer, bounds, title_time, lights,
                                    offset, darkness, softness))
        return;
    QImage mask = software_layer_shadow_mask(layer, bounds, local_time);
    if (mask.isNull())
        return;
    if (preview_scale < 0.999) {
        mask = mask.scaled(
            std::max(1, qRound(mask.width() * preview_scale)),
            std::max(1, qRound(mask.height() * preview_scale)),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const int radius = std::clamp(static_cast<int>(std::ceil(softness)), 0, 8);
    const int diameter = radius * 2 + 1;
    const int sample_count = diameter * diameter;
    painter.save();
    painter.setOpacity(std::clamp(layer_opacity * darkness /
                                  std::max(1, sample_count), 0.0, 1.0));
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            const QPointF sample_offset = offset + QPointF(x, y);
            painter.setWorldTransform(translated_canvas_transform(
                local_to_canvas, sample_offset), true);
            painter.drawImage(bounds, mask);
        }
    }
    painter.restore();
}

void append_software_render_tree(const Title &title, const Layer &layer,
                                 double time,
                                 std::vector<const Layer *> &ordered,
                                 std::unordered_set<std::string> &visiting)
{
    if (!visiting.insert(layer.id).second)
        return;
    if (layer_type_is_container(layer.type)) {
        const std::vector<const Layer *> children =
            fxm::transform3d::ordered_group_children(title, layer.id, time);
        for (const Layer *child : children) {
            if (child)
                append_software_render_tree(
                    title, *child, time, ordered, visiting);
        }
    } else {
        ordered.push_back(&layer);
    }
    visiting.erase(layer.id);
}

std::vector<const Layer *> software_render_order(const Title &title,
                                                 double time)
{
    std::vector<const Layer *> ordered;
    std::unordered_set<std::string> visiting;
    const std::vector<std::size_t> roots =
        fxm::transform3d::ordered_root_layer_indices(
            title, 0, title.layers.size(), time);
    for (std::size_t index : roots) {
        if (index < title.layers.size() && title.layers[index])
            append_software_render_tree(
                title, *title.layers[index], time, ordered, visiting);
    }
    return ordered;
}

QPainter::CompositionMode software_blend_mode(EffectBlendMode mode)
{
    switch (mode) {
    case EffectBlendMode::Multiply:
        return QPainter::CompositionMode_Multiply;
    case EffectBlendMode::Additive:
        return QPainter::CompositionMode_Plus;
    case EffectBlendMode::Screen:
        return QPainter::CompositionMode_Screen;
    case EffectBlendMode::Overlay:
        return QPainter::CompositionMode_Overlay;
    case EffectBlendMode::Color:
        return QPainter::CompositionMode_SourceOver;
    case EffectBlendMode::Normal:
    default:
        return QPainter::CompositionMode_SourceOver;
    }
}

QImage render_software_title(const Title &title, double time,
                             SoftwareLayerRasterCache *raster_cache = nullptr,
                             bool transform_only_update = false,
                             double preview_scale = 1.0)
{
    preview_scale = std::clamp(preview_scale, 0.05, 1.0);
    QImage image(std::max(1, qRound(title.width * preview_scale)),
                 std::max(1, qRound(title.height * preview_scale)),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(preview_scale, preview_scale);
    const std::vector<SoftwareLight> lights = software_lights(title, time);
    const std::vector<const Layer *> ordered_layers =
        software_render_order(title, time);

    for (const Layer *layer : ordered_layers) {
        if (!layer || !layer_and_parents_visible(title, *layer, time) ||
            layer_type_is_audio(layer->type) ||
            layer_type_is_light(layer->type) || layer->type == LayerType::Empty)
            continue;

        const double resolved_time = resolved_layer_time(title, *layer, time);
        const double local_time = layer_local_time(*layer, resolved_time);
        if (layer->type == LayerType::Adjustment) {
            const QRectF canvas_bounds(0.0, 0.0, image.width(), image.height());
            for (const LayerEffect &effect : layer->effects) {
                const ResolvedLayerEffect resolved = resolve_layer_effect(
                    effect, local_time);
                apply_software_effect(image, resolved, canvas_bounds,
                                      canvas_bounds, time);
            }
            continue;
        }
        const QRectF bounds = software_layer_local_rect(*layer, local_time);
        if (!bounds.isValid() || bounds.isEmpty())
            continue;
        if (!fxm::transform3d::layer_passes_backface_culling(
                title, *layer, time))
            continue;
        QTransform local_to_canvas;
        if (!software_layer_canvas_transform(
                title, *layer, time, local_time, bounds, local_to_canvas))
            continue;

        const LayerTransitionVisualState transition =
            evaluate_layer_general_transitions(
                layer->transitions, layer->in_time, layer->out_time,
                resolved_time);
        const double opacity = std::clamp(
            layer->opacity.evaluate(local_time) * transition.opacity,
            0.0, 1.0);
        SoftwareLayerRaster raster = cached_software_layer_raster(
            *layer, bounds, local_time, time, raster_cache,
            transform_only_update);
        if (preview_scale < 0.999 && !raster.image.isNull()) {
            raster.image = raster.image.scaled(
                std::max(1, qRound(raster.image.width() * preview_scale)),
                std::max(1, qRound(raster.image.height() * preview_scale)),
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        raster.image = shade_software_image(
            raster.image, raster.local_bounds, title, *layer,
            time, local_time, lights);

        draw_software_3d_shadow(
            painter, title, *layer, bounds, time, local_time,
            local_to_canvas, lights, opacity, preview_scale);

        painter.save();
        painter.setOpacity(opacity);
        painter.setCompositionMode(software_blend_mode(layer->blend_mode));
        painter.setWorldTransform(local_to_canvas, true);
        painter.drawImage(raster.local_bounds, raster.image);
        painter.restore();
    }
    return image;
}

bool software_title_changes_with_time(const Title &title)
{
    if (fxm::asset_runtime::title_has_timeline_animation(title))
        return true;
    return std::any_of(title.layers.begin(), title.layers.end(),
        [](const std::shared_ptr<Layer> &layer) {
            return layer && (layer->type == LayerType::Clock ||
                             layer->type == LayerType::Ticker ||
                             layer->type == LayerType::Video);
        });
}

class SoftwareTitlePreviewRenderer final
    : public fxm::rendering::ITitlePreviewRenderer {
public:
    QImage render_title(const Title &title, double time,
                        std::uint64_t) override
    {
        return render_software_title(title, time);
    }
};

} // namespace

struct TitleGpuRenderSession {
    std::shared_ptr<Title> title;
    QImage frame;
    SoftwareLayerRasterCache raster_cache;
    double time = 0.0;
    double scale = 1.0;
    double rendered_scale = 0.0;
    TitleGpuRenderRequest render_request;
    std::uint64_t model_revision = 0;
    std::uint64_t update_serial = 0;
};

TitleGpuRenderSession *title_gpu_render_session_create()
{
    return new TitleGpuRenderSession;
}

void title_gpu_render_session_destroy(TitleGpuRenderSession *session)
{
    delete session;
}

void title_gpu_render_session_invalidate_presentation(
    TitleGpuRenderSession *session, bool discard_model)
{
    if (!session) return;
    session->frame = {};
    session->rendered_scale = 0.0;
    if (discard_model) {
        session->title.reset();
        session->raster_cache.clear();
    }
}

void title_gpu_render_session_update(
    TitleGpuRenderSession *session, const Title &title, double time,
    std::uint64_t model_revision, bool transform_only_update)
{
    if (!session) return;
    const bool same_title = session->title &&
        session->title->id == title.id;
    const bool same_model = same_title &&
        session->model_revision == model_revision;
    const bool same_quality =
        std::abs(session->rendered_scale - session->scale) < 0.000001;
    session->time = time;

    /* A static title does not acquire new pixels merely because the transport
     * requested another timestamp.  Retain the already prepared QImage just as
     * the OBS renderer retains its resident final texture. */
    if (same_model && same_quality && !session->frame.isNull() &&
        !software_title_changes_with_time(title)) {
        ++session->update_serial;
        return;
    }

    if (!same_title)
        session->raster_cache.clear();
    if (!same_model)
        session->title = std::make_shared<Title>(clone_title_snapshot(title));
    session->model_revision = model_revision;
    session->frame = render_software_title(
        title, time, &session->raster_cache, transform_only_update,
        session->scale);
    session->rendered_scale = session->scale;
    ++session->update_serial;
}

void title_gpu_render_session_update_range(
    TitleGpuRenderSession *session, const Title &title, double time,
    std::uint64_t model_revision, std::size_t, std::size_t, bool transform_only)
{
    title_gpu_render_session_update(session, title, time, model_revision,
                                    transform_only);
}

void title_gpu_render_session_set_preview_quality(
    TitleGpuRenderSession *session, double scale, bool)
{
    if (session) session->scale = std::clamp(scale, 0.05, 1.0);
}

void title_gpu_render_session_set_render_request(
    TitleGpuRenderSession *session, const TitleGpuRenderRequest &request)
{
    if (session)
        session->render_request = request;
}

void title_gpu_render_session_set_editor_video_decode_client(
    TitleGpuRenderSession *, bool) {}
void title_gpu_render_session_set_transition_input_preview(
    TitleGpuRenderSession *, bool) {}
void title_gpu_render_session_set_scene_mask_placeholder_preview(
    TitleGpuRenderSession *, bool) {}

bool title_gpu_render_session_submit_final_frame(
    TitleGpuRenderSession *session, const Title &title, const QImage &image,
    std::uint64_t revision)
{
    if (!session || image.isNull()) return false;
    session->title = std::make_shared<Title>(clone_title_snapshot(title));
    session->raster_cache.clear();
    session->frame = image;
    session->model_revision = revision;
    session->rendered_scale = 1.0;
    return true;
}

bool title_gpu_render_session_submit_cached_prefix(
    TitleGpuRenderSession *session, const Title &title, const QImage &image,
    double, std::size_t, std::uint64_t revision)
{
    return title_gpu_render_session_submit_final_frame(
        session, title, image, revision);
}

bool title_gpu_render_session_submit_gpu_cached_frame(
    TitleGpuRenderSession *, const Title &, const std::string &,
    std::uint64_t) { return false; }
bool title_gpu_render_session_submit_gpu_cached_prefix(
    TitleGpuRenderSession *, const Title &, const std::string &, double,
    std::size_t, std::uint64_t) { return false; }
std::string title_gpu_render_session_last_error(TitleGpuRenderSession *)
{ return {}; }
bool title_gpu_render_session_last_draw_deferred(TitleGpuRenderSession *)
{ return false; }
bool title_gpu_render_session_shader_compile_status(
    TitleGpuRenderSession *, TitleGpuShaderCompileStatus &) { return false; }
bool title_gpu_render_session_get_diagnostics(
    TitleGpuRenderSession *session, TitleGpuRenderDiagnostics &diagnostics)
{
    if (!session) return false;
    diagnostics.valid = true;
    diagnostics.session_time = session->time;
    diagnostics.last_published_time = session->time;
    diagnostics.update_serial = session->update_serial;
    diagnostics.model_revision = session->model_revision;
    diagnostics.published_model_revision = session->model_revision;
    diagnostics.has_published_frame = !session->frame.isNull();
    return true;
}
QImage title_gpu_render_session_readback(TitleGpuRenderSession *session)
{ return session ? session->frame : QImage(); }

namespace {
std::mutex g_cache_mutex;
std::unordered_map<std::string, QImage> g_frame_cache;
std::uint64_t g_cache_budget = 0;
}

bool title_gpu_frame_cache_contains(const std::string &key)
{
    std::lock_guard lock(g_cache_mutex);
    return g_frame_cache.find(key) != g_frame_cache.end();
}
bool title_gpu_frame_cache_alias(const std::string &key,
                                 const std::string &canonical)
{
    std::lock_guard lock(g_cache_mutex);
    const auto found = g_frame_cache.find(canonical);
    if (found == g_frame_cache.end()) return false;
    g_frame_cache[key] = found->second; return true;
}
bool title_gpu_frame_cache_store_image(
    const std::string &key, const QImage &image, std::uint32_t, std::uint32_t)
{ std::lock_guard lock(g_cache_mutex); g_frame_cache[key] = image; return true; }
void title_gpu_frame_cache_remove(const std::string &key)
{ std::lock_guard lock(g_cache_mutex); g_frame_cache.erase(key); }
void title_gpu_frame_cache_remove_title(const std::string &title_id)
{
    std::lock_guard lock(g_cache_mutex);
    for (auto it = g_frame_cache.begin(); it != g_frame_cache.end();) {
        it = it->first.find(title_id) != std::string::npos
            ? g_frame_cache.erase(it) : std::next(it);
    }
}
void title_gpu_frame_cache_clear()
{ std::lock_guard lock(g_cache_mutex); g_frame_cache.clear(); }
void title_gpu_frame_cache_set_budget(std::uint64_t bytes)
{ g_cache_budget = bytes; }
std::uint64_t title_gpu_frame_cache_bytes_used()
{
    std::lock_guard lock(g_cache_mutex);
    std::uint64_t bytes = 0;
    for (const auto &[key, image] : g_frame_cache)
        bytes += static_cast<std::uint64_t>(image.sizeInBytes());
    return bytes;
}

bool render_title_gpu_cache_submit_readback(
    const Title &, double, std::uint64_t, const std::string &,
    const QRect &, TitleGpuReadbackTicket &) { return false; }
bool title_gpu_render_session_resolve_readback(
    const TitleGpuReadbackTicket &, QImage &) { return false; }
void title_gpu_render_session_discard_readback(
    const TitleGpuReadbackTicket &) {}
void title_gpu_render_session_cancel_readback(
    const TitleGpuReadbackTicket &) {}

namespace fxm::editor {

rendering::ITitlePreviewRenderer &software_title_preview_renderer() noexcept
{
    static SoftwareTitlePreviewRenderer renderer;
    return renderer;
}

} // namespace fxm::editor
