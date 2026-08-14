#include "layer-transform-3d.h"
#include "asset-runtime.h"
#include "effects/effect-runtime.h"

#include <QPolygonF>
#include <QQuaternion>
#include <QtMath>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <unordered_set>
#include <vector>

namespace fxm::transform3d {
namespace {

constexpr double kEpsilon = 1.0e-8;

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
    const Layer *asset = find_layer(title, layer.asset_owner_id);
    if (!asset || asset->type != LayerType::Asset) {
        visiting.erase(layer.asset_owner_id);
        return title_time;
    }
    const double owner_time = resolved_layer_time_impl(
        title, *asset, title_time, visiting);
    const double resolved = fxm::asset_runtime::resolve_local_time(
        title.id, *asset, owner_time - asset->in_time);
    visiting.erase(layer.asset_owner_id);
    return resolved;
}

double resolved_layer_time(const Title &title, const Layer &layer,
                           double title_time)
{
    std::unordered_set<std::string> visiting;
    return resolved_layer_time_impl(title, layer, title_time, visiting);
}

double local_time(const Title &title, const Layer &layer, double title_time)
{
    const double resolved = resolved_layer_time(title, layer, title_time);
    return std::clamp(resolved - layer.in_time, 0.0,
                      std::max(0.0, layer.out_time - layer.in_time));
}

QMatrix4x4 parent_bind_matrix(const Layer &layer)
{
    QMatrix4x4 result;
    result.setToIdentity();
    if (!layer.parent_bind_enabled)
        return result;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result(row, column) = static_cast<float>(
                layer.parent_bind_matrix[static_cast<std::size_t>(row * 4 + column)]);
    return result;
}

bool parent_bind_uses_3d(const Layer &layer)
{
    if (!layer.parent_bind_enabled)
        return false;
    constexpr double epsilon = 1.0e-8;
    const auto &m = layer.parent_bind_matrix;
    return std::abs(m[2]) > epsilon || std::abs(m[6]) > epsilon ||
           std::abs(m[8]) > epsilon || std::abs(m[9]) > epsilon ||
           std::abs(m[11]) > epsilon || std::abs(m[14]) > epsilon ||
           std::abs(m[10] - 1.0) > epsilon ||
           std::abs(m[15] - 1.0) > epsilon;
}

QMatrix4x4 local_matrix(const Title &title, const Layer &layer, double title_time)
{
    const double resolved_time = resolved_layer_time(title, layer, title_time);
    const double t = std::clamp(resolved_time - layer.in_time, 0.0,
                                std::max(0.0, layer.out_time - layer.in_time));
    LayerVector3Value p = evaluated_layer_position_3d(layer, t);
    LayerVector3Value s = evaluated_layer_scale_3d(layer, t);
    LayerVector3Value rotation = evaluated_layer_rotation_3d(layer, t);
    LayerVector3Value orientation = evaluated_layer_orientation_3d(layer, t);
    LayerVector3Value anchor = evaluated_layer_anchor_3d(layer, t);
    if (layer.dimension_mode == LayerDimensionMode::TwoD) {
        /* A 2D child inherits the complete 3D parent basis, but its own local
         * transform remains a strict XY plane. Hidden/stale Z channels can
         * therefore never alter a layer merely because a parent became 3D. */
        p.z = 0.0;
        s.z = 1.0;
        rotation.x = rotation.y = 0.0;
        orientation = {};
        anchor.z = 0.0;
    }
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, resolved_time);

    QMatrix4x4 result;
    result.setToIdentity();
    result.translate(static_cast<float>(p.x + transition.translate_x),
                     static_cast<float>(p.y + transition.translate_y),
                     static_cast<float>(p.z));

    /* Orientation establishes the layer's base local axes. Rotation is then
     * applied inside that oriented basis in stable Z/Y/X Euler order. */
    result.rotate(static_cast<float>(orientation.z), 0.0f, 0.0f, 1.0f);
    result.rotate(static_cast<float>(orientation.y), 0.0f, 1.0f, 0.0f);
    result.rotate(static_cast<float>(orientation.x), 1.0f, 0.0f, 0.0f);
    result.rotate(static_cast<float>(rotation.z), 0.0f, 0.0f, 1.0f);
    result.rotate(static_cast<float>(rotation.y), 0.0f, 1.0f, 0.0f);
    result.rotate(static_cast<float>(rotation.x), 1.0f, 0.0f, 0.0f);
    result.scale(static_cast<float>(s.x * transition.scale),
                 static_cast<float>(s.y * transition.scale),
                 static_cast<float>(s.z * transition.scale));
    result.translate(0.0f, 0.0f, static_cast<float>(-anchor.z));
    return result;
}

std::vector<std::string> group_chain(const Title &title,
                                     const std::string &group_parent_id)
{
    std::vector<std::string> chain;
    std::string cursor = group_parent_id;
    std::unordered_set<std::string> visited;
    while (!cursor.empty() && visited.insert(cursor).second) {
        const Layer *group = find_layer(title, cursor);
        if (!group || !layer_type_is_container(group->type))
            break;
        chain.push_back(cursor);
        cursor = group->parent_id;
    }
    return chain;
}

std::vector<std::string> group_chain_for_layer(const Title &title,
                                               const Layer &layer)
{
    std::vector<std::string> chain;
    if (layer_type_is_container(layer.type))
        chain.push_back(layer.id);
    auto ancestors = group_chain(title, layer.parent_id);
    chain.insert(chain.end(), ancestors.begin(), ancestors.end());
    return chain;
}

std::string common_group(const Title &title,
                         const std::string &child_group_parent_id,
                         const Layer &transform_parent)
{
    const auto child_groups = group_chain(title, child_group_parent_id);
    const auto parent_groups = group_chain_for_layer(title, transform_parent);
    const std::set<std::string> parent_set(parent_groups.begin(), parent_groups.end());
    for (const auto &id : child_groups) {
        if (parent_set.find(id) != parent_set.end())
            return id;
    }
    return {};
}

QMatrix4x4 world_matrix_impl(const Title &title, const Layer &layer,
                             double title_time,
                             std::unordered_set<std::string> &visiting);

QMatrix4x4 parent_world_matrix_impl(
    const Title &title, const Layer &layer, double title_time,
    std::unordered_set<std::string> &visiting)
{
    QMatrix4x4 group_world;
    group_world.setToIdentity();
    if (const Layer *group = find_layer(title, layer.parent_id);
        group && layer_type_is_container(group->type)) {
        group_world = world_matrix_impl(title, *group, title_time, visiting);
    }

    QMatrix4x4 parent_basis = group_world;
    if (const Layer *transform_parent = find_layer(title, layer.transform_parent_id)) {
        const QMatrix4x4 transform_world = world_matrix_impl(
            title, *transform_parent, title_time, visiting);
        const std::string common_id = common_group(
            title, layer.parent_id, *transform_parent);
        if (common_id.empty()) {
            parent_basis = transform_world * group_world;
        } else if (const Layer *common = find_layer(title, common_id)) {
            std::unordered_set<std::string> common_visiting;
            bool invertible = false;
            const QMatrix4x4 common_inverse = world_matrix_impl(
                title, *common, title_time, common_visiting).inverted(&invertible);
            parent_basis = invertible
                ? transform_world * common_inverse * group_world
                : transform_world * group_world;
        }
    }
    return parent_basis * parent_bind_matrix(layer);
}

QMatrix4x4 world_matrix_impl(const Title &title, const Layer &layer,
                             double title_time,
                             std::unordered_set<std::string> &visiting)
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    if (!layer.id.empty() && !visiting.insert(layer.id).second)
        return identity;

    const QMatrix4x4 parent_basis = parent_world_matrix_impl(
        title, layer, title_time, visiting);
    const QMatrix4x4 world = parent_basis * local_matrix(title, layer, title_time);
    if (!layer.id.empty())
        visiting.erase(layer.id);
    return world;
}

QMatrix4x4 world_matrix(const Title &title, const Layer &layer,
                        double title_time)
{
    std::unordered_set<std::string> visiting;
    return world_matrix_impl(title, layer, title_time, visiting);
}

QMatrix4x4 parent_world_matrix(const Title &title, const Layer &layer,
                               double title_time)
{
    std::unordered_set<std::string> visiting;
    if (!layer.id.empty())
        visiting.insert(layer.id);
    return parent_world_matrix_impl(title, layer, title_time, visiting);
}

struct CameraMatrices {
    QMatrix4x4 view;
    QMatrix4x4 projection;
    QVector3D position;
    CameraProjection projection_mode = CameraProjection::Perspective;
    float vertical_fov = 50.0f;
    float aspect = 1.0f;
    float near_clip = 0.1f;
    float far_clip = 10000.0f;
    float ortho_half_width = 1.0f;
    float ortho_half_height = 1.0f;
};

CameraMatrices camera_matrices(const Title &title, const TitleCamera &camera,
                               double title_time, int canvas_width = 0,
                               int canvas_height = 0,
                               double coordinate_offset_x = 0.0,
                               double coordinate_offset_y = 0.0)
{
    CameraMatrices result;
    result.view.setToIdentity();
    result.projection.setToIdentity();

    const double w = std::max(1, canvas_width > 0 ? canvas_width : title.width);
    const double h = std::max(1, canvas_height > 0 ? canvas_height : title.height);
    const double t = std::max(0.0, title_time);
    const double authored_focal = std::max(1.0, camera.focal_length.evaluate(t));
    const double zoom = std::max(0.0001, camera.zoom.evaluate(t));
    const double focal = authored_focal * zoom;

    QVector3D position;
    QVector3D target;
    if (camera.use_canvas_default) {
        position = QVector3D(static_cast<float>(w * 0.5),
                             static_cast<float>(h * 0.5),
                             static_cast<float>(-focal));
        target = QVector3D(static_cast<float>(w * 0.5),
                           static_cast<float>(h * 0.5), 0.0f);
    } else {
        const Vec3Value position_value = evaluated_camera_position_3d(camera, t);
        const Vec3Value target_value = evaluated_camera_target_3d(camera, t);
        position = QVector3D(static_cast<float>(position_value.x),
                             static_cast<float>(position_value.y),
                             static_cast<float>(position_value.z));
        target = QVector3D(static_cast<float>(target_value.x),
                           static_cast<float>(target_value.y),
                           static_cast<float>(target_value.z));
    }
    position -= QVector3D(static_cast<float>(coordinate_offset_x),
                          static_cast<float>(coordinate_offset_y), 0.0f);
    target -= QVector3D(static_cast<float>(coordinate_offset_x),
                        static_cast<float>(coordinate_offset_y), 0.0f);

    QVector3D forward = target - position;
    if (forward.lengthSquared() < kEpsilon)
        forward = QVector3D(0.0f, 0.0f, 1.0f);
    forward.normalize();
    QVector3D up(0.0f, -1.0f, 0.0f); /* canvas Y points down */
    /* Top/bottom editor views look parallel to the canvas Y axis. A fixed -Y
     * up vector is singular there, so choose a stable Z-based up before applying
     * authored camera orientation. */
    if (std::abs(QVector3D::dotProduct(forward, up)) > 0.999f)
        up = QVector3D(0.0f, 0.0f, -1.0f);
    /* Keep the Development Version 206 rotation path literally intact. */
    const QQuaternion orientation = QQuaternion::fromEulerAngles(
        static_cast<float>(camera.rotation_x.evaluate(t)),
        static_cast<float>(camera.rotation_y.evaluate(t)),
        static_cast<float>(camera.rotation_z.evaluate(t)));
    forward = orientation.rotatedVector(forward);
    up = orientation.rotatedVector(up);

    /* Camera Orientation is a new, independent axis basis. It is appended only
     * when authored; an untouched camera executes only the original 206 code
     * above and therefore cannot change ordinary layer-drag presentation. */
    const double orientation_x = camera.orientation_x.evaluate(t);
    const double orientation_y = camera.orientation_y.evaluate(t);
    const double orientation_z = camera.orientation_z.evaluate(t);
    const bool has_axis_orientation = camera.orientation_x.is_animated() ||
        camera.orientation_y.is_animated() || camera.orientation_z.is_animated() ||
        std::abs(orientation_x) > kEpsilon ||
        std::abs(orientation_y) > kEpsilon ||
        std::abs(orientation_z) > kEpsilon;
    if (has_axis_orientation) {
        const QQuaternion axis_orientation = QQuaternion::fromEulerAngles(
            static_cast<float>(orientation_x),
            static_cast<float>(orientation_y),
            static_cast<float>(orientation_z));
        forward = axis_orientation.rotatedVector(forward);
        up = axis_orientation.rotatedVector(up);
    }
    result.view.lookAt(position, position + forward, up);
    result.position = position;

    const float near_plane = static_cast<float>(std::max(
        0.0001, camera.near_clip.evaluate(t)));
    const float far_plane = static_cast<float>(std::max(
        static_cast<double>(near_plane) + 0.001,
        camera.far_clip.evaluate(t)));
    /* The original static enum remains the default. Only an authored
     * Projection track can replace it. */
    result.projection_mode = camera.projection;
    if (camera.projection_mode.is_animated())
        result.projection_mode = evaluated_camera_projection(camera, t);
    result.aspect = static_cast<float>(w / h);
    result.near_clip = near_plane;
    result.far_clip = far_plane;
    if (result.projection_mode == CameraProjection::Orthographic) {
        const float half_w = static_cast<float>(w / (2.0 * zoom));
        const float half_h = static_cast<float>(h / (2.0 * zoom));
        result.ortho_half_width = half_w;
        result.ortho_half_height = half_h;
        result.projection.ortho(-half_w, half_w, -half_h, half_h,
                                near_plane, far_plane);
    } else {
        const double exact_fov = qRadiansToDegrees(
            2.0 * std::atan(h / (2.0 * focal)));
        const double requested_fov = camera.field_of_view.evaluate(t);
        const double fov = camera.use_canvas_default
            ? exact_fov
            : std::clamp(requested_fov > 0.0 ? requested_fov : exact_fov,
                         0.1, 179.0);
        result.vertical_fov = static_cast<float>(fov);
        result.projection.perspective(result.vertical_fov,
                                      result.aspect,
                                      near_plane, far_plane);
    }
    return result;
}

bool project_point(const QMatrix4x4 &view_projection,
                   const QVector3D &point, int width, int height,
                   QPointF &screen, double *camera_depth = nullptr)
{
    const QVector4D clip = view_projection * QVector4D(point, 1.0f);
    /* A planar homography cannot represent geometry crossing the camera plane.
     * Treat those frames as clipped instead of falling back to legacy 2D. */
    if (clip.w() <= kEpsilon)
        return false;
    const double inv_w = 1.0 / clip.w();
    const double x = clip.x() * inv_w;
    const double y = clip.y() * inv_w;
    const double z = clip.z() * inv_w;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    screen = QPointF((x + 1.0) * 0.5 * width,
                     (1.0 - y) * 0.5 * height);
    if (camera_depth)
        *camera_depth = z;
    return true;
}

bool scalar_animated(const AnimatedProperty &property)
{
    return property.is_animated();
}

bool camera_animated(const TitleCamera &camera)
{
    return (camera.position_3d_path_enabled && camera.position_3d.is_animated()) ||
           (camera.target_3d_path_enabled && camera.target_3d.is_animated()) ||
           scalar_animated(camera.position_x) ||
           scalar_animated(camera.position_y) ||
           scalar_animated(camera.position_z) ||
           scalar_animated(camera.target_x) ||
           scalar_animated(camera.target_y) ||
           scalar_animated(camera.target_z) ||
           scalar_animated(camera.orientation_x) ||
           scalar_animated(camera.orientation_y) ||
           scalar_animated(camera.orientation_z) ||
           scalar_animated(camera.rotation_x) ||
           scalar_animated(camera.rotation_y) ||
           scalar_animated(camera.rotation_z) ||
           scalar_animated(camera.focal_length) ||
           scalar_animated(camera.field_of_view) ||
           scalar_animated(camera.zoom) ||
           scalar_animated(camera.near_clip) ||
           scalar_animated(camera.far_clip) ||
           scalar_animated(camera.projection_mode);
}

const Layer *isolated_asset_owner(const Title &title, const Layer &layer)
{
    if (layer.asset_owner_id.empty())
        return nullptr;
    const Layer *owner = find_layer(title, layer.asset_owner_id);
    return owner && owner->type == LayerType::Asset &&
                   owner->asset_isolated_3d_space
        ? owner : nullptr;
}

bool evaluated_effect_enabled_3d(const LayerEffect &effect, double local_time)
{
    return effect.enabled_prop.is_animated()
        ? effect.enabled_prop.evaluate(local_time) >= 0.5
        : effect.enabled;
}

bool effect_stack_has_active_space(const Layer &layer, double local_time,
                                   LayerEffectSpace space)
{
    return std::any_of(layer.effects.begin(), layer.effects.end(),
        [&](const LayerEffect &effect) {
            return evaluated_effect_enabled_3d(effect, local_time) &&
                   effect_execution_space(effect) == space;
        });
}

bool effect_stack_has_active_non_motion_blur_space(
    const Layer &layer, double local_time, LayerEffectSpace space)
{
    return std::any_of(layer.effects.begin(), layer.effects.end(),
        [&](const LayerEffect &effect) {
            return effect.type != LayerEffectType::MotionBlur &&
                   evaluated_effect_enabled_3d(effect, local_time) &&
                   effect_execution_space(effect) == space;
        });
}

bool simple_planar_3d_candidate(const Title &title, const Layer &layer,
                                double title_time)
{
    if (!layer_supports_3d(layer) ||
        !layer_or_ancestor_uses_3d(title, layer) ||
        layer.depth_mode != LayerDepthMode::Automatic ||
        layer.type == LayerType::Adjustment ||
        layer.type == LayerType::Empty ||
        layer.type == LayerType::TransitionInput ||
        layer_type_is_container(layer.type))
        return false;
    const double t = local_time(title, layer, title_time);
    const double resolved_time = resolved_layer_time(title, layer, title_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, resolved_time);
    const bool post_transform_effect = effect_stack_has_active_space(
        layer, t, LayerEffectSpace::PostTransform);
    const bool screen_space_effect = effect_stack_has_active_space(
        layer, t, LayerEffectSpace::ScreenSpace);
    const bool effects_after_projected_mask =
        layer.effect_stack_respects_masks &&
        layer.mask_mode != MaskMode::None &&
        std::any_of(layer.effects.begin(), layer.effects.end(),
            [&](const LayerEffect &effect) {
                return evaluated_effect_enabled_3d(effect, t);
            });
    return !post_transform_effect && !screen_space_effect &&
           !effects_after_projected_mask &&
           layer.opacity.evaluate(t) > 0.000001 &&
           transition.opacity > 0.000001 &&
           transition.wipe >= 0.999999 && transition.blur <= 0.000001;
}

bool simple_opaque_3d_candidate(const Title &title, const Layer &layer,
                                double title_time)
{
    if (!simple_planar_3d_candidate(title, layer, title_time))
        return false;
    const double t = local_time(title, layer, title_time);
    const double resolved_time = resolved_layer_time(title, layer, title_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, resolved_time);
    return layer.opacity.evaluate(t) >= 0.999 && transition.opacity >= 0.999;
}

bool simple_transparent_3d_candidate(const Title &title, const Layer &layer,
                                     double title_time)
{
    return simple_planar_3d_candidate(title, layer, title_time) &&
           !simple_opaque_3d_candidate(title, layer, title_time);
}

bool depth_sort_candidate(const Title &title, const Layer &layer,
                          double title_time)
{
    /* Non-Normal blends must read the destination color and therefore remain
     * in the full-frame ping/pong compositor. Track mattes and effects that run
     * after a projected mask can also require a full-canvas intermediate. They
     * are still planar 3D surfaces, so order contiguous fallback surfaces by
     * camera depth instead of reverting to the raw layer-list order. Motion
     * Blur is rendered through a full-frame temporal intermediate, but that
     * must not change the owning plane's camera-depth order. Other active
     * post-transform and screen-space effects remain authored-order barriers. */
    if (!layer.depth_test || !layer_supports_3d(layer) ||
        !layer_or_ancestor_uses_3d(title, layer) ||
        layer.depth_mode != LayerDepthMode::Automatic ||
        layer.type == LayerType::Adjustment ||
        layer.type == LayerType::Empty ||
        layer.type == LayerType::TransitionInput ||
        layer_type_is_container(layer.type))
        return false;
    const double t = local_time(title, layer, title_time);
    return !effect_stack_has_active_non_motion_blur_space(
               layer, t, LayerEffectSpace::PostTransform) &&
           !effect_stack_has_active_space(
               layer, t, LayerEffectSpace::ScreenSpace) &&
           layer.opacity.evaluate(t) > 0.000001;
}

std::string effective_camera_id(const Title &title, const Layer &layer,
                                double title_time)
{
    if (!isolated_asset_owner(title, layer) &&
        !title.render_camera_override_id.empty())
        return title.render_camera_override_id;
    const std::string assigned = resolved_layer_camera_id(title, layer, title_time);
    return assigned.empty() ? resolved_active_camera_id(title, title_time) : assigned;
}

template<typename Item, typename LayerAccessor>
void sort_candidate_runs(std::vector<Item> &items, const Title &title,
                         double title_time, LayerAccessor layer_for)
{
    struct CameraDepthSet {
        std::vector<std::size_t> positions;
        std::vector<Item> candidates;
    };
    std::map<std::string, CameraDepthSet> by_camera;
    for (std::size_t slot = 0; slot < items.size(); ++slot) {
        const Layer *layer = layer_for(items[slot]);
        if (!layer || !depth_sort_candidate(title, *layer, title_time))
            continue;
        CameraDepthSet &set = by_camera[
            effective_camera_id(title, *layer, title_time)];
        set.positions.push_back(slot);
        set.candidates.push_back(items[slot]);
    }

    for (auto &[camera_id, set] : by_camera) {
        (void)camera_id;
        std::stable_sort(set.candidates.begin(), set.candidates.end(),
                         [&](const Item &a, const Item &b) {
            const Layer *left = layer_for(a);
            const Layer *right = layer_for(b);
            if (!left || !right)
                return false;
            const double left_depth =
                evaluate(title, *left, title_time).camera_depth;
            const double right_depth =
                evaluate(title, *right, title_time).camera_depth;
            /* Automatic depth is a world/camera-space contract. Every plane
             * assigned to the same camera participates, regardless of where
             * non-3D, audio, light or empty rows happen to sit in the layer
             * list. NDC increases near-to-far; painter compositing draws the
             * farthest surface first. Authored order is only the tie-break. */
            return left_depth > right_depth;
        });
        for (std::size_t index = 0; index < set.positions.size(); ++index)
            items[set.positions[index]] = std::move(set.candidates[index]);
    }
}

} // namespace

bool layer_supports_3d(const Layer &layer)
{
    return !layer_type_is_audio(layer.type) &&
           layer.type != LayerType::Adjustment;
}

bool layer_or_ancestor_uses_3d(const Title &title, const Layer &layer)
{
    if (layer.dimension_mode == LayerDimensionMode::ThreeD ||
        parent_bind_uses_3d(layer))
        return true;
    std::vector<std::string> pending;
    if (!layer.parent_id.empty())
        pending.push_back(layer.parent_id);
    if (!layer.transform_parent_id.empty())
        pending.push_back(layer.transform_parent_id);
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        const std::string id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second)
            continue;
        const Layer *parent = find_layer(title, id);
        if (!parent)
            continue;
        if (parent->dimension_mode == LayerDimensionMode::ThreeD ||
            parent_bind_uses_3d(*parent))
            return true;
        if (!parent->parent_id.empty())
            pending.push_back(parent->parent_id);
        if (!parent->transform_parent_id.empty())
            pending.push_back(parent->transform_parent_id);
    }
    return false;
}

bool title_camera_has_animation(const Title &title)
{
    if (title.active_camera.is_animated())
        return true;
    if (std::any_of(title.cameras.begin(), title.cameras.end(), camera_animated))
        return true;
    return std::any_of(title.layers.begin(), title.layers.end(),
        [](const std::shared_ptr<Layer> &layer) {
            return layer && layer->camera_assignment.is_animated();
        });
}

bool layer_passes_backface_culling(const Title &title, const Layer &layer,
                                   double title_time)
{
    /* Double-sided and disabled-culling surfaces remain visible from either
     * side for every planar artwork type, including Text/Clock/Ticker. */
    if ((!layer.backface_culling || layer.double_sided) ||
        !layer_or_ancestor_uses_3d(title, layer))
        return true;
    const EvaluatedTransform transform = evaluate(title, layer, title_time);
    return !transform.projectable || transform.front_facing;
}

bool depth_compositor_surface_candidate(const Title &title, const Layer &layer,
                                        double title_time)
{
    return simple_planar_3d_candidate(title, layer, title_time);
}

bool hardware_depth_candidate(const Title &title, const Layer &layer,
                              double title_time)
{
    /* Custom blend modes remain on the ordered full-frame compositor because
     * they must read the destination color. Normal-blend layers can enter the
     * hardware Z pass even when they have a projected track matte or a padded
     * layer-space effect stack. */
    /* Isolated Asset Layers use a two-stage projection: authored 3D first,
     * host placement second. OBS' single model-view hardware-depth pass cannot
     * represent that composition boundary, so these planes use the ordered
     * projected compositor within the asset instead. */
    return !isolated_asset_owner(title, layer) &&
           layer.blend_mode == EffectBlendMode::Normal &&
           (layer.depth_test || layer.write_to_depth) &&
           depth_compositor_surface_candidate(title, layer, title_time);
}

bool hardware_depth_writer(const Title &title, const Layer &layer,
                           double title_time)
{
    return layer.write_to_depth &&
           hardware_depth_candidate(title, layer, title_time);
}

bool hardware_depth_read_only(const Title &title, const Layer &layer,
                              double title_time)
{
    return layer.depth_test && !layer.write_to_depth &&
           hardware_depth_candidate(title, layer, title_time);
}

bool hardware_depth_transparent_candidate(const Title &title,
                                          const Layer &layer,
                                          double title_time)
{
    return hardware_depth_candidate(title, layer, title_time) &&
           simple_transparent_3d_candidate(title, layer, title_time);
}

std::vector<std::size_t> ordered_root_layer_indices(
    const Title &title, std::size_t first, std::size_t last, double title_time)
{
    first = std::min(first, title.layers.size());
    last = std::max(first, std::min(last, title.layers.size()));
    std::vector<std::size_t> result;
    result.reserve(last - first);
    for (std::size_t index = first; index < last; ++index) {
        const auto &layer = title.layers[index];
        if (!layer || !layer->parent_id.empty())
            continue;
        result.push_back(index);
    }
    sort_candidate_runs(result, title, title_time, [&](std::size_t index) {
        return index < title.layers.size() ? title.layers[index].get() : nullptr;
    });
    return result;
}

std::vector<const Layer *> ordered_group_children(
    const Title &title, const std::string &group_id, double title_time)
{
    std::vector<const Layer *> result;
    for (const auto &layer : title.layers) {
        if (layer && layer->parent_id == group_id)
            result.push_back(layer.get());
    }
    sort_candidate_runs(result, title, title_time,
                        [](const Layer *layer) { return layer; });
    return result;
}

std::string resolved_active_camera_id(const Title &title, double title_time)
{
    /* Static titles deliberately use the original mirror directly. This keeps
     * the Development Version 206 camera selection path byte-for-byte intact. */
    if (!title.active_camera.is_animated())
        return title.active_camera_id;
    const std::string evaluated = title.active_camera.evaluate(
        std::max(0.0, title_time));
    return evaluated.empty() ? title.active_camera_id : evaluated;
}

std::string resolved_layer_camera_id(const Title &title, const Layer &layer,
                                     double title_time)
{
    if (!layer.camera_assignment.is_animated())
        return layer.camera_id;
    const double t = layer.asset_camera_uses_owner_time
        ? resolved_layer_time(title, layer, title_time)
        : local_time(title, layer, title_time);
    const std::string evaluated = layer.camera_assignment.evaluate(t);
    return evaluated.empty() ? layer.camera_id : evaluated;
}

CameraProjection evaluated_camera_projection(const TitleCamera &camera,
                                              double title_time)
{
    /* The legacy enum remains authoritative until the user creates a
     * Projection keyframe. Merely opening the new camera timeline must not
     * alter any existing render matrix. */
    if (!camera.projection_mode.is_animated())
        return camera.projection;
    const int value = std::clamp(
        static_cast<int>(std::llround(camera.projection_mode.evaluate(
            std::max(0.0, title_time)))),
        static_cast<int>(CameraProjection::Perspective),
        static_cast<int>(CameraProjection::Orthographic));
    return static_cast<CameraProjection>(value);
}

const TitleCamera *active_camera(const Title &title,
                                 const std::string &camera_id,
                                 double title_time)
{
    const std::string requested = camera_id.empty()
        ? resolved_active_camera_id(title, title_time) : camera_id;
    for (const auto &camera : title.cameras) {
        if (camera.id == requested)
            return &camera;
    }
    return title.cameras.empty() ? nullptr : &title.cameras.front();
}

bool camera_render_state(const Title &title, double title_time,
                         CameraRenderState &state,
                         const std::string &camera_id)
{
    TitleCamera fallback;
    const std::string requested = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id : camera_id;
    const TitleCamera *camera = active_camera(title, requested, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(title, *camera, title_time);
    state.view = matrices.view;
    state.projection_matrix = matrices.projection;
    state.projection = matrices.projection_mode;
    state.vertical_fov = matrices.vertical_fov;
    state.aspect = matrices.aspect;
    state.near_clip = matrices.near_clip;
    state.far_clip = matrices.far_clip;
    state.ortho_half_width = matrices.ortho_half_width;
    state.ortho_half_height = matrices.ortho_half_height;
    return true;
}

namespace {

struct LayerProjectionPoint {
    QPointF canvas;
    QVector3D camera_position;
    double camera_depth = 0.0;
};

const TitleCamera *asset_space_camera(const Title &title,
                                      const Layer &asset_owner,
                                      const std::string &camera_id)
{
    if (!camera_id.empty()) {
        for (const TitleCamera &camera : title.cameras) {
            /* Duplicated Asset Layers may intentionally share the same hidden
             * immutable camera snapshot. The explicit assignment is
             * authoritative; the owner tag supplies fallback lookup and UI
             * hiding, not camera identity. */
            if (camera.id == camera_id &&
                !camera.asset_space_owner_id.empty())
                return &camera;
        }
    }
    for (const TitleCamera &camera : title.cameras) {
        if (camera.asset_space_owner_id == asset_owner.id)
            return &camera;
    }
    return nullptr;
}

bool project_layer_local_point_impl(
    const Title &title, const Layer &layer, double title_time,
    const QVector3D &local_point, LayerProjectionPoint &result,
    std::unordered_set<std::string> &visiting_assets)
{
    if (const Layer *asset_owner = isolated_asset_owner(title, layer)) {
        if (!visiting_assets.insert(asset_owner->id).second)
            return false;

        bool invertible = false;
        const QMatrix4x4 owner_inverse = world_matrix(
            title, *asset_owner, title_time).inverted(&invertible);
        if (!invertible) {
            visiting_assets.erase(asset_owner->id);
            return false;
        }
        const QMatrix4x4 relative_world = owner_inverse *
            world_matrix(title, layer, title_time);
        const double camera_time = resolved_layer_time(title, layer, title_time);
        const std::string camera_id = resolved_layer_camera_id(
            title, layer, title_time);
        TitleCamera fallback;
        const TitleCamera *camera = asset_space_camera(
            title, *asset_owner, camera_id);
        if (!camera)
            camera = &fallback;
        const int source_width = std::max(1, asset_owner->asset_space_width);
        const int source_height = std::max(1, asset_owner->asset_space_height);
        const CameraMatrices matrices = camera_matrices(
            title, *camera, camera_time, source_width, source_height,
            asset_owner->asset_space_center_x,
            asset_owner->asset_space_center_y);
        const QVector3D asset_world_point = relative_world.map(local_point);
        QPointF source_point;
        double source_depth = 0.0;
        if (!project_point(matrices.projection * matrices.view,
                           asset_world_point, source_width, source_height,
                           source_point, &source_depth)) {
            visiting_assets.erase(asset_owner->id);
            return false;
        }

        const QVector3D owner_local_point(
            static_cast<float>(source_point.x() -
                               asset_owner->asset_space_center_x),
            static_cast<float>(source_point.y() -
                               asset_owner->asset_space_center_y),
            0.0f);
        LayerProjectionPoint placed;
        const bool projected = project_layer_local_point_impl(
            title, *asset_owner, title_time, owner_local_point,
            placed, visiting_assets);
        visiting_assets.erase(asset_owner->id);
        if (!projected)
            return false;
        result = placed;
        /* Sibling order inside this asset is determined exclusively by its
         * authored camera, not by the host title camera or placement. */
        result.camera_depth = source_depth;
        result.camera_position = matrices.position;
        return true;
    }

    const QMatrix4x4 world = world_matrix(title, layer, title_time);
    const QVector3D world_point = world.map(local_point);
    if (!layer_or_ancestor_uses_3d(title, layer)) {
        result.canvas = QPointF(world_point.x(), world_point.y());
        result.camera_depth = world_point.z();
        result.camera_position = QVector3D();
        return std::isfinite(result.canvas.x()) &&
               std::isfinite(result.canvas.y());
    }

    TitleCamera fallback;
    const std::string requested = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id
        : resolved_layer_camera_id(title, layer, title_time);
    const TitleCamera *camera = active_camera(
        title, requested, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(
        title, *camera, title_time);
    result.camera_position = matrices.position;
    return project_point(matrices.projection * matrices.view, world_point,
                         title.width, title.height, result.canvas,
                         &result.camera_depth);
}

bool project_layer_local_point(const Title &title, const Layer &layer,
                               double title_time,
                               const QVector3D &local_point,
                               LayerProjectionPoint &result)
{
    std::unordered_set<std::string> visiting_assets;
    return project_layer_local_point_impl(title, layer, title_time,
                                          local_point, result,
                                          visiting_assets);
}

} // namespace

EvaluatedTransform evaluate(const Title &title, const Layer &layer,
                            double title_time)
{
    EvaluatedTransform result;
    result.world.setToIdentity();
    result.inherited_3d = layer_or_ancestor_uses_3d(title, layer);
    if (!result.inherited_3d || !layer_supports_3d(layer))
        return result;

    if (isolated_asset_owner(title, layer)) {
        const std::array<QVector3D, 4> local_points = {
            QVector3D(0.0f, 0.0f, 0.0f),
            QVector3D(1.0f, 0.0f, 0.0f),
            QVector3D(1.0f, 1.0f, 0.0f),
            QVector3D(0.0f, 1.0f, 0.0f)};
        std::array<LayerProjectionPoint, 4> projected_points;
        for (std::size_t index = 0; index < local_points.size(); ++index) {
            if (!project_layer_local_point(title, layer, title_time,
                                           local_points[index],
                                           projected_points[index]))
                return result;
        }
        const bool entirely_before_near = std::all_of(
            projected_points.begin(), projected_points.end(),
            [](const LayerProjectionPoint &point) {
                return point.camera_depth < -1.0;
            });
        const bool entirely_beyond_far = std::all_of(
            projected_points.begin(), projected_points.end(),
            [](const LayerProjectionPoint &point) {
                return point.camera_depth > 1.0;
            });
        if (entirely_before_near || entirely_beyond_far)
            return result;

        QPolygonF source;
        source << QPointF(0.0, 0.0) << QPointF(1.0, 0.0)
               << QPointF(1.0, 1.0) << QPointF(0.0, 1.0);
        QPolygonF destination;
        for (const LayerProjectionPoint &point : projected_points)
            destination << point.canvas;
        result.projectable = QTransform::quadToQuad(
            source, destination, result.projected);
        result.world = world_matrix(title, layer, title_time);
        if (const Layer *owner = isolated_asset_owner(title, layer)) {
            result.camera_position = world_matrix(
                title, *owner, title_time).map(
                    projected_points.front().camera_position);
        } else {
            result.camera_position = projected_points.front().camera_position;
        }
        result.camera_depth = std::accumulate(
            projected_points.begin(), projected_points.end(), 0.0,
            [](double total, const LayerProjectionPoint &point) {
                return total + point.camera_depth;
            }) * 0.25;
        const QPointF &q00 = projected_points[0].canvas;
        const QPointF &q10 = projected_points[1].canvas;
        const QPointF &q01 = projected_points[3].canvas;
        result.projected_winding =
            (q10.x() - q00.x()) * (q01.y() - q00.y()) -
            (q10.y() - q00.y()) * (q01.x() - q00.x());
        result.front_facing = result.projected_winding >= -kEpsilon;
        const QVector3D p00 = result.world.map(
            QVector3D(0.0f, 0.0f, 0.0f));
        const QVector3D p10 = result.world.map(
            QVector3D(1.0f, 0.0f, 0.0f));
        const QVector3D p01 = result.world.map(
            QVector3D(0.0f, 1.0f, 0.0f));
        QVector3D normal = QVector3D::crossProduct(p01 - p00, p10 - p00);
        if (normal.lengthSquared() > kEpsilon)
            normal.normalize();
        else
            normal = QVector3D(0.0f, 0.0f, -1.0f);
        result.world_normal = normal;
        return result;
    }

    result.world = world_matrix(title, layer, title_time);
    TitleCamera fallback;
    const std::string camera_id = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id
        : resolved_layer_camera_id(title, layer, title_time);
    const TitleCamera *camera = active_camera(title, camera_id, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(title, *camera, title_time);
    result.camera_position = matrices.position;
    const QMatrix4x4 view_projection = matrices.projection * matrices.view;

    const QVector3D p00 = result.world.map(QVector3D(0.0f, 0.0f, 0.0f));
    const QVector3D p10 = result.world.map(QVector3D(1.0f, 0.0f, 0.0f));
    const QVector3D p11 = result.world.map(QVector3D(1.0f, 1.0f, 0.0f));
    const QVector3D p01 = result.world.map(QVector3D(0.0f, 1.0f, 0.0f));

    QPolygonF source;
    source << QPointF(0.0, 0.0) << QPointF(1.0, 0.0)
           << QPointF(1.0, 1.0) << QPointF(0.0, 1.0);
    QPolygonF destination;
    QPointF q00, q10, q11, q01;
    double depth00 = 0.0, depth10 = 0.0, depth11 = 0.0, depth01 = 0.0;
    if (!project_point(view_projection, p00, title.width, title.height, q00, &depth00) ||
        !project_point(view_projection, p10, title.width, title.height, q10, &depth10) ||
        !project_point(view_projection, p11, title.width, title.height, q11, &depth11) ||
        !project_point(view_projection, p01, title.width, title.height, q01, &depth01))
        return result;
    const bool entirely_before_near = depth00 < -1.0 && depth10 < -1.0 &&
                                      depth11 < -1.0 && depth01 < -1.0;
    const bool entirely_beyond_far = depth00 > 1.0 && depth10 > 1.0 &&
                                     depth11 > 1.0 && depth01 > 1.0;
    if (entirely_before_near || entirely_beyond_far)
        return result;
    destination << q00 << q10 << q11 << q01;
    result.projectable = QTransform::quadToQuad(source, destination,
                                                result.projected);
    result.camera_depth = (depth00 + depth10 + depth11 + depth01) * 0.25;

    // The screen-space winding is the authoritative culling convention. It is
    // identical for perspective and orthographic cameras and naturally flips
    // for negative X/Y scale in either the layer or any 3D parent. Canvas Y
    // points down, so an unmodified front-facing plane has positive winding.
    result.projected_winding =
        (q10.x() - q00.x()) * (q01.y() - q00.y()) -
        (q10.y() - q00.y()) * (q01.x() - q00.x());
    result.front_facing = result.projected_winding >= -kEpsilon;

    // Keep a world-space normal for editor diagnostics. The chosen local
    // winding gives the default plane a -Z normal, toward the default camera.
    QVector3D normal = QVector3D::crossProduct(p01 - p00, p10 - p00);
    if (normal.lengthSquared() > kEpsilon)
        normal.normalize();
    else
        normal = QVector3D(0.0f, 0.0f, -1.0f);
    result.world_normal = normal;
    return result;
}

QMatrix4x4 layer_world_matrix(const Title &title, const Layer &layer,
                              double title_time)
{
    return world_matrix(title, layer, title_time);
}

QMatrix4x4 layer_parent_world_matrix(const Title &title, const Layer &layer,
                                     double title_time)
{
    return parent_world_matrix(title, layer, title_time);
}

bool project_world_point(const Title &title, const QVector3D &world_point,
                         double title_time, QPointF &canvas_point,
                         double *camera_depth, const std::string &camera_id)
{
    TitleCamera fallback;
    const std::string requested = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id
        : camera_id;
    const TitleCamera *camera = active_camera(title, requested, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(title, *camera, title_time);
    return project_point(matrices.projection * matrices.view, world_point,
                         title.width, title.height, canvas_point, camera_depth);
}


bool canvas_world_ray(const Title &title, const QPointF &canvas_point,
                      double title_time, QVector3D &ray_origin,
                      QVector3D &ray_direction, const std::string &camera_id)
{
    CameraRenderState matrices;
    if (!camera_render_state(title, title_time, matrices, camera_id))
        return false;
    const int width = std::max(1, title.width);
    const int height = std::max(1, title.height);
    const float ndc_x = static_cast<float>(2.0 * canvas_point.x() / width - 1.0);
    const float ndc_y = static_cast<float>(1.0 - 2.0 * canvas_point.y() / height);
    bool invertible = false;
    const QMatrix4x4 inverse =
        (matrices.projection_matrix * matrices.view).inverted(&invertible);
    if (!invertible) return false;

    QVector4D near_h = inverse * QVector4D(ndc_x, ndc_y, -1.0f, 1.0f);
    QVector4D far_h = inverse * QVector4D(ndc_x, ndc_y, 1.0f, 1.0f);
    if (std::abs(near_h.w()) <= kEpsilon || std::abs(far_h.w()) <= kEpsilon)
        return false;
    near_h /= near_h.w();
    far_h /= far_h.w();
    ray_origin = near_h.toVector3D();
    ray_direction = far_h.toVector3D() - ray_origin;
    if (ray_direction.lengthSquared() <= kEpsilon) return false;
    ray_direction.normalize();
    return true;
}

bool canvas_point_on_world_plane(const Title &title, const QPointF &canvas_point,
                                 double title_time,
                                 const QVector3D &plane_point,
                                 const QVector3D &plane_normal,
                                 QVector3D &world_point,
                                 const std::string &camera_id)
{
    QVector3D origin, direction;
    if (!canvas_world_ray(title, canvas_point, title_time,
                          origin, direction, camera_id))
        return false;
    QVector3D normal = plane_normal;
    if (normal.lengthSquared() <= kEpsilon) return false;
    normal.normalize();
    const float denominator = QVector3D::dotProduct(direction, normal);
    if (std::abs(denominator) <= 1.0e-7f) return false;
    const float distance = QVector3D::dotProduct(plane_point - origin, normal) /
                           denominator;
    if (!std::isfinite(distance)) return false;
    world_point = origin + direction * distance;
    return std::isfinite(world_point.x()) && std::isfinite(world_point.y()) &&
           std::isfinite(world_point.z());
}

bool layer_local_clip_point(const Title &title, const Layer &layer,
                            double title_time, const QVector3D &local_point,
                            QVector4D &clip_point,
                            const std::string &camera_id)
{
    if (!layer_supports_3d(layer) ||
        !layer_or_ancestor_uses_3d(title, layer))
        return false;
    if (isolated_asset_owner(title, layer)) {
        LayerProjectionPoint projected;
        if (!project_layer_local_point(title, layer, title_time,
                                       local_point, projected))
            return false;
        const double width = std::max(1, title.width);
        const double height = std::max(1, title.height);
        clip_point = QVector4D(
            static_cast<float>(2.0 * projected.canvas.x() / width - 1.0),
            static_cast<float>(1.0 - 2.0 * projected.canvas.y() / height),
            static_cast<float>(projected.camera_depth), 1.0f);
        return std::isfinite(clip_point.x()) &&
               std::isfinite(clip_point.y()) &&
               std::isfinite(clip_point.z());
    }
    TitleCamera fallback;
    const std::string requested = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id
        : (!camera_id.empty() ? camera_id
                              : resolved_layer_camera_id(title, layer, title_time));
    const TitleCamera *camera = active_camera(title, requested, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(title, *camera, title_time);
    clip_point = matrices.projection * matrices.view *
        world_matrix(title, layer, title_time) * QVector4D(local_point, 1.0f);
    return clip_point.w() > kEpsilon &&
           std::isfinite(clip_point.x()) && std::isfinite(clip_point.y()) &&
           std::isfinite(clip_point.z()) && std::isfinite(clip_point.w());
}

bool projected_local_quad_transform(
    const Title &title, const Layer &layer, double title_time,
    const QPolygonF &source_quad, const QPolygonF &local_quad,
    QTransform &source_to_canvas, const std::string &camera_id)
{
    source_to_canvas = QTransform();
    if (source_quad.size() != 4 || local_quad.size() != 4 ||
        !layer_supports_3d(layer) ||
        !layer_or_ancestor_uses_3d(title, layer))
        return false;

    if (isolated_asset_owner(title, layer)) {
        QPolygonF canvas_quad;
        canvas_quad.reserve(4);
        for (const QPointF &point : local_quad) {
            LayerProjectionPoint projected;
            if (!project_layer_local_point(
                    title, layer, title_time,
                    QVector3D(static_cast<float>(point.x()),
                              static_cast<float>(point.y()), 0.0f),
                    projected))
                return false;
            canvas_quad << projected.canvas;
        }
        return QTransform::quadToQuad(source_quad, canvas_quad,
                                      source_to_canvas);
    }

    TitleCamera fallback;
    const std::string requested = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id
        : (!camera_id.empty() ? camera_id
                              : resolved_layer_camera_id(title, layer,
                                                         title_time));
    const TitleCamera *camera = active_camera(title, requested, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(title, *camera, title_time);
    const QMatrix4x4 local_to_clip = matrices.projection * matrices.view *
        world_matrix(title, layer, title_time);

    QPolygonF canvas_quad;
    canvas_quad.reserve(4);
    for (const QPointF &point : local_quad) {
        const QVector4D clip = local_to_clip * QVector4D(
            static_cast<float>(point.x()), static_cast<float>(point.y()),
            0.0f, 1.0f);
        if (clip.w() <= kEpsilon || !std::isfinite(clip.x()) ||
            !std::isfinite(clip.y()) || !std::isfinite(clip.z()) ||
            !std::isfinite(clip.w()))
            return false;
        const double inv_w = 1.0 / static_cast<double>(clip.w());
        const double ndc_x = static_cast<double>(clip.x()) * inv_w;
        const double ndc_y = static_cast<double>(clip.y()) * inv_w;
        const QPointF canvas_point(
            (ndc_x + 1.0) * 0.5 * std::max(1, title.width),
            (1.0 - ndc_y) * 0.5 * std::max(1, title.height));
        if (!std::isfinite(canvas_point.x()) ||
            !std::isfinite(canvas_point.y()))
            return false;
        canvas_quad << canvas_point;
    }

    QTransform transform;
    if (!QTransform::quadToQuad(source_quad, canvas_quad, transform))
        return false;
    const std::array<double, 9> coefficients = {
        transform.m11(), transform.m12(), transform.m13(),
        transform.m21(), transform.m22(), transform.m23(),
        transform.m31(), transform.m32(), transform.m33()};
    if (!std::all_of(coefficients.begin(), coefficients.end(),
                     [](double value) { return std::isfinite(value); }))
        return false;
    source_to_canvas = transform;
    return true;
}

bool projected_local_polygon(const Title &title, const Layer &layer,
                             double title_time,
                             const QPolygonF &local_polygon,
                             QRectF &canvas_bounds,
                             QPolygonF *clipped_polygon,
                             const std::string &camera_id)
{
    canvas_bounds = QRectF();
    if (clipped_polygon)
        clipped_polygon->clear();
    if (local_polygon.size() < 3 || !layer_supports_3d(layer) ||
        !layer_or_ancestor_uses_3d(title, layer))
        return false;

    if (isolated_asset_owner(title, layer)) {
        QPolygonF canvas_polygon;
        canvas_polygon.reserve(local_polygon.size());
        for (const QPointF &point : local_polygon) {
            LayerProjectionPoint projected;
            if (!project_layer_local_point(
                    title, layer, title_time,
                    QVector3D(static_cast<float>(point.x()),
                              static_cast<float>(point.y()), 0.0f),
                    projected))
                return false;
            canvas_polygon << projected.canvas;
        }
        canvas_bounds = canvas_polygon.boundingRect();
        if (clipped_polygon)
            *clipped_polygon = canvas_polygon;
        return canvas_bounds.isValid() &&
               std::isfinite(canvas_bounds.left()) &&
               std::isfinite(canvas_bounds.top()) &&
               std::isfinite(canvas_bounds.right()) &&
               std::isfinite(canvas_bounds.bottom());
    }

    TitleCamera fallback;
    const std::string requested = !title.render_camera_override_id.empty()
        ? title.render_camera_override_id
        : (!camera_id.empty() ? camera_id
                              : resolved_layer_camera_id(title, layer, title_time));
    const TitleCamera *camera = active_camera(title, requested, title_time);
    if (!camera)
        camera = &fallback;
    const CameraMatrices matrices = camera_matrices(title, *camera, title_time);
    const QMatrix4x4 local_to_clip = matrices.projection * matrices.view *
        world_matrix(title, layer, title_time);

    std::vector<QVector4D> polygon;
    polygon.reserve(static_cast<std::size_t>(local_polygon.size()) + 4u);
    for (const QPointF &point : local_polygon) {
        /* layer_local_clip_point intentionally rejects w <= epsilon, but a
         * polygon crossing the camera/near plane must be clipped rather than
         * discarded. Build the exact clip coordinate directly here. */
        const QVector4D clip = local_to_clip * QVector4D(
            static_cast<float>(point.x()), static_cast<float>(point.y()),
            0.0f, 1.0f);
        if (!std::isfinite(clip.x()) || !std::isfinite(clip.y()) ||
            !std::isfinite(clip.z()) || !std::isfinite(clip.w()))
            return false;
        polygon.push_back(clip);
    }

    const auto clip_against = [&](const std::vector<QVector4D> &input,
                                  auto distance) {
        std::vector<QVector4D> output;
        if (input.empty())
            return output;
        output.reserve(input.size() + 4);
        QVector4D previous = input.back();
        double previous_distance = distance(previous);
        bool previous_inside = previous_distance >= 0.0;
        for (const QVector4D &current : input) {
            const double current_distance = distance(current);
            const bool current_inside = current_distance >= 0.0;
            if (current_inside != previous_inside) {
                const double denominator = previous_distance - current_distance;
                const double t = std::abs(denominator) > kEpsilon
                    ? std::clamp(previous_distance / denominator, 0.0, 1.0)
                    : 0.0;
                output.push_back(previous + (current - previous) *
                    static_cast<float>(t));
            }
            if (current_inside)
                output.push_back(current);
            previous = current;
            previous_distance = current_distance;
            previous_inside = current_inside;
        }
        return output;
    };

    /* Clip in homogeneous coordinates. This bounds huge off-canvas values and
     * keeps near-plane crossings stable instead of projecting w≈0 to infinity. */
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.w()) - kEpsilon;
    });
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.x() + p.w());
    });
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.w() - p.x());
    });
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.y() + p.w());
    });
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.w() - p.y());
    });
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.z() + p.w());
    });
    polygon = clip_against(polygon, [](const QVector4D &p) {
        return static_cast<double>(p.w() - p.z());
    });
    if (polygon.size() < 3)
        return false;

    QPolygonF canvas_polygon;
    canvas_polygon.reserve(static_cast<int>(polygon.size()));
    for (const QVector4D &clip : polygon) {
        if (clip.w() <= kEpsilon)
            continue;
        const double inv_w = 1.0 / static_cast<double>(clip.w());
        const double ndc_x = static_cast<double>(clip.x()) * inv_w;
        const double ndc_y = static_cast<double>(clip.y()) * inv_w;
        const QPointF canvas_point(
            (ndc_x + 1.0) * 0.5 * std::max(1, title.width),
            (1.0 - ndc_y) * 0.5 * std::max(1, title.height));
        if (std::isfinite(canvas_point.x()) &&
            std::isfinite(canvas_point.y()))
            canvas_polygon << canvas_point;
    }
    if (canvas_polygon.size() < 3)
        return false;
    canvas_bounds = canvas_polygon.boundingRect().normalized();
    if (clipped_polygon)
        *clipped_polygon = canvas_polygon;
    return canvas_bounds.isValid() && !canvas_bounds.isEmpty();
}

bool projected_local_bounds(const Title &title, const Layer &layer,
                            double title_time, const QRectF &local_bounds,
                            QRectF &canvas_bounds, QPolygonF *clipped_polygon,
                            const std::string &camera_id)
{
    if (!local_bounds.isValid() || local_bounds.isEmpty()) {
        canvas_bounds = QRectF();
        if (clipped_polygon)
            clipped_polygon->clear();
        return false;
    }
    QPolygonF rectangle;
    rectangle << local_bounds.topLeft() << local_bounds.topRight()
              << local_bounds.bottomRight() << local_bounds.bottomLeft();
    return projected_local_polygon(title, layer, title_time, rectangle,
                                   canvas_bounds, clipped_polygon, camera_id);
}

QVector3D world_delta_to_parent_space(const Title &title, const Layer &layer,
                                      double title_time,
                                      const QVector3D &world_delta)
{
    bool invertible = false;
    const QMatrix4x4 inverse = parent_world_matrix(
        title, layer, title_time).inverted(&invertible);
    return invertible ? inverse.mapVector(world_delta) : world_delta;
}

QTransform projected_or_legacy(const Title &title, const Layer &layer,
                               double title_time,
                               const QTransform &legacy_transform,
                               EvaluatedTransform *details)
{
    if (!layer_or_ancestor_uses_3d(title, layer) || !layer_supports_3d(layer)) {
        if (details)
            *details = EvaluatedTransform{};
        return legacy_transform;
    }
    const EvaluatedTransform evaluated = evaluate(title, layer, title_time);
    if (details)
        *details = evaluated;
    if (evaluated.projectable)
        return evaluated.projected;
    /* Keep clipped/behind-camera 3D planes out of the frame. Returning the
     * legacy affine transform here would make them pop back into 2D. */
    return QTransform::fromTranslate(1.0e9, 1.0e9);
}

} // namespace fxm::transform3d
