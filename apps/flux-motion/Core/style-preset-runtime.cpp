#include "style-preset-runtime.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QString>

#include <algorithm>

namespace fxm::style_presets {
namespace {

constexpr int kMaxStylePresets = 4096;

uint32_t parse_argb(const QJsonObject &object, const char *key,
                    uint32_t fallback)
{
    bool ok = false;
    const uint value = object.value(QString::fromUtf8(key))
                           .toString(QString::number(fallback, 16))
                           .toUInt(&ok, 16);
    return ok ? value : fallback;
}

int normalized_gradient_type(int type)
{
    switch (std::clamp(type, 0, 4)) {
    case 1: return 1;
    case 2: return 2;
    case 4: return 1;
    case 0:
    case 3:
    default: return 0;
    }
}

int normalized_gradient_spread(int spread)
{
    return spread == 1 || spread == 2 ? spread : 0;
}

int gradient_spread_from_payload(const QJsonObject &object, int fallback)
{
    if (!object.contains(QStringLiteral("gradientSpread")) &&
        object.value(QStringLiteral("gradientType")).toInt(0) == 3) {
        return 1;
    }
    return normalized_gradient_spread(
        object.value(QStringLiteral("gradientSpread")).toInt(fallback));
}

void apply_gradient_payload(const QJsonObject &object,
                            RichTextCharFormat &format)
{
    format.fill.type = object.value(QStringLiteral("fillType")).toInt(1);
    format.fill.gradient_spread = gradient_spread_from_payload(
        object, format.fill.gradient_spread);
    format.fill.gradient_type = normalized_gradient_type(
        object.value(QStringLiteral("gradientType"))
            .toInt(format.fill.gradient_type));
    format.fill.gradient_start_color = parse_argb(
        object, "startColor", format.fill.gradient_start_color);
    format.fill.gradient_end_color = parse_argb(
        object, "endColor", format.fill.gradient_end_color);
    format.fill.gradient_start_pos = float(
        object.value(QStringLiteral("startPos"))
            .toDouble(format.fill.gradient_start_pos));
    format.fill.gradient_end_pos = float(
        object.value(QStringLiteral("endPos"))
            .toDouble(format.fill.gradient_end_pos));
    format.fill.gradient_start_opacity = float(
        object.value(QStringLiteral("startOpacity"))
            .toDouble(format.fill.gradient_start_opacity));
    format.fill.gradient_end_opacity = float(
        object.value(QStringLiteral("endOpacity"))
            .toDouble(format.fill.gradient_end_opacity));
    format.fill.gradient_opacity = float(
        object.value(QStringLiteral("opacity"))
            .toDouble(format.fill.gradient_opacity));
    format.fill.gradient_angle = float(
        object.value(QStringLiteral("angle"))
            .toDouble(format.fill.gradient_angle));
    format.fill.gradient_center_x = float(
        object.value(QStringLiteral("centerX"))
            .toDouble(format.fill.gradient_center_x));
    format.fill.gradient_center_y = float(
        object.value(QStringLiteral("centerY"))
            .toDouble(format.fill.gradient_center_y));
    format.fill.gradient_scale = float(
        object.value(QStringLiteral("scale"))
            .toDouble(format.fill.gradient_scale));
    format.fill.gradient_focal_x = float(
        object.value(QStringLiteral("focalX"))
            .toDouble(format.fill.gradient_focal_x));
    format.fill.gradient_focal_y = float(
        object.value(QStringLiteral("focalY"))
            .toDouble(format.fill.gradient_focal_y));
    format.fill.color = format.fill.type == 1
        ? format.fill.gradient_start_color
        : parse_argb(object, "textColor", format.fill.color);
}

void apply_stroke_payload(const QJsonObject &object, RichTextStroke &format)
{
    format.enabled = object.value(QStringLiteral("enabled"))
                         .toBool(format.enabled);
    format.width = float(object.value(QStringLiteral("width"))
                             .toDouble(format.width));
    format.opacity = float(object.value(QStringLiteral("opacity"))
                               .toDouble(format.opacity));
    format.on_front = object.value(QStringLiteral("onFront"))
                          .toBool(format.on_front);
    format.alignment = std::clamp(
        object.value(QStringLiteral("alignment")).toInt(format.alignment),
        0, 2);
    format.antialias = object.value(QStringLiteral("antialias"))
                           .toBool(format.antialias);
    format.join_style = std::clamp(
        object.value(QStringLiteral("joinStyle")).toInt(format.join_style),
        0, 2);
    const int fill_type = std::clamp(
        object.value(QStringLiteral("fillType"))
            .toInt(format.fill.type == 1 ? 2 : 1),
        0, 2);
    format.fill.type = fill_type == 2 ? 1 : 0;
    format.fill.color = parse_argb(object, "color", format.fill.color);
    if (object.value(QStringLiteral("gradient")).isObject()) {
        RichTextCharFormat wrapper;
        wrapper.fill = format.fill;
        apply_gradient_payload(
            object.value(QStringLiteral("gradient")).toObject(), wrapper);
        format.fill = wrapper.fill;
        format.fill.type = fill_type == 2 ? 1 : 0;
        if (format.fill.type == 0)
            format.fill.color = parse_argb(object, "color", format.fill.color);
    }
    format.enabled = format.enabled && fill_type != 0 && format.width > 0.0f;
}

QString style_preset_path()
{
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.flux-motion");
    return QDir(base).filePath(QStringLiteral("style-presets/styles.json"));
}

} // namespace

bool apply_text_preset_payload(const QJsonObject &payload,
                               RichTextCharFormat &format)
{
    format.font_family = payload.value(QStringLiteral("fontFamily"))
                             .toString(QString::fromStdString(format.font_family))
                             .toStdString();
    format.font_style = payload.value(QStringLiteral("fontStyle"))
                            .toString(QString::fromStdString(format.font_style))
                            .toStdString();
    format.font_size = std::clamp(
        payload.value(QStringLiteral("fontSize")).toInt(format.font_size),
        1, 4096);
    format.bold = payload.value(QStringLiteral("bold")).toBool(format.bold);
    format.italic = payload.value(QStringLiteral("italic")).toBool(format.italic);
    format.underline = payload.value(QStringLiteral("underline"))
                           .toBool(format.underline);
    format.strikethrough = payload.value(QStringLiteral("strike"))
                               .toBool(format.strikethrough);
    format.kerning = payload.value(QStringLiteral("kerning"))
                         .toBool(format.kerning);
    format.kerning_mode = std::clamp(
        payload.value(QStringLiteral("kerningMode")).toInt(format.kerning_mode),
        0, 2);
    format.manual_kerning = float(
        payload.value(QStringLiteral("manualKerning"))
            .toDouble(format.manual_kerning));
    format.tracking = float(payload.value(QStringLiteral("tracking"))
                                .toDouble(format.tracking));
    format.scale_x = float(std::clamp(
        payload.value(QStringLiteral("scaleX")).toDouble(format.scale_x),
        0.01, 100.0));
    format.scale_y = float(std::clamp(
        payload.value(QStringLiteral("scaleY")).toDouble(format.scale_y),
        0.01, 100.0));
    format.baseline_shift = float(
        payload.value(QStringLiteral("baseline"))
            .toDouble(format.baseline_shift));
    format.text_style = std::clamp(
        payload.value(QStringLiteral("textStyle")).toInt(format.text_style),
        0, 4);
    format.ligatures = payload.value(QStringLiteral("ligatures"))
                           .toBool(format.ligatures);
    format.stylistic_alternates =
        payload.value(QStringLiteral("stylisticAlternates"))
            .toBool(format.stylistic_alternates);
    format.fractions = payload.value(QStringLiteral("fractions"))
                           .toBool(format.fractions);
    format.opentype_features =
        payload.value(QStringLiteral("openTypeFeatures"))
            .toBool(format.opentype_features);
    format.language = payload.value(QStringLiteral("language"))
                          .toString(QString::fromStdString(format.language))
                          .toStdString();
    format.fill.color = parse_argb(payload, "textColor", format.fill.color);
    format.fill.type = std::clamp(
        payload.value(QStringLiteral("fillType")).toInt(format.fill.type),
        0, 1);
    if (payload.contains(QStringLiteral("gradient")))
        apply_gradient_payload(
            payload.value(QStringLiteral("gradient")).toObject(), format);
    if (payload.value(QStringLiteral("stroke")).isObject())
        apply_stroke_payload(
            payload.value(QStringLiteral("stroke")).toObject(), format.stroke);
    return true;
}

uint32_t text_preset_char_mask()
{
    return RichTextCharFontFamily | RichTextCharFontStyle |
           RichTextCharFontSize | RichTextCharBold | RichTextCharItalic |
           RichTextCharUnderline | RichTextCharStrikethrough |
           RichTextCharKerning | RichTextCharTracking | RichTextCharScaleX |
           RichTextCharScaleY | RichTextCharBaselineShift |
           RichTextCharFillColor | RichTextCharTextStyle |
           RichTextCharLigatures | RichTextCharStylisticAlternates |
           RichTextCharFractions | RichTextCharOpenTypeFeatures |
           RichTextCharLanguage | RichTextCharStroke;
}

bool resolve_text_preset(const std::string &preset_id,
                         RichTextCharFormat &format, uint32_t &mask)
{
    QFile file(style_preset_path());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    const QJsonArray presets =
        document.object().value(QStringLiteral("presets")).toArray();
    const int count = std::min<int>(presets.size(), kMaxStylePresets);
    const QString requested_id = QString::fromStdString(preset_id);
    for (int index = 0; index < count; ++index) {
        const QJsonObject preset = presets.at(index).toObject();
        if (preset.value(QStringLiteral("id")).toString() != requested_id ||
            preset.value(QStringLiteral("kind")).toString() !=
                QStringLiteral("text") ||
            !preset.value(QStringLiteral("payload")).isObject()) {
            continue;
        }
        if (!apply_text_preset_payload(
                preset.value(QStringLiteral("payload")).toObject(), format)) {
            return false;
        }
        mask = text_preset_char_mask();
        return true;
    }
    return false;
}

} // namespace fxm::style_presets
