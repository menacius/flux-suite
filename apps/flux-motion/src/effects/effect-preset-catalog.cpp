#include "effect-preset-catalog.h"
#include "effect-runtime.h"
#include "preset-category-path.h"
#include "extensions/effect-extension-catalog.h"
#include "asset-path-provider.h"


#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace fxm::effects {
namespace {

constexpr qint64 kMaxPresetFileBytes = 1024 * 1024;

QString normalized_path(const QString &path)
{
    QFileInfo info(path);
    QString value = info.canonicalFilePath();
    if (value.isEmpty())
        value = info.absoluteFilePath();
    value = QDir::fromNativeSeparators(QDir::cleanPath(value));
#ifdef Q_OS_WIN
    value = value.toLower();
#endif
    return value;
}

bool paths_equal(const QString &first, const QString &second)
{
    const QString normalized_first = normalized_path(first);
    const QString normalized_second = normalized_path(second);
    return !normalized_first.isEmpty() && normalized_first == normalized_second;
}

bool is_effect_preset_file_in_library(const QString &file_path)
{
    const QString root = effect_presets_root_path();
    if (root.isEmpty())
        return false;

    const QFileInfo info(file_path);
    const QString expected_suffix = QString::fromUtf8(kEffectPresetExtension).mid(1);
    const QString normalized_file = normalized_path(file_path);
    const QString normalized_root = normalized_path(root);
    const bool canonical_child = !normalized_file.isEmpty() && !normalized_root.isEmpty() &&
        normalized_file.startsWith(normalized_root + QLatin1Char('/'));
    return info.exists() && info.isFile() && info.isReadable() && canonical_child &&
           info.suffix().compare(expected_suffix, Qt::CaseInsensitive) == 0 &&
           paths_equal(info.absolutePath(), root);
}


bool valid_effect_category_path(const QStringList &parts)
{
    if (parts.isEmpty())
        return false;
    const QString root = parts.front();
    return root.compare(QStringLiteral("Effects"), Qt::CaseInsensitive) == 0 ||
           root.compare(QStringLiteral("Animation Presets"), Qt::CaseInsensitive) == 0;
}

void set_color_channels(LayerEffect &effect, uint32_t argb)
{
    effect.color_a.static_value = (argb >> 24) & 0xFF;
    effect.color_r.static_value = (argb >> 16) & 0xFF;
    effect.color_g.static_value = (argb >> 8) & 0xFF;
    effect.color_b.static_value = argb & 0xFF;
}

void set_stroke_color_channels(LayerEffect &effect, uint32_t argb)
{
    effect.stroke_color_a.static_value = (argb >> 24) & 0xFF;
    effect.stroke_color_r.static_value = (argb >> 16) & 0xFF;
    effect.stroke_color_g.static_value = (argb >> 8) & 0xFF;
    effect.stroke_color_b.static_value = argb & 0xFF;
}

void set_secondary_color_channels(LayerEffect &effect, uint32_t argb)
{
    effect.secondary_color_a.static_value = (argb >> 24) & 0xFF;
    effect.secondary_color_r.static_value = (argb >> 16) & 0xFF;
    effect.secondary_color_g.static_value = (argb >> 8) & 0xFF;
    effect.secondary_color_b.static_value = argb & 0xFF;
}

uint32_t json_color(const QJsonObject &object, const char *key, uint32_t fallback)
{
    const QJsonValue value = object.value(QString::fromUtf8(key));
    if (value.isDouble())
        return static_cast<uint32_t>(value.toVariant().toULongLong());
    if (!value.isString())
        return fallback;

    QString text = value.toString().trimmed();
    if (text.startsWith(QLatin1Char('#')))
        text.remove(0, 1);
    bool ok = false;
    const qulonglong parsed = text.toULongLong(&ok, 16);
    if (!ok)
        return fallback;
    if (text.size() <= 6)
        return 0xFF000000u | static_cast<uint32_t>(parsed);
    return static_cast<uint32_t>(parsed);
}

void apply_parameter_overrides(LayerEffect &effect, const QJsonObject &p)
{
    auto number = [&p](const char *key, double fallback) {
        const QJsonValue value = p.value(QString::fromUtf8(key));
        return value.isDouble() ? value.toDouble(fallback) : fallback;
    };
    auto integer = [&p](const char *key, int fallback) {
        const QJsonValue value = p.value(QString::fromUtf8(key));
        return value.isDouble() ? value.toInt(fallback) : fallback;
    };
    auto boolean = [&p](const char *key, bool fallback) {
        const QJsonValue value = p.value(QString::fromUtf8(key));
        return value.isBool() ? value.toBool(fallback) : fallback;
    };

    effect.enabled = boolean("enabled", effect.enabled);
    effect.brightness = static_cast<float>(number("brightness", effect.brightness));
    effect.contrast = static_cast<float>(number("contrast", effect.contrast));
    effect.saturation = static_cast<float>(number("saturation", effect.saturation));
    effect.tint_color = json_color(p, "tintColor", effect.tint_color);
    effect.tint_amount = static_cast<float>(number("tintAmount", effect.tint_amount));
    effect.effect_color = json_color(p, "color", effect.effect_color);
    effect.effect_opacity = static_cast<float>(number("opacity", effect.effect_opacity));
    effect.effect_size = static_cast<float>(number("size", effect.effect_size));
    effect.effect_distance = static_cast<float>(number("distance", effect.effect_distance));
    effect.effect_angle = static_cast<float>(number("angle", effect.effect_angle));
    effect.effect_spread = static_cast<float>(number("spread", effect.effect_spread));
    effect.effect_falloff = static_cast<float>(number("falloff", effect.effect_falloff));
    effect.effect_blur_type = integer("blurType", effect.effect_blur_type);
    effect.effect_samples = std::max(1, integer("samples", effect.effect_samples));
    effect.effect_centered = boolean("centered", effect.effect_centered);
    effect.blend_mode = static_cast<EffectBlendMode>(
        std::clamp(integer("blendMode", static_cast<int>(effect.blend_mode)),
                   static_cast<int>(EffectBlendMode::Normal),
                   static_cast<int>(EffectBlendMode::Color)));

    effect.effect_fill_type = integer("fillType", effect.effect_fill_type);
    effect.effect_join_style = integer("joinStyle", effect.effect_join_style);
    effect.effect_on_front = boolean("onFront", effect.effect_on_front);
    effect.effect_antialias = boolean("antialias", effect.effect_antialias);
    effect.effect_stroke_color = json_color(p, "strokeColor", effect.effect_stroke_color);
    effect.effect_stroke_width = static_cast<float>(number("strokeWidth", effect.effect_stroke_width));
    effect.effect_stroke_opacity = static_cast<float>(number("strokeOpacity", effect.effect_stroke_opacity));
    effect.effect_trim_start = static_cast<float>(number("start", effect.effect_trim_start));
    effect.effect_trim_end = static_cast<float>(number("end", effect.effect_trim_end));
    effect.effect_trim_offset = static_cast<float>(number("trimOffset", effect.effect_trim_offset));
    effect.effect_trim_multiple_shapes = integer("trimMultipleShapes", effect.effect_trim_multiple_shapes);
    effect.effect_padding_left = static_cast<float>(number("paddingLeft", effect.effect_padding_left));
    effect.effect_padding_right = static_cast<float>(number("paddingRight", effect.effect_padding_right));
    effect.effect_padding_top = static_cast<float>(number("paddingTop", effect.effect_padding_top));
    effect.effect_padding_bottom = static_cast<float>(number("paddingBottom", effect.effect_padding_bottom));
    effect.effect_corner_radius_tl = static_cast<float>(number("cornerRadiusTL", effect.effect_corner_radius_tl));
    effect.effect_corner_radius_tr = static_cast<float>(number("cornerRadiusTR", effect.effect_corner_radius_tr));
    effect.effect_corner_radius_br = static_cast<float>(number("cornerRadiusBR", effect.effect_corner_radius_br));
    effect.effect_corner_radius_bl = static_cast<float>(number("cornerRadiusBL", effect.effect_corner_radius_bl));
    effect.effect_corner_type = integer("cornerType", effect.effect_corner_type);
    effect.effect_profile = integer("profile", effect.effect_profile);
    effect.effect_animated = boolean("animated", effect.effect_animated);
    effect.effect_monochrome = boolean("monochrome", effect.effect_monochrome);
    effect.effect_invert = boolean("invert", effect.effect_invert);
    effect.effect_seed = integer("seed", effect.effect_seed);
    effect.effect_amount = static_cast<float>(number("amount", effect.effect_amount));
    effect.effect_scale = static_cast<float>(number("scale", effect.effect_scale));
    effect.effect_softness = static_cast<float>(number("softness", effect.effect_softness));
    effect.effect_roundness = static_cast<float>(number("roundness", effect.effect_roundness));
    effect.effect_speed = static_cast<float>(number("speed", effect.effect_speed));
    effect.effect_center_x = static_cast<float>(number("centerX", effect.effect_center_x));
    effect.effect_center_y = static_cast<float>(number("centerY", effect.effect_center_y));
    effect.effect_complexity = static_cast<float>(number("complexity", effect.effect_complexity));
    effect.effect_evolution = static_cast<float>(number("evolution", effect.effect_evolution));
    effect.effect_affect_alpha = boolean("affectAlpha", effect.effect_affect_alpha);
    effect.effect_clamp_output = boolean("clampOutput", effect.effect_clamp_output);
    effect.effect_temporal_stability = boolean("temporalStability", effect.effect_temporal_stability);
    effect.effect_secondary_color = json_color(p, "secondaryColor", effect.effect_secondary_color);

    auto finite_clamp = [](float value, float minimum, float maximum, float fallback) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    };
    effect.brightness = finite_clamp(effect.brightness, -1.0f, 1.0f, 0.0f);
    effect.contrast = finite_clamp(effect.contrast, 0.0f, 4.0f, 1.0f);
    effect.saturation = finite_clamp(effect.saturation, 0.0f, 4.0f, 1.0f);
    effect.tint_amount = finite_clamp(effect.tint_amount, 0.0f, 1.0f, 1.0f);
    effect.effect_opacity = finite_clamp(effect.effect_opacity, 0.0f, 1.0f, 1.0f);
    effect.effect_angle = finite_clamp(effect.effect_angle, -360.0f, 360.0f, 0.0f);
    effect.effect_blur_type = std::clamp(effect.effect_blur_type,
        (int)ShadowBlurType::Box, (int)ShadowBlurType::DualKawase);
    effect.effect_samples = std::clamp(effect.effect_samples, 2, 64);
    effect.effect_fill_type = std::clamp(effect.effect_fill_type, 0, 2);
    effect.effect_join_style = std::clamp(effect.effect_join_style, 0, 2);
    effect.effect_corner_type = std::clamp(effect.effect_corner_type, 0, 3);
    effect.effect_stroke_width = finite_clamp(effect.effect_stroke_width, 0.0f, 1000.0f, 0.0f);
    effect.effect_stroke_opacity = finite_clamp(effect.effect_stroke_opacity, 0.0f, 1.0f, 1.0f);
    effect.effect_trim_start = finite_clamp(effect.effect_trim_start, 0.0f, 100.0f, 0.0f);
    effect.effect_trim_end = finite_clamp(effect.effect_trim_end, 0.0f, 100.0f, 100.0f);
    effect.effect_trim_offset = std::isfinite(effect.effect_trim_offset) ? effect.effect_trim_offset : 0.0f;
    effect.effect_trim_multiple_shapes = std::clamp(effect.effect_trim_multiple_shapes, 0, 1);
    effect.effect_padding_left = finite_clamp(effect.effect_padding_left, -1000.0f, 1000.0f, 0.0f);
    effect.effect_padding_right = finite_clamp(effect.effect_padding_right, -1000.0f, 1000.0f, 0.0f);
    effect.effect_padding_top = finite_clamp(effect.effect_padding_top, -1000.0f, 1000.0f, 0.0f);
    effect.effect_padding_bottom = finite_clamp(effect.effect_padding_bottom, -1000.0f, 1000.0f, 0.0f);
    effect.effect_corner_radius_tl = finite_clamp(effect.effect_corner_radius_tl, 0.0f, 1000.0f, 0.0f);
    effect.effect_corner_radius_tr = finite_clamp(effect.effect_corner_radius_tr, 0.0f, 1000.0f, 0.0f);
    effect.effect_corner_radius_br = finite_clamp(effect.effect_corner_radius_br, 0.0f, 1000.0f, 0.0f);
    effect.effect_corner_radius_bl = finite_clamp(effect.effect_corner_radius_bl, 0.0f, 1000.0f, 0.0f);
    effect.effect_profile = std::clamp(effect.effect_profile, 0, 16);
    effect.effect_seed = std::clamp(effect.effect_seed, 0, 1000000);
    effect.effect_amount = finite_clamp(effect.effect_amount, 0.0f, 10.0f, 1.0f);
    effect.effect_scale = finite_clamp(effect.effect_scale, 0.001f, 1000.0f, 1.0f);
    effect.effect_softness = finite_clamp(effect.effect_softness, 0.0f, 1.0f, 0.25f);
    effect.effect_roundness = finite_clamp(effect.effect_roundness, -1.0f, 1.0f, 0.0f);
    effect.effect_speed = finite_clamp(effect.effect_speed, -100.0f, 100.0f, 1.0f);
    effect.effect_center_x = finite_clamp(effect.effect_center_x, -4.0f, 4.0f, 0.5f);
    effect.effect_center_y = finite_clamp(effect.effect_center_y, -4.0f, 4.0f, 0.5f);
    effect.effect_complexity = finite_clamp(effect.effect_complexity, 1.0f, 12.0f, 4.0f);
    if ((effect.type == LayerEffectType::Noise || effect.type == LayerEffectType::Grain ||
        effect.type == LayerEffectType::FilmDistortion || effect.type == LayerEffectType::AnalogDistortion ||
        effect.type == LayerEffectType::DigitalDistortion) && effect.extension_schema_version >= 1) {
        effect.effect_roundness = finite_clamp(effect.effect_roundness, -3.0f, 3.0f, 0.0f);
        effect.effect_center_x = finite_clamp(effect.effect_center_x, -100000.0f, 100000.0f, 0.0f);
        effect.effect_center_y = finite_clamp(effect.effect_center_y, -100000.0f, 100000.0f, 0.0f);
        effect.effect_complexity = finite_clamp(effect.effect_complexity, 1.0f, 8.0f, 5.0f);
        effect.effect_spread = finite_clamp(effect.effect_spread, 1.01f, 8.0f, 2.0f);
        effect.effect_falloff = finite_clamp(effect.effect_falloff, 0.0f, 1.0f, 0.5f);
    }
    effect.effect_evolution = finite_clamp(effect.effect_evolution, -100000.0f, 100000.0f, 0.0f);

    switch (effect.type) {
    case LayerEffectType::Outline:
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 200.0f, 4.0f);
        break;
    case LayerEffectType::MotionBlur:
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 720.0f, 180.0f);
        effect.effect_distance = 0.0f;
        effect.effect_spread = 0.0f;
        break;
    case LayerEffectType::Emboss:
        effect.effect_size = finite_clamp(effect.effect_size, 0.1f, 32.0f, 2.0f);
        effect.effect_distance = finite_clamp(effect.effect_distance, 0.1f, 32.0f, 1.0f);
        effect.effect_spread = finite_clamp(effect.effect_spread, 0.0f, 16.0f, 0.0f);
        break;
    case LayerEffectType::LensFlare:
        effect.effect_size = finite_clamp(effect.effect_size, 0.001f, 4.0f, 0.22f);
        effect.effect_spread = finite_clamp(effect.effect_spread, 0.0f, 4.0f, 0.8f);
        effect.effect_falloff = finite_clamp(effect.effect_falloff, 0.01f, 16.0f, 2.0f);
        break;
    case LayerEffectType::Vignette:
    case LayerEffectType::Noise:
    case LayerEffectType::Grain:
    case LayerEffectType::FilmDistortion:
    case LayerEffectType::AnalogDistortion:
    case LayerEffectType::DigitalDistortion:
    case LayerEffectType::RoughenEdges:
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 4096.0f, 16.0f);
        break;
    case LayerEffectType::Bloom:
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 512.0f, 24.0f);
        effect.effect_spread = finite_clamp(effect.effect_spread, 0.0f, 1.0f, 0.65f);
        effect.effect_falloff = finite_clamp(effect.effect_falloff, 0.0f, 8.0f, 1.0f);
        break;
    case LayerEffectType::DropShadow:
    case LayerEffectType::InnerShadow:
        effect.effect_distance = finite_clamp(effect.effect_distance, 0.0f, 4096.0f, 8.0f);
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 512.0f, 4.0f);
        effect.effect_spread = finite_clamp(effect.effect_spread, 0.0f, 512.0f, 0.0f);
        break;
    case LayerEffectType::LongShadow:
        effect.effect_distance = finite_clamp(effect.effect_distance, 0.0f, 4096.0f, 120.0f);
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 512.0f, 0.0f);
        effect.effect_falloff = finite_clamp(effect.effect_falloff, 0.0f, 8.0f, 1.0f);
        break;
    case LayerEffectType::Glow:
    case LayerEffectType::InnerGlow:
    case LayerEffectType::Blur:
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 512.0f, 16.0f);
        break;
    default:
        effect.effect_size = finite_clamp(effect.effect_size, 0.0f, 4096.0f, 16.0f);
        effect.effect_distance = finite_clamp(effect.effect_distance, 0.0f, 4096.0f, 8.0f);
        effect.effect_spread = finite_clamp(effect.effect_spread, 0.0f, 512.0f, 0.0f);
        effect.effect_falloff = finite_clamp(effect.effect_falloff, 0.0f, 8.0f, 1.0f);
        break;
    }

    effect.enabled_prop.static_value = effect.enabled ? 1.0 : 0.0;
    effect.brightness_prop.static_value = effect.brightness;
    effect.contrast_prop.static_value = effect.contrast;
    effect.saturation_prop.static_value = effect.saturation;
    effect.opacity_prop.static_value = effect.effect_opacity;
    effect.size_prop.static_value = effect.effect_size;
    effect.distance_prop.static_value = effect.effect_distance;
    effect.angle_prop.static_value = effect.effect_angle;
    effect.spread_prop.static_value = effect.effect_spread;
    effect.falloff_prop.static_value = effect.effect_falloff;
    effect.stroke_width_prop.static_value = effect.effect_stroke_width;
    effect.trim_start_prop.static_value = effect.effect_trim_start;
    effect.trim_end_prop.static_value = effect.effect_trim_end;
    effect.trim_offset_prop.static_value = effect.effect_trim_offset;
    effect.stroke_opacity_prop.static_value = effect.effect_stroke_opacity;
    effect.padding_left_prop.static_value = effect.effect_padding_left;
    effect.padding_right_prop.static_value = effect.effect_padding_right;
    effect.padding_top_prop.static_value = effect.effect_padding_top;
    effect.padding_bottom_prop.static_value = effect.effect_padding_bottom;
    effect.corner_radius_tl_prop.static_value = effect.effect_corner_radius_tl;
    effect.corner_radius_tr_prop.static_value = effect.effect_corner_radius_tr;
    effect.corner_radius_br_prop.static_value = effect.effect_corner_radius_br;
    effect.corner_radius_bl_prop.static_value = effect.effect_corner_radius_bl;
    effect.amount_prop.static_value = effect.effect_amount;
    effect.scale_prop.static_value = effect.effect_scale;
    effect.softness_prop.static_value = effect.effect_softness;
    effect.roundness_prop.static_value = effect.effect_roundness;
    effect.speed_prop.static_value = effect.effect_speed;
    effect.center_x_prop.static_value = effect.effect_center_x;
    effect.center_y_prop.static_value = effect.effect_center_y;
    effect.complexity_prop.static_value = effect.effect_complexity;
    effect.evolution_prop.static_value = effect.effect_evolution;
    effect.gradient_start_pos_prop.static_value = effect.effect_gradient_start_pos;
    effect.gradient_end_pos_prop.static_value = effect.effect_gradient_end_pos;
    effect.gradient_start_opacity_prop.static_value = effect.effect_gradient_start_opacity;
    effect.gradient_end_opacity_prop.static_value = effect.effect_gradient_end_opacity;
    effect.gradient_angle_prop.static_value = effect.effect_gradient_angle;
    effect.gradient_center_x_prop.static_value = effect.effect_gradient_center_x;
    effect.gradient_center_y_prop.static_value = effect.effect_gradient_center_y;
    effect.gradient_scale_prop.static_value = effect.effect_gradient_scale;
    effect.gradient_focal_x_prop.static_value = effect.effect_gradient_focal_x;
    effect.gradient_focal_y_prop.static_value = effect.effect_gradient_focal_y;
    effect.gradient_opacity_prop.static_value = effect.effect_gradient_opacity;
    effect.gradient_start_color_a.static_value = (effect.effect_gradient_start_color >> 24) & 0xFF;
    effect.gradient_start_color_r.static_value = (effect.effect_gradient_start_color >> 16) & 0xFF;
    effect.gradient_start_color_g.static_value = (effect.effect_gradient_start_color >> 8) & 0xFF;
    effect.gradient_start_color_b.static_value = effect.effect_gradient_start_color & 0xFF;
    effect.gradient_end_color_a.static_value = (effect.effect_gradient_end_color >> 24) & 0xFF;
    effect.gradient_end_color_r.static_value = (effect.effect_gradient_end_color >> 16) & 0xFF;
    effect.gradient_end_color_g.static_value = (effect.effect_gradient_end_color >> 8) & 0xFF;
    effect.gradient_end_color_b.static_value = effect.effect_gradient_end_color & 0xFF;
    set_color_channels(effect, effect.effect_color);
    set_stroke_color_channels(effect, effect.effect_stroke_color);
    set_secondary_color_channels(effect, effect.effect_secondary_color);
}

} // namespace

LayerEffect make_default_layer_effect(LayerEffectType type)
{
    LayerEffect effect;
    effect.type = type;
    effect.extension_id = FxmEffectExtensionCatalog::builtInId(type).toStdString();
    if (const EffectDescriptor *descriptor = effect_descriptor(type))
        effect.extension_schema_version = descriptor->schema_version;
    effect.enabled = true;
    effect.enabled_prop.static_value = 1.0;

    switch (type) {
    case LayerEffectType::TrimPaths:
        effect.effect_trim_start = 0.0f;
        effect.effect_trim_end = 100.0f;
        effect.effect_trim_offset = 0.0f;
        effect.effect_trim_multiple_shapes = 0;
        break;
    case LayerEffectType::BackgroundColor:
        effect.effect_color = 0xFF000000;
        effect.effect_opacity = 0.35f;
        break;
    case LayerEffectType::Outline:
        effect.effect_fill_type = 1;
        effect.effect_color = 0xFF000000;
        effect.effect_size = 4.0f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::DropShadow:
    case LayerEffectType::InnerShadow:
        effect.blend_mode = EffectBlendMode::Multiply;
        effect.effect_color = 0x99000000;
        effect.effect_opacity = 0.6f;
        effect.effect_size = 4.0f;
        break;
    case LayerEffectType::LongShadow:
        effect.blend_mode = EffectBlendMode::Multiply;
        effect.effect_color = 0x99000000;
        effect.effect_distance = 120.0f;
        effect.effect_angle = 135.0f;
        effect.effect_size = 0.0f;
        effect.effect_opacity = 0.45f;
        break;
    case LayerEffectType::ColorOverlay:
        effect.blend_mode = EffectBlendMode::Color;
        effect.effect_color = effect.tint_color;
        effect.effect_opacity = effect.tint_amount;
        break;
    case LayerEffectType::Glow:
    case LayerEffectType::InnerGlow:
        effect.blend_mode = EffectBlendMode::Additive;
        effect.effect_color = 0xFFFFFFFF;
        effect.effect_opacity = 0.75f;
        break;
    case LayerEffectType::Blur:
        effect.effect_size = 12.0f;
        effect.effect_opacity = 1.0f;
        effect.effect_blur_type = static_cast<int>(ShadowBlurType::Gaussian);
        break;
    case LayerEffectType::Bloom:
        effect.blend_mode = EffectBlendMode::Screen;
        effect.effect_color = 0xFFFFFFFF;
        effect.effect_opacity = 0.8f;
        effect.effect_size = 24.0f;
        effect.effect_spread = 0.65f;
        effect.effect_falloff = 1.0f;
        effect.effect_blur_type = static_cast<int>(ShadowBlurType::DualKawase);
        break;
    case LayerEffectType::Emboss:
        effect.effect_size = 2.0f;
        effect.effect_distance = 2.0f;
        effect.effect_angle = 135.0f;
        effect.effect_opacity = 0.8f;
        effect.effect_spread = 0.5f;
        effect.blend_mode = EffectBlendMode::Overlay;
        break;
    case LayerEffectType::MotionBlur:
        effect.effect_size = 180.0f;
        effect.effect_angle = 0.0f;
        effect.effect_opacity = 1.0f;
        effect.effect_samples = 8;
        effect.effect_centered = true;
        break;
    case LayerEffectType::LensFlare:
        effect.blend_mode = EffectBlendMode::Screen;
        effect.effect_profile = 0;
        effect.effect_color = 0xFFFFD59A;
        effect.effect_secondary_color = 0xFF4EA3FF;
        effect.effect_opacity = 0.85f;
        effect.effect_amount = 1.0f;
        effect.effect_scale = 1.0f;
        effect.effect_size = 0.22f;
        effect.effect_spread = 0.8f;
        effect.effect_falloff = 2.0f;
        effect.effect_center_x = 0.5f;
        effect.effect_center_y = 0.5f;
        effect.effect_samples = 6;
        effect.effect_complexity = 6.0f;
        break;
    case LayerEffectType::Vignette:
        effect.blend_mode = EffectBlendMode::Multiply;
        effect.effect_color = 0xFF000000;
        effect.effect_opacity = 1.0f;
        effect.effect_amount = 0.65f;
        effect.effect_scale = 0.78f;
        effect.effect_softness = 0.35f;
        effect.effect_roundness = 0.0f;
        break;
    case LayerEffectType::Noise:
        /* Noise is the procedural/noise-map generator. Grain is now separate. */
        effect.effect_profile = 3; /* Clouds / fBM */
        effect.effect_amount = 0.18f;
        effect.effect_scale = 96.0f;
        effect.effect_softness = 0.12f;
        effect.effect_speed = 1.0f;
        effect.effect_complexity = 5.0f;
        effect.effect_spread = 2.0f;
        effect.effect_falloff = 0.52f;
        effect.effect_roundness = 0.0f;
        effect.effect_center_x = 0.0f;
        effect.effect_center_y = 0.0f;
        effect.effect_animated = false;
        effect.effect_monochrome = true;
        effect.effect_seed = 1;
        effect.effect_color = 0xFFFFFFFF;
        effect.effect_affect_alpha = false;
        effect.effect_clamp_output = true;
        effect.effect_temporal_stability = true;
        break;
    case LayerEffectType::Grain:
        effect.effect_profile = 1; /* Film Grain */
        effect.effect_amount = 0.22f;
        effect.effect_scale = 2.0f;
        effect.effect_softness = 0.08f;
        effect.effect_speed = 1.0f;
        effect.effect_complexity = 5.0f;
        effect.effect_spread = 2.0f;
        effect.effect_falloff = 0.52f;
        effect.effect_roundness = 0.0f;
        effect.effect_center_x = 0.0f;
        effect.effect_center_y = 0.0f;
        effect.effect_animated = false;
        effect.effect_monochrome = true;
        effect.effect_seed = 1;
        effect.effect_color = 0xFFFFFFFF;
        effect.effect_affect_alpha = false;
        effect.effect_clamp_output = true;
        effect.effect_temporal_stability = true;
        break;
    case LayerEffectType::FilmDistortion:
        effect.effect_profile = 8;
        effect.effect_amount = 0.62f;
        effect.effect_scale = 1.8f;
        effect.effect_softness = 0.22f;
        effect.effect_speed = 1.0f;
        effect.effect_complexity = 7.5f;
        effect.effect_spread = 2.4f;
        effect.effect_falloff = 0.70f;
        effect.effect_roundness = 0.0f;
        effect.effect_center_x = 0.0f;
        effect.effect_center_y = 0.0f;
        effect.effect_animated = true;
        effect.effect_monochrome = false;
        effect.effect_seed = 17;
        effect.effect_color = 0xFFFFF1C4;
        effect.effect_secondary_color = 0xFF1B120A;
        effect.effect_temporal_stability = true;
        break;
    case LayerEffectType::AnalogDistortion:
        effect.effect_profile = 9;
        effect.effect_amount = 0.56f;
        effect.effect_scale = 8.0f;
        effect.effect_softness = 0.18f;
        effect.effect_speed = 1.0f;
        effect.effect_complexity = 6.0f;
        effect.effect_spread = 2.2f;
        effect.effect_falloff = 0.65f;
        effect.effect_roundness = 0.0f;
        effect.effect_center_x = 0.0f;
        effect.effect_center_y = 0.0f;
        effect.effect_animated = true;
        effect.effect_monochrome = false;
        effect.effect_seed = 29;
        effect.effect_color = 0xFFFFFFFF;
        effect.effect_secondary_color = 0xFF73A8FF;
        effect.effect_temporal_stability = true;
        break;
    case LayerEffectType::DigitalDistortion:
        effect.effect_profile = 10;
        effect.effect_amount = 0.52f;
        effect.effect_scale = 20.0f;
        effect.effect_softness = 0.09f;
        effect.effect_speed = 1.0f;
        effect.effect_complexity = 5.0f;
        effect.effect_spread = 2.0f;
        effect.effect_falloff = 0.62f;
        effect.effect_roundness = 0.0f;
        effect.effect_center_x = 0.0f;
        effect.effect_center_y = 0.0f;
        effect.effect_animated = true;
        effect.effect_monochrome = false;
        effect.effect_seed = 43;
        effect.effect_color = 0xFFFFFFFF;
        effect.effect_secondary_color = 0xFF8DFFEC;
        effect.effect_temporal_stability = true;
        break;
    case LayerEffectType::RoughenEdges:
        effect.effect_amount = 0.18f;
        effect.effect_scale = 48.0f;
        effect.effect_softness = 0.2f;
        effect.effect_complexity = 4.0f;
        effect.effect_seed = 1;
        break;
    case LayerEffectType::Sharpen:
        effect.effect_amount = 1.0f;
        effect.effect_size = 1.75f;
        effect.effect_softness = 0.005f;
        effect.effect_monochrome = true;
        effect.effect_affect_alpha = true;
        break;
    case LayerEffectType::UnsharpMask:
        effect.effect_amount = 1.35f;
        effect.effect_size = 3.0f;
        effect.effect_softness = 0.01f;
        effect.effect_spread = 0.15f;
        effect.effect_falloff = 0.15f;
        effect.effect_monochrome = true;
        effect.effect_affect_alpha = true;
        break;
    case LayerEffectType::HighPass:
        effect.effect_amount = 1.8f;
        effect.effect_size = 5.0f;
        effect.effect_monochrome = true;
        effect.effect_invert = false; /* overlay preview */
        effect.effect_affect_alpha = true;
        break;
    case LayerEffectType::Clarity:
        effect.effect_amount = 0.85f;
        effect.effect_size = 18.0f;
        effect.effect_softness = 0.005f;
        effect.brightness = 0.0f; /* midtone bias */
        effect.effect_spread = 0.35f;
        effect.effect_falloff = 0.35f;
        effect.effect_monochrome = true;
        effect.effect_affect_alpha = true;
        break;
    case LayerEffectType::BilateralSharpen:
        effect.effect_amount = 1.0f;
        effect.effect_size = 3.0f;
        effect.effect_softness = 0.045f; /* range threshold */
        effect.effect_spread = 0.75f; /* edge protection */
        effect.effect_monochrome = true;
        effect.effect_affect_alpha = true;
        break;
    case LayerEffectType::Glare:
        effect.blend_mode = EffectBlendMode::Screen;
        effect.effect_color = 0xFFFFE6C0;
        effect.effect_secondary_color = 0xFF78AFFF;
        effect.effect_opacity = 0.85f;
        effect.effect_amount = 1.25f;
        effect.effect_scale = 1.0f;
        effect.effect_size = 22.0f;
        effect.effect_distance = 220.0f;
        effect.effect_angle = 0.0f;
        effect.effect_spread = 0.72f;
        effect.effect_falloff = 1.35f;
        effect.effect_softness = 0.28f;
        break;
    case LayerEffectType::Halation:
        effect.blend_mode = EffectBlendMode::Screen;
        effect.effect_color = 0xFFFF5A30;
        effect.effect_secondary_color = 0xFFFF9A52;
        effect.effect_opacity = 0.58f;
        effect.effect_amount = 1.0f;
        effect.effect_size = 22.0f;
        effect.effect_spread = 0.68f;
        effect.effect_falloff = 1.25f;
        effect.effect_softness = 0.35f;
        effect.effect_blur_type = static_cast<int>(ShadowBlurType::DualKawase);
        break;
    case LayerEffectType::LensDistortion:
        effect.effect_roundness = -0.35f;
        effect.effect_opacity = 1.0f;
        effect.effect_center_x = 0.5f;
        effect.effect_center_y = 0.5f;
        break;
    case LayerEffectType::ChromaticAberration:
        effect.effect_amount = 8.0f;
        effect.effect_opacity = 1.0f;
        effect.effect_center_x = 0.5f;
        effect.effect_center_y = 0.5f;
        break;
    case LayerEffectType::DirectionalBlur:
        effect.effect_size = 18.0f;
        effect.effect_angle = 0.0f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::ZoomBlur:
        effect.effect_size = 28.0f;
        effect.effect_center_x = 0.5f;
        effect.effect_center_y = 0.5f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::RadialBlur:
        effect.effect_size = 35.0f;
        effect.effect_center_x = 0.5f;
        effect.effect_center_y = 0.5f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::Ripple:
        effect.effect_amount = 2.5f;
        effect.effect_scale = 12.0f;
        effect.effect_center_x = 0.5f;
        effect.effect_center_y = 0.5f;
        break;
    case LayerEffectType::WaveWarp:
        effect.effect_amount = 3.0f;
        effect.effect_scale = 8.0f;
        effect.effect_angle = 0.0f;
        break;
    case LayerEffectType::Pixelate:
        effect.effect_size = 12.0f;
        break;
    case LayerEffectType::EdgeDetect:
        effect.effect_size = 1.0f;
        effect.effect_amount = 2.0f;
        effect.effect_spread = 0.08f;
        effect.effect_color = 0xFFFFFFFF;
        break;
    case LayerEffectType::Posterize:
        effect.effect_complexity = 6.0f;
        break;
    case LayerEffectType::Threshold:
        effect.effect_spread = 0.5f;
        effect.effect_softness = 0.08f;
        effect.effect_color = 0xFFFFFFFF;
        break;
    case LayerEffectType::Scanlines:
        effect.effect_amount = 0.45f;
        effect.effect_scale = 4.0f;
        effect.effect_softness = 0.45f;
        effect.effect_angle = 90.0f;
        break;
    case LayerEffectType::ChromaKey:
        effect.effect_color = 0xFF00FF00;
        effect.effect_amount = 0.28f;
        effect.effect_softness = 0.12f;
        effect.effect_spread = 0.70f;
        effect.effect_falloff = 0.20f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::LumaKey:
        effect.effect_amount = 0.50f;
        effect.effect_softness = 0.10f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::ColorRange:
        effect.effect_color = 0xFF00FF00;
        effect.effect_amount = 0.22f;
        effect.effect_softness = 0.10f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::SpillSuppression:
        effect.effect_color = 0xFF00FF00;
        effect.effect_amount = 0.80f;
        effect.effect_softness = 0.35f;
        effect.effect_monochrome = true;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::MatteChoker:
        effect.effect_amount = 0.35f;
        effect.effect_size = 2.0f;
        effect.effect_softness = 0.15f;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::LightWrap:
        effect.effect_source_mode = 0;
        effect.effect_size = 24.0f;
        effect.effect_amount = 1.0f;
        effect.effect_spread = 12.0f;
        effect.effect_falloff = 0.5f;
        effect.effect_color = 0xFFFFFFFFu;
        effect.effect_alpha_aware = true;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::DisplacementMap:
        effect.effect_source_mode = 1;
        effect.effect_amount = 20.0f;
        effect.effect_distance = 20.0f;
        effect.effect_x_channel = 0;
        effect.effect_y_channel = 0;
        effect.effect_wrap_mode = 0;
        effect.effect_mapping_space = 0;
        effect.effect_opacity = 1.0f;
        break;
    case LayerEffectType::FourColorGradient: {
        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty())
            catalog.reload();
        if (const auto *definition = catalog.find(type)) {
            effect.extension_parameters_json = QJsonDocument(definition->defaults)
                .toJson(QJsonDocument::Compact).toStdString();
            effect.extension_schema_version = definition->schemaVersion;
        }
        break;
    }
    case LayerEffectType::BrightnessContrast:
    case LayerEffectType::Saturation:
        break;
    }

    effect.opacity_prop.static_value = effect.effect_opacity;
    effect.size_prop.static_value = effect.effect_size;
    effect.distance_prop.static_value = effect.effect_distance;
    effect.angle_prop.static_value = effect.effect_angle;
    effect.spread_prop.static_value = effect.effect_spread;
    effect.falloff_prop.static_value = effect.effect_falloff;
    effect.amount_prop.static_value = effect.effect_amount;
    effect.scale_prop.static_value = effect.effect_scale;
    effect.softness_prop.static_value = effect.effect_softness;
    effect.roundness_prop.static_value = effect.effect_roundness;
    effect.speed_prop.static_value = effect.effect_speed;
    effect.center_x_prop.static_value = effect.effect_center_x;
    effect.center_y_prop.static_value = effect.effect_center_y;
    effect.complexity_prop.static_value = effect.effect_complexity;
    effect.evolution_prop.static_value = effect.effect_evolution;
    effect.trim_start_prop.static_value = effect.effect_trim_start;
    effect.trim_end_prop.static_value = effect.effect_trim_end;
    effect.trim_offset_prop.static_value = effect.effect_trim_offset;
    set_color_channels(effect, effect.effect_color);
    set_stroke_color_channels(effect, effect.effect_stroke_color);
    set_secondary_color_channels(effect, effect.effect_secondary_color);
    return effect;
}

QString effect_type_id(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::BackgroundColor: return QStringLiteral("background-color");
    case LayerEffectType::Outline: return QStringLiteral("outline");
    case LayerEffectType::DropShadow: return QStringLiteral("drop-shadow");
    case LayerEffectType::LongShadow: return QStringLiteral("long-shadow");
    case LayerEffectType::BrightnessContrast: return QStringLiteral("brightness-contrast");
    case LayerEffectType::Saturation: return QStringLiteral("saturation");
    case LayerEffectType::ColorOverlay: return QStringLiteral("color-overlay");
    case LayerEffectType::Glow: return QStringLiteral("glow");
    case LayerEffectType::InnerGlow: return QStringLiteral("inner-glow");
    case LayerEffectType::InnerShadow: return QStringLiteral("inner-shadow");
    case LayerEffectType::Blur: return QStringLiteral("blur");
    case LayerEffectType::MotionBlur: return QStringLiteral("motion-blur");
    case LayerEffectType::Bloom: return QStringLiteral("bloom");
    case LayerEffectType::Emboss: return QStringLiteral("emboss");
    case LayerEffectType::LensFlare: return QStringLiteral("lens-flare");
    case LayerEffectType::Vignette: return QStringLiteral("vignette");
    case LayerEffectType::Noise: return QStringLiteral("noise");
    case LayerEffectType::Grain: return QStringLiteral("grain");
    case LayerEffectType::RoughenEdges: return QStringLiteral("roughen-edges");
    case LayerEffectType::FourColorGradient: return QStringLiteral("4-color-gradient");
    case LayerEffectType::Sharpen: return QStringLiteral("sharpen");
    case LayerEffectType::UnsharpMask: return QStringLiteral("unsharp-mask");
    case LayerEffectType::HighPass: return QStringLiteral("high-pass");
    case LayerEffectType::Clarity: return QStringLiteral("clarity");
    case LayerEffectType::BilateralSharpen: return QStringLiteral("bilateral-sharpen");
    case LayerEffectType::Glare: return QStringLiteral("glare");
    case LayerEffectType::Halation: return QStringLiteral("halation");
    case LayerEffectType::LensDistortion: return QStringLiteral("lens-distortion");
    case LayerEffectType::ChromaticAberration: return QStringLiteral("chromatic-aberration");
    case LayerEffectType::DirectionalBlur: return QStringLiteral("directional-blur");
    case LayerEffectType::ZoomBlur: return QStringLiteral("zoom-blur");
    case LayerEffectType::RadialBlur: return QStringLiteral("radial-blur");
    case LayerEffectType::Ripple: return QStringLiteral("ripple");
    case LayerEffectType::WaveWarp: return QStringLiteral("wave-warp");
    case LayerEffectType::Pixelate: return QStringLiteral("pixelate");
    case LayerEffectType::EdgeDetect: return QStringLiteral("edge-detect");
    case LayerEffectType::Posterize: return QStringLiteral("posterize");
    case LayerEffectType::Threshold: return QStringLiteral("threshold");
    case LayerEffectType::Scanlines: return QStringLiteral("scanlines");
    case LayerEffectType::ChromaKey: return QStringLiteral("chroma-key");
    case LayerEffectType::LumaKey: return QStringLiteral("luma-key");
    case LayerEffectType::ColorRange: return QStringLiteral("color-range");
    case LayerEffectType::SpillSuppression: return QStringLiteral("spill-suppression");
    case LayerEffectType::MatteChoker: return QStringLiteral("matte-choker");
    case LayerEffectType::LightWrap: return QStringLiteral("light-wrap");
    case LayerEffectType::DisplacementMap: return QStringLiteral("displacement-map");
    case LayerEffectType::FilmDistortion: return QStringLiteral("film-distortion");
    case LayerEffectType::AnalogDistortion: return QStringLiteral("analog-distortion");
    case LayerEffectType::DigitalDistortion: return QStringLiteral("digital-distortion");
    case LayerEffectType::TrimPaths: return QStringLiteral("trim-paths");
    }
    return {};
}

bool effect_type_from_id(const QString &id, LayerEffectType *type)
{
    if (!type)
        return false;
    QString normalized = id.trimmed().toLower();
    normalized.replace(QLatin1Char('_'), QLatin1Char('-'));
    normalized.replace(QLatin1Char(' '), QLatin1Char('-'));
    for (int value = static_cast<int>(LayerEffectType::BackgroundColor);
         value <= static_cast<int>(LayerEffectType::TrimPaths); ++value) {
        const auto candidate = static_cast<LayerEffectType>(value);
        if (effect_type_id(candidate) == normalized) {
            *type = candidate;
            return true;
        }
    }
    return false;
}

QString effect_presets_root_path()
{
    return QString::fromStdString(
        fxm::resolve_asset_path("effect-transitions"));
}

bool load_effect_preset_file(const QString &file_path,
                             EffectPresetDescriptor *descriptor,
                             QString *error_message)
{
    auto fail = [error_message](const QString &message) {
        if (error_message)
            *error_message = message;
        return false;
    };

    const QFileInfo info(file_path);
    if (!is_effect_preset_file_in_library(file_path))
        return fail(QStringLiteral("Effect preset files must be readable .fxmeffect files stored directly in the Effects & Presets library folder."));
    if (info.size() < 0 || info.size() > kMaxPresetFileBytes)
        return fail(QStringLiteral("The effect preset file is too large."));

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("Could not open the effect preset file."));

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.read(kMaxPresetFileBytes + 1), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("The effect preset file is not valid JSON."));

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt(1) != 1)
        return fail(QStringLiteral("Unsupported effect preset version."));
    const QString format = object.value(QStringLiteral("format")).toString();
    if (!format.isEmpty() && format != QStringLiteral("obs-fxm-effect-preset"))
        return fail(QStringLiteral("Unsupported effect preset format."));

    const QString kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("effect"));
    if (kind.compare(QStringLiteral("effect"), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("The preset is not a layer effect."));

    QStringList category_path = fxm_preset_category_path_from_json(object.value(QStringLiteral("category")));
    if (category_path.isEmpty())
        category_path = {QStringLiteral("Effects")};
    if (!valid_effect_category_path(category_path))
        return fail(QStringLiteral("Effect preset categories must begin with Effects or Animation Presets."));

    LayerEffectType type;
    if (!effect_type_from_id(object.value(QStringLiteral("type")).toString(), &type))
        return fail(QStringLiteral("Unknown or missing effect type."));

    if (descriptor) {
        descriptor->file_path = info.absoluteFilePath();
        descriptor->id = object.value(QStringLiteral("id")).toString(effect_type_id(type)).trimmed().left(128);
        if (descriptor->id.isEmpty())
            descriptor->id = effect_type_id(type);
        descriptor->display_name = object.value(QStringLiteral("name")).toString(info.completeBaseName()).trimmed().left(256);
        if (descriptor->display_name.isEmpty())
            descriptor->display_name = info.completeBaseName().left(256);
        descriptor->kind = kind.toLower();
        descriptor->category_path = category_path;
        descriptor->effect = make_default_layer_effect(type);
        if (object.value(QStringLiteral("parameters")).isObject())
            apply_parameter_overrides(descriptor->effect, object.value(QStringLiteral("parameters")).toObject());
    }
    return true;
}

QByteArray encode_effect_preset_mime(const QString &file_path)
{
    return QFileInfo(file_path).absoluteFilePath().toUtf8();
}

QString effect_preset_path_from_mime(const QMimeData *mime_data)
{
    if (!mime_data || !mime_data->hasFormat(QString::fromUtf8(kEffectPresetMimeType)))
        return {};
    const QByteArray payload = mime_data->data(QString::fromUtf8(kEffectPresetMimeType));
    if (payload.isEmpty() || payload.size() > 32768)
        return {};
    return QString::fromUtf8(payload).trimmed();
}

bool mime_has_effect_preset(const QMimeData *mime_data)
{
    const QString path = effect_preset_path_from_mime(mime_data);
    return !path.isEmpty() && is_effect_preset_file_in_library(path);
}

} // namespace fxm::effects
