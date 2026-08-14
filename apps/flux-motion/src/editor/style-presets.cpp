#include "style-presets.h"
#include "properties-panel.h"
#include "title-localization.h"
#include "title-serialization-schema.h"
#include "style-preset-runtime.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QAbstractItemView>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QStandardPaths>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUuid>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QBrush>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QConicalGradient>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace obsfxm {
namespace {

constexpr int kStylePresetFileVersion = 4;
constexpr int kMaxStylePresets = 4096;
constexpr int kMaxPresetIdLength = 256;
constexpr int kMaxPresetNameLength = 512;
constexpr int kMaxPresetCategoryLength = 256;

QString kindToString(StylePresetKind kind);
StylePresetKind stringToKind(const QString &value);

QString boundedJsonString(const QJsonObject &object, const char *key,
                          const QString &fallback, int max_length)
{
    const QJsonValue value = object.value(QString::fromUtf8(key));
    if (!value.isString())
        return fallback;
    QString result = value.toString();
    if (result.size() > max_length)
        result.truncate(max_length);
    return result;
}

bool presetFromJson(const QJsonValue &entry, StylePreset *out,
                    const QString &default_category, bool require_id)
{
    if (!out || !entry.isObject())
        return false;
    const QJsonObject object = entry.toObject();
    if (!object.value(QStringLiteral("payload")).isObject())
        return false;

    StylePreset preset;
    preset.id = boundedJsonString(object, "id", QString(), kMaxPresetIdLength);
    preset.name = boundedJsonString(object, "name", QString(), kMaxPresetNameLength).trimmed();
    preset.category = boundedJsonString(object, "category", default_category,
                                        kMaxPresetCategoryLength).trimmed();
    if (preset.category.isEmpty())
        preset.category = default_category;
    preset.kind = stringToKind(boundedJsonString(object, "kind",
                                                  QStringLiteral("text"), 32));
    preset.payload = object.value(QStringLiteral("payload")).toObject();
    if (preset.name.isEmpty() || (require_id && preset.id.isEmpty()))
        return false;
    *out = std::move(preset);
    return true;
}

QJsonObject presetFileRoot(const QList<StylePreset> &presets,
                           std::optional<StylePresetKind> kind_filter = std::nullopt)
{
    QJsonArray array;
    for (const auto &preset : presets) {
        if (kind_filter && preset.kind != *kind_filter)
            continue;
        QJsonObject object;
        object[QStringLiteral("id")] = preset.id;
        object[QStringLiteral("name")] = preset.name;
        object[QStringLiteral("category")] = preset.category;
        object[QStringLiteral("kind")] = kindToString(preset.kind);
        object[QStringLiteral("payload")] = preset.payload;
        array.append(object);
    }
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("OBS-FXM Style Presets");
    root[QStringLiteral("version")] = kStylePresetFileVersion;
    root[QStringLiteral("schema_version")] =
        fxm::serialization::kCurrentFormattingSchemaVersion;
    root[QStringLiteral("development_version")] =
        fxm::serialization::kCurrentDevelopmentVersion;
    root[QStringLiteral("presets")] = array;
    return root;
}

struct PresetChangeListener {
    QPointer<QObject> context;
    std::function<void()> callback;
};

std::vector<PresetChangeListener> &presetChangeListeners()
{
    static std::vector<PresetChangeListener> listeners;
    return listeners;
}

QString kindToString(StylePresetKind kind)
{
    return kind == StylePresetKind::Text ? QStringLiteral("text") : QStringLiteral("gradient");
}

StylePresetKind stringToKind(const QString &value)
{
    return value == QStringLiteral("gradient") ? StylePresetKind::Gradient : StylePresetKind::Text;
}

int normalizedGradientType(int type)
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

int normalizedGradientSpread(int spread)
{
    return spread == 1 || spread == 2 ? spread : 0;
}

int gradientSpreadFromPayload(const QJsonObject &o, int fallback)
{
    if (!o.contains(QStringLiteral("gradientSpread")) && o.value(QStringLiteral("gradientType")).toInt(0) == 3)
        return 1;
    return normalizedGradientSpread(o.value(QStringLiteral("gradientSpread")).toInt(fallback));
}

QJsonObject gradientPayloadFromLayer(const Layer &layer)
{
    QJsonObject o;
    o[QStringLiteral("fillType")] = layer.fill_type;
    o[QStringLiteral("gradientType")] = layer.gradient_type;
    o[QStringLiteral("gradientSpread")] = layer.gradient_spread;
    o[QStringLiteral("startColor")] = QString::number(layer.gradient_start_color, 16);
    o[QStringLiteral("endColor")] = QString::number(layer.gradient_end_color, 16);
    o[QStringLiteral("startPos")] = layer.gradient_start_pos;
    o[QStringLiteral("endPos")] = layer.gradient_end_pos;
    o[QStringLiteral("startOpacity")] = layer.gradient_start_opacity;
    o[QStringLiteral("endOpacity")] = layer.gradient_end_opacity;
    o[QStringLiteral("opacity")] = layer.gradient_opacity;
    o[QStringLiteral("angle")] = layer.gradient_angle;
    o[QStringLiteral("centerX")] = layer.gradient_center_x;
    o[QStringLiteral("centerY")] = layer.gradient_center_y;
    o[QStringLiteral("scale")] = layer.gradient_scale;
    o[QStringLiteral("focalX")] = layer.gradient_focal_x;
    o[QStringLiteral("focalY")] = layer.gradient_focal_y;
    QJsonArray stops;
    for (const auto &stop : layer.gradient_stops) {
        QJsonObject s;
        s[QStringLiteral("color")] = QString::number(stop.color, 16);
        s[QStringLiteral("position")] = stop.position;
        s[QStringLiteral("opacity")] = stop.opacity;
        stops.append(s);
    }
    o[QStringLiteral("stops")] = stops;
    return o;
}

QJsonObject gradientPayloadFromFill(const RichTextFill &fill,
                                    const std::vector<GradientStop> &extra_stops)
{
    QJsonObject o;
    o[QStringLiteral("fillType")] = 1;
    o[QStringLiteral("gradientType")] = fill.gradient_type;
    o[QStringLiteral("gradientSpread")] = fill.gradient_spread;
    o[QStringLiteral("startColor")] = QString::number(fill.gradient_start_color, 16);
    o[QStringLiteral("endColor")] = QString::number(fill.gradient_end_color, 16);
    o[QStringLiteral("startPos")] = fill.gradient_start_pos;
    o[QStringLiteral("endPos")] = fill.gradient_end_pos;
    o[QStringLiteral("startOpacity")] = fill.gradient_start_opacity;
    o[QStringLiteral("endOpacity")] = fill.gradient_end_opacity;
    o[QStringLiteral("opacity")] = fill.gradient_opacity;
    o[QStringLiteral("angle")] = fill.gradient_angle;
    o[QStringLiteral("centerX")] = fill.gradient_center_x;
    o[QStringLiteral("centerY")] = fill.gradient_center_y;
    o[QStringLiteral("scale")] = fill.gradient_scale;
    o[QStringLiteral("focalX")] = fill.gradient_focal_x;
    o[QStringLiteral("focalY")] = fill.gradient_focal_y;
    QJsonArray stops;
    for (const auto &stop : extra_stops) {
        QJsonObject s;
        s[QStringLiteral("color")] = QString::number(stop.color, 16);
        s[QStringLiteral("position")] = stop.position;
        s[QStringLiteral("opacity")] = stop.opacity;
        stops.append(s);
    }
    o[QStringLiteral("stops")] = stops;
    return o;
}

static uint32_t parseArgb(const QJsonObject &o, const char *key, uint32_t fallback)
{
    bool ok = false;
    const uint value = o.value(QString::fromUtf8(key)).toString(QString::number(fallback, 16)).toUInt(&ok, 16);
    return ok ? value : fallback;
}

std::vector<GradientStop> gradientStopsFromPayload(const QJsonObject &o)
{
    std::vector<GradientStop> result;
    const auto stops = o.value(QStringLiteral("stops")).toArray();
    result.reserve(static_cast<size_t>(stops.size()));
    for (const auto &entry : stops) {
        const auto s = entry.toObject();
        GradientStop stop;
        stop.color = parseArgb(s, "color", 0xffffffffu);
        stop.position = std::clamp(
            float(s.value(QStringLiteral("position")).toDouble(0.5)),
            0.0f, 1.0f);
        stop.opacity = std::clamp(
            float(s.value(QStringLiteral("opacity")).toDouble(1.0)),
            0.0f, 1.0f);
        result.push_back(stop);
    }
    return result;
}

bool isTextLikeLayer(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock ||
           layer.type == LayerType::Ticker;
}

QColor colorFromArgb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, (argb >> 24) & 0xff);
}

void applyGradientPayload(const QJsonObject &o, Layer &layer)
{
    layer.fill_type = o.value(QStringLiteral("fillType")).toInt(1);
    layer.gradient_spread = gradientSpreadFromPayload(o, layer.gradient_spread);
    layer.gradient_type = normalizedGradientType(o.value(QStringLiteral("gradientType")).toInt(layer.gradient_type));
    layer.gradient_start_color = parseArgb(o, "startColor", layer.gradient_start_color);
    layer.gradient_end_color = parseArgb(o, "endColor", layer.gradient_end_color);
    layer.gradient_start_pos = float(o.value(QStringLiteral("startPos")).toDouble(layer.gradient_start_pos));
    layer.gradient_end_pos = float(o.value(QStringLiteral("endPos")).toDouble(layer.gradient_end_pos));
    layer.gradient_start_opacity = float(o.value(QStringLiteral("startOpacity")).toDouble(layer.gradient_start_opacity));
    layer.gradient_end_opacity = float(o.value(QStringLiteral("endOpacity")).toDouble(layer.gradient_end_opacity));
    layer.gradient_opacity = float(o.value(QStringLiteral("opacity")).toDouble(layer.gradient_opacity));
    layer.gradient_angle = float(o.value(QStringLiteral("angle")).toDouble(layer.gradient_angle));
    layer.gradient_center_x = float(o.value(QStringLiteral("centerX")).toDouble(layer.gradient_center_x));
    layer.gradient_center_y = float(o.value(QStringLiteral("centerY")).toDouble(layer.gradient_center_y));
    layer.gradient_scale = float(o.value(QStringLiteral("scale")).toDouble(layer.gradient_scale));
    layer.gradient_focal_x = float(o.value(QStringLiteral("focalX")).toDouble(layer.gradient_focal_x));
    layer.gradient_focal_y = float(o.value(QStringLiteral("focalY")).toDouble(layer.gradient_focal_y));
    layer.gradient_stops.clear();
    const auto stops = o.value(QStringLiteral("stops")).toArray();
    for (const auto &entry : stops) {
        const auto s = entry.toObject();
        GradientStop stop;
        stop.color = parseArgb(s, "color", 0xffffffffu);
        stop.position = float(s.value(QStringLiteral("position")).toDouble(0.5));
        stop.opacity = float(s.value(QStringLiteral("opacity")).toDouble(1.0));
        layer.gradient_stops.push_back(stop);
    }
}

void fillFormatFromGradientPayload(const QJsonObject &o, RichTextCharFormat &format)
{
    format.fill.type = o.value(QStringLiteral("fillType")).toInt(1);
    format.fill.gradient_spread = gradientSpreadFromPayload(o, format.fill.gradient_spread);
    format.fill.gradient_type = normalizedGradientType(o.value(QStringLiteral("gradientType")).toInt(format.fill.gradient_type));
    format.fill.gradient_start_color = parseArgb(o, "startColor", format.fill.gradient_start_color);
    format.fill.gradient_end_color = parseArgb(o, "endColor", format.fill.gradient_end_color);
    format.fill.gradient_start_pos = float(o.value(QStringLiteral("startPos")).toDouble(format.fill.gradient_start_pos));
    format.fill.gradient_end_pos = float(o.value(QStringLiteral("endPos")).toDouble(format.fill.gradient_end_pos));
    format.fill.gradient_start_opacity = float(o.value(QStringLiteral("startOpacity")).toDouble(format.fill.gradient_start_opacity));
    format.fill.gradient_end_opacity = float(o.value(QStringLiteral("endOpacity")).toDouble(format.fill.gradient_end_opacity));
    format.fill.gradient_opacity = float(o.value(QStringLiteral("opacity")).toDouble(format.fill.gradient_opacity));
    format.fill.gradient_angle = float(o.value(QStringLiteral("angle")).toDouble(format.fill.gradient_angle));
    format.fill.gradient_center_x = float(o.value(QStringLiteral("centerX")).toDouble(format.fill.gradient_center_x));
    format.fill.gradient_center_y = float(o.value(QStringLiteral("centerY")).toDouble(format.fill.gradient_center_y));
    format.fill.gradient_scale = float(o.value(QStringLiteral("scale")).toDouble(format.fill.gradient_scale));
    format.fill.gradient_focal_x = float(o.value(QStringLiteral("focalX")).toDouble(format.fill.gradient_focal_x));
    format.fill.gradient_focal_y = float(o.value(QStringLiteral("focalY")).toDouble(format.fill.gradient_focal_y));
    format.fill.gradient_stops = gradientStopsFromPayload(o);
    format.fill.color = format.fill.type == 1 ? format.fill.gradient_start_color
                                               : parseArgb(o, "textColor", format.fill.color);
}

void fillFromGradientPayload(const QJsonObject &o,
                             RichTextFill &fill,
                             std::vector<GradientStop> *extra_stops)
{
    fill.type = 1;
    fill.gradient_spread = gradientSpreadFromPayload(o, fill.gradient_spread);
    fill.gradient_type = normalizedGradientType(o.value(QStringLiteral("gradientType")).toInt(fill.gradient_type));
    fill.gradient_start_color = parseArgb(o, "startColor", fill.gradient_start_color);
    fill.gradient_end_color = parseArgb(o, "endColor", fill.gradient_end_color);
    fill.gradient_start_pos = float(o.value(QStringLiteral("startPos")).toDouble(fill.gradient_start_pos));
    fill.gradient_end_pos = float(o.value(QStringLiteral("endPos")).toDouble(fill.gradient_end_pos));
    fill.gradient_start_opacity = float(o.value(QStringLiteral("startOpacity")).toDouble(fill.gradient_start_opacity));
    fill.gradient_end_opacity = float(o.value(QStringLiteral("endOpacity")).toDouble(fill.gradient_end_opacity));
    fill.gradient_opacity = float(o.value(QStringLiteral("opacity")).toDouble(fill.gradient_opacity));
    fill.gradient_angle = float(o.value(QStringLiteral("angle")).toDouble(fill.gradient_angle));
    fill.gradient_center_x = float(o.value(QStringLiteral("centerX")).toDouble(fill.gradient_center_x));
    fill.gradient_center_y = float(o.value(QStringLiteral("centerY")).toDouble(fill.gradient_center_y));
    fill.gradient_scale = float(o.value(QStringLiteral("scale")).toDouble(fill.gradient_scale));
    fill.gradient_focal_x = float(o.value(QStringLiteral("focalX")).toDouble(fill.gradient_focal_x));
    fill.gradient_focal_y = float(o.value(QStringLiteral("focalY")).toDouble(fill.gradient_focal_y));
    fill.gradient_stops = gradientStopsFromPayload(o);
    fill.color = fill.gradient_start_color;

    if (extra_stops)
        *extra_stops = fill.gradient_stops;
}

QJsonObject strokePayloadFromLayer(const Layer &layer)
{
    RichTextFill fill;
    fill.type = layer.stroke_fill_type == 2 ? 1 : 0;
    fill.color = layer.stroke_color;
    fill.gradient_type = layer.stroke_gradient_type;
    fill.gradient_spread = layer.stroke_gradient_spread;
    fill.gradient_start_color = layer.stroke_gradient_start_color;
    fill.gradient_end_color = layer.stroke_gradient_end_color;
    fill.gradient_start_pos = layer.stroke_gradient_start_pos;
    fill.gradient_end_pos = layer.stroke_gradient_end_pos;
    fill.gradient_start_opacity = layer.stroke_gradient_start_opacity;
    fill.gradient_end_opacity = layer.stroke_gradient_end_opacity;
    fill.gradient_opacity = layer.stroke_gradient_opacity;
    fill.gradient_angle = layer.stroke_gradient_angle;
    fill.gradient_center_x = layer.stroke_gradient_center_x;
    fill.gradient_center_y = layer.stroke_gradient_center_y;
    fill.gradient_scale = layer.stroke_gradient_scale;
    fill.gradient_focal_x = layer.stroke_gradient_focal_x;
    fill.gradient_focal_y = layer.stroke_gradient_focal_y;

    QJsonObject stroke;
    stroke[QStringLiteral("enabled")] = layer.outline_enabled;
    stroke[QStringLiteral("width")] = layer.stroke_width;
    stroke[QStringLiteral("offset")] = layer.stroke_offset;
    stroke[QStringLiteral("opacity")] = layer.outline_opacity;
    stroke[QStringLiteral("onFront")] = layer.outline_on_front;
    stroke[QStringLiteral("alignment")] = layer.outline_alignment;
    stroke[QStringLiteral("antialias")] = layer.outline_antialias;
    stroke[QStringLiteral("joinStyle")] = layer.outline_join_style;
    stroke[QStringLiteral("fillType")] = layer.stroke_fill_type;
    stroke[QStringLiteral("color")] = QString::number(layer.stroke_color, 16);
    stroke[QStringLiteral("gradient")] = gradientPayloadFromFill(fill, layer.stroke_gradient_stops);
    return stroke;
}

void applyStrokePayloadToLayer(const QJsonObject &stroke, Layer &layer)
{
    layer.outline_enabled = stroke.value(QStringLiteral("enabled")).toBool(layer.outline_enabled);
    layer.stroke_width = float(stroke.value(QStringLiteral("width")).toDouble(layer.stroke_width));
    layer.stroke_offset = float(stroke.value(QStringLiteral("offset")).toDouble(layer.stroke_offset));
    layer.stroke_offset_prop.static_value = layer.stroke_offset;
    layer.outline_opacity = float(stroke.value(QStringLiteral("opacity")).toDouble(layer.outline_opacity));
    layer.outline_on_front = stroke.value(QStringLiteral("onFront")).toBool(layer.outline_on_front);
    layer.outline_alignment = std::clamp(stroke.value(QStringLiteral("alignment")).toInt(layer.outline_alignment), 0, 2);
    layer.outline_antialias = stroke.value(QStringLiteral("antialias")).toBool(layer.outline_antialias);
    layer.outline_join_style = std::clamp(stroke.value(QStringLiteral("joinStyle")).toInt(layer.outline_join_style), 0, 2);
    layer.stroke_fill_type = std::clamp(stroke.value(QStringLiteral("fillType")).toInt(layer.stroke_fill_type), 0, 2);
    layer.stroke_color = parseArgb(stroke, "color", layer.stroke_color);
    layer.stroke_color_a.static_value = (layer.stroke_color >> 24) & 0xFF;
    layer.stroke_color_r.static_value = (layer.stroke_color >> 16) & 0xFF;
    layer.stroke_color_g.static_value = (layer.stroke_color >> 8) & 0xFF;
    layer.stroke_color_b.static_value = layer.stroke_color & 0xFF;

    if (stroke.value(QStringLiteral("gradient")).isObject()) {
        RichTextFill fill;
        fill.color = layer.stroke_color;
        fill.gradient_type = layer.stroke_gradient_type;
        fill.gradient_spread = layer.stroke_gradient_spread;
        fill.gradient_start_color = layer.stroke_gradient_start_color;
        fill.gradient_end_color = layer.stroke_gradient_end_color;
        fill.gradient_start_pos = layer.stroke_gradient_start_pos;
        fill.gradient_end_pos = layer.stroke_gradient_end_pos;
        fill.gradient_start_opacity = layer.stroke_gradient_start_opacity;
        fill.gradient_end_opacity = layer.stroke_gradient_end_opacity;
        fill.gradient_opacity = layer.stroke_gradient_opacity;
        fill.gradient_angle = layer.stroke_gradient_angle;
        fill.gradient_center_x = layer.stroke_gradient_center_x;
        fill.gradient_center_y = layer.stroke_gradient_center_y;
        fill.gradient_scale = layer.stroke_gradient_scale;
        fill.gradient_focal_x = layer.stroke_gradient_focal_x;
        fill.gradient_focal_y = layer.stroke_gradient_focal_y;
        std::vector<GradientStop> stops;
        fillFromGradientPayload(stroke.value(QStringLiteral("gradient")).toObject(), fill, &stops);
        layer.stroke_gradient_type = fill.gradient_type;
        layer.stroke_gradient_spread = fill.gradient_spread;
        layer.stroke_gradient_start_color = fill.gradient_start_color;
        layer.stroke_gradient_end_color = fill.gradient_end_color;
        layer.stroke_gradient_start_pos = fill.gradient_start_pos;
        layer.stroke_gradient_end_pos = fill.gradient_end_pos;
        layer.stroke_gradient_start_opacity = fill.gradient_start_opacity;
        layer.stroke_gradient_end_opacity = fill.gradient_end_opacity;
        layer.stroke_gradient_opacity = fill.gradient_opacity;
        layer.stroke_gradient_angle = fill.gradient_angle;
        layer.stroke_gradient_center_x = fill.gradient_center_x;
        layer.stroke_gradient_center_y = fill.gradient_center_y;
        layer.stroke_gradient_scale = fill.gradient_scale;
        layer.stroke_gradient_focal_x = fill.gradient_focal_x;
        layer.stroke_gradient_focal_y = fill.gradient_focal_y;
        layer.stroke_gradient_stops = std::move(stops);
    }
    layer.outline_enabled = layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f;
}

void strokeFormatFromPayload(const QJsonObject &stroke, RichTextStroke &format)
{
    format.enabled = stroke.value(QStringLiteral("enabled")).toBool(format.enabled);
    format.width = float(stroke.value(QStringLiteral("width")).toDouble(format.width));
    format.opacity = float(stroke.value(QStringLiteral("opacity")).toDouble(format.opacity));
    format.on_front = stroke.value(QStringLiteral("onFront")).toBool(format.on_front);
    format.alignment = std::clamp(stroke.value(QStringLiteral("alignment")).toInt(format.alignment), 0, 2);
    format.antialias = stroke.value(QStringLiteral("antialias")).toBool(format.antialias);
    format.join_style = std::clamp(stroke.value(QStringLiteral("joinStyle")).toInt(format.join_style), 0, 2);
    const int fill_type = std::clamp(stroke.value(QStringLiteral("fillType")).toInt(format.fill.type == 1 ? 2 : 1), 0, 2);
    format.fill.type = fill_type == 2 ? 1 : 0;
    format.fill.color = parseArgb(stroke, "color", format.fill.color);
    if (stroke.value(QStringLiteral("gradient")).isObject()) {
        RichTextCharFormat wrapper;
        wrapper.fill = format.fill;
        fillFormatFromGradientPayload(stroke.value(QStringLiteral("gradient")).toObject(), wrapper);
        format.fill = wrapper.fill;
        format.fill.type = fill_type == 2 ? 1 : 0;
        if (format.fill.type == 0)
            format.fill.color = parseArgb(stroke, "color", format.fill.color);
    }
    format.enabled = format.enabled && fill_type != 0 && format.width > 0.0f;
}

QColor gradientStopColor(uint32_t argb, double stop_opacity, double overall_opacity)
{
    QColor color = colorFromArgb(argb);
    color.setAlphaF(std::clamp(color.alphaF() * stop_opacity * overall_opacity, 0.0, 1.0));
    return color;
}

void drawCheckerboard(QPainter &painter, const QRect &rect)
{
    constexpr int cell = 6;
    const QColor light(214, 214, 214);
    const QColor dark(164, 164, 164);
    for (int y = rect.top(); y <= rect.bottom(); y += cell) {
        for (int x = rect.left(); x <= rect.right(); x += cell) {
            const bool alternate = (((x - rect.left()) / cell) + ((y - rect.top()) / cell)) % 2;
            painter.fillRect(QRect(x, y, cell, cell).intersected(rect), alternate ? dark : light);
        }
    }
}

static void setColorButtonValue(QPushButton *button, uint32_t argb)
{
    if (!button) return;
    button->setProperty("styleArgb", QVariant::fromValue<qulonglong>(argb));
    const QColor color = colorFromArgb(argb);
    const QColor border = color.lightness() < 128 ? color.lighter(170) : color.darker(170);
    button->setText(color.name(QColor::HexArgb));
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;padding:3px 8px;}"
        "QPushButton:hover{border:2px solid %3;}")
        .arg(color.name(QColor::HexArgb),
             color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black"),
             border.name(QColor::HexRgb)));
}

static uint32_t colorButtonValue(const QPushButton *button, uint32_t fallback)
{
    if (!button) return fallback;
    bool ok = false;
    const qulonglong value = button->property("styleArgb").toULongLong(&ok);
    return ok ? static_cast<uint32_t>(value) : fallback;
}

static QBrush previewGradientBrush(int type, const QRectF &bounds,
                                   uint32_t start_argb, uint32_t end_argb,
                                   double angle)
{
    const QColor start = colorFromArgb(start_argb);
    const QColor end = colorFromArgb(end_argb);
    if (type == 1) {
        QRadialGradient gradient(bounds.center(), std::max(1.0, std::min(bounds.width(), bounds.height()) * 0.55));
        gradient.setColorAt(0.0, start);
        gradient.setColorAt(1.0, end);
        return QBrush(gradient);
    }
    if (type == 2) {
        QConicalGradient gradient(bounds.center(), -angle);
        gradient.setColorAt(0.0, start);
        gradient.setColorAt(1.0, end);
        return QBrush(gradient);
    }
    constexpr double kPi = 3.14159265358979323846;
    const double radians = angle * kPi / 180.0;
    const QPointF direction(std::cos(radians), std::sin(radians));
    const double half = 0.5 * std::hypot(bounds.width(), bounds.height());
    QLinearGradient gradient(bounds.center() - direction * half,
                             bounds.center() + direction * half);
    gradient.setColorAt(0.0, start);
    gradient.setColorAt(1.0, end);
    return QBrush(gradient);
}

class TextStylePreview final : public QWidget {
public:
    explicit TextStylePreview(const Layer *layer, QWidget *parent = nullptr)
        : QWidget(parent), layer_(layer)
    {
        setMinimumHeight(180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSample(const QString &sample)
    {
        sample_ = sample.isEmpty() ? QStringLiteral("Broadcast Graphics  Aa 123") : sample;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        const QRectF frame = QRectF(rect()).adjusted(10, 10, -10, -10);
        painter.setPen(palette().color(QPalette::Mid));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame, 4, 4);
        if (!layer_) return;

        QString text = sample_;
        if (layer_->text_style == 1)
            text = text.toUpper();

        QFont font(QString::fromStdString(layer_->font_family));
        if (!layer_->font_style.empty())
            font.setStyleName(QString::fromStdString(layer_->font_style));
        const double preview_size = std::clamp(static_cast<double>(layer_->font_size), 28.0, 144.0);
        font.setPixelSize(static_cast<int>(std::lround(preview_size)));
        font.setBold(layer_->font_bold);
        font.setItalic(layer_->font_italic);
        font.setUnderline(false);
        font.setStrikeOut(false);
        if (layer_->text_style == 2)
            font.setCapitalization(QFont::SmallCaps);
        if (layer_->text_style == 3 || layer_->text_style == 4)
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * 0.65))));

        const QFontMetricsF metrics(font);
        QPainterPath path;
        double cursor = 0.0;
        const double preview_tracking = std::clamp(static_cast<double>(layer_->char_tracking), -80.0, 160.0);
        for (int i = 0; i < text.size(); ++i) {
            const QString ch(text.at(i));
            path.addText(QPointF(cursor, metrics.ascent()), font, ch);
            cursor += metrics.horizontalAdvance(ch) + preview_tracking;
        }
        if (path.isEmpty()) return;

        QTransform glyph_scale;
        glyph_scale.scale(std::clamp(static_cast<double>(layer_->char_scale_x), 0.1, 5.0),
                          std::clamp(static_cast<double>(layer_->char_scale_y), 0.1, 5.0));
        path = glyph_scale.map(path);
        QRectF bounds = path.boundingRect();
        const double available_scale = std::min(
            1.0, std::min((frame.width() - 20.0) / std::max(1.0, bounds.width()),
                          (frame.height() - 20.0) / std::max(1.0, bounds.height())));
        if (available_scale < 1.0) {
            QTransform fit;
            fit.scale(available_scale, available_scale);
            path = fit.map(path);
            bounds = path.boundingRect();
        }
        const QPointF target = frame.center() - bounds.center() +
            QPointF(0.0, -layer_->baseline_shift * std::min(1.0, available_scale));
        path.translate(target);
        bounds = path.boundingRect();

        const QBrush fill_brush = layer_->fill_type == 1
            ? previewGradientBrush(layer_->gradient_type, bounds,
                                   layer_->gradient_start_color,
                                   layer_->gradient_end_color,
                                   layer_->gradient_angle)
            : QBrush(colorFromArgb(layer_->text_color));
        const QBrush stroke_brush = layer_->stroke_fill_type == 2
            ? previewGradientBrush(layer_->stroke_gradient_type, bounds,
                                   layer_->stroke_gradient_start_color,
                                   layer_->stroke_gradient_end_color,
                                   layer_->stroke_gradient_angle)
            : QBrush(colorFromArgb(layer_->stroke_color));

        QPen stroke_pen(stroke_brush,
                        std::max(0.1, static_cast<double>(layer_->stroke_width)));
        stroke_pen.setJoinStyle(layer_->outline_join_style == 1 ? Qt::RoundJoin
                                : layer_->outline_join_style == 2 ? Qt::BevelJoin
                                                                  : Qt::MiterJoin);
        QColor stroke_color = stroke_brush.color();
        const double stroke_alpha = std::clamp(
            static_cast<double>(stroke_color.alphaF()) *
                static_cast<double>(layer_->outline_opacity),
            0.0, 1.0);
        stroke_color.setAlphaF(stroke_alpha);
        if (layer_->stroke_fill_type != 2)
            stroke_pen.setColor(stroke_color);

        const bool draw_stroke = layer_->outline_enabled &&
            layer_->stroke_fill_type != 0 && layer_->stroke_width > 0.0f;
        if (draw_stroke && !layer_->outline_on_front) {
            painter.setPen(stroke_pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill_brush);
        painter.drawPath(path);
        if (draw_stroke && layer_->outline_on_front) {
            painter.setPen(stroke_pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        painter.setPen(QPen(fill_brush, std::max(1.0, preview_size / 18.0)));
        if (layer_->text_underline)
            painter.drawLine(QPointF(bounds.left(), bounds.bottom() + 3.0),
                             QPointF(bounds.right(), bounds.bottom() + 3.0));
        if (layer_->text_strikethrough)
            painter.drawLine(QPointF(bounds.left(), bounds.center().y()),
                             QPointF(bounds.right(), bounds.center().y()));
    }

private:
    const Layer *layer_ = nullptr;
    QString sample_ = QStringLiteral("Broadcast Graphics  Aa 123");
};

class TextStyleEditDialog final : public QDialog {
public:
    explicit TextStyleEditDialog(const StylePreset &preset, QWidget *parent = nullptr)
        : QDialog(parent), original_(preset), layer_(std::make_shared<Layer>())
    {
        layer_->id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        layer_->name = "Text style preview";
        layer_->type = LayerType::Text;
        layer_->text_content = default_sample_.toUtf8().toStdString();
        StylePresetLibrary::applyTextPreset(preset, *layer_);
        layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);

        setWindowTitle(QStringLiteral("Edit Text Style"));
        resize(1360, 880);
        setMinimumSize(1060, 700);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(7);

        auto *identity_host = new QWidget(this);
        auto *identity = new QGridLayout(identity_host);
        identity->setContentsMargins(0, 0, 0, 0);
        identity->setHorizontalSpacing(8);
        identity->setVerticalSpacing(5);
        name_ = new QLineEdit(preset.name, identity_host);
        category_ = new QLineEdit(preset.category, identity_host);
        sample_ = new QLineEdit(default_sample_, identity_host);
        identity->addWidget(new QLabel(QStringLiteral("Name"), identity_host), 0, 0);
        identity->addWidget(name_, 0, 1);
        identity->addWidget(new QLabel(QStringLiteral("Category"), identity_host), 0, 2);
        identity->addWidget(category_, 0, 3);
        identity->addWidget(new QLabel(QStringLiteral("Preview text"), identity_host), 1, 0);
        identity->addWidget(sample_, 1, 1, 1, 3);
        identity->setColumnStretch(1, 1);
        identity->setColumnStretch(3, 1);
        root->addWidget(identity_host);

        auto *split = new QSplitter(Qt::Horizontal, this);
        split->setChildrenCollapsible(false);
        properties_ = new PropertiesPanel(split);
        properties_->setMinimumWidth(390);
        properties_->set_layer(layer_, 0.0);
        properties_->set_text_style_editor_mode(true);

        auto *preview_host = new QWidget(split);
        auto *preview_layout = new QVBoxLayout(preview_host);
        preview_layout->setContentsMargins(0, 0, 0, 0);
        preview_layout->setSpacing(4);
        auto *preview_label = new QLabel(QStringLiteral("Preview"), preview_host);
        preview_ = new TextStylePreview(layer_.get(), preview_host);
        preview_->setMinimumSize(560, 480);
        preview_->setSample(default_sample_);
        preview_layout->addWidget(preview_label);
        preview_layout->addWidget(preview_, 1);

        split->addWidget(properties_);
        split->addWidget(preview_host);
        split->setStretchFactor(0, 2);
        split->setStretchFactor(1, 3);
        split->setSizes({470, 820});
        root->addWidget(split, 1);

        connect(sample_, &QLineEdit::textChanged, this, [this](const QString &text) {
            preview_->setSample(text);
        });
        connect(properties_, &PropertiesPanel::property_changed, this,
                [this](bool push_undo_snapshot) {
                    properties_->set_text_style_editor_mode(true);
                    preview_->update();
                    if (push_undo_snapshot)
                        pushHistorySnapshot();
                });
        connect(properties_, &PropertiesPanel::undo_requested, this, [this]() {
            restoreHistory(history_index_ - 1);
        });
        connect(properties_, &PropertiesPanel::redo_requested, this, [this]() {
            restoreHistory(history_index_ + 1);
        });
        history_.push_back(currentPayload());
        history_index_ = 0;
        updateHistoryButtons();

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (name_->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Edit Text Style"),
                                     QStringLiteral("The style name cannot be empty."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

    StylePreset editedPreset() const
    {
        StylePreset edited = StylePresetLibrary::makeTextPreset(
            *layer_, name_->text().trimmed(),
            category_->text().trimmed().isEmpty() ? QStringLiteral("User")
                                                   : category_->text().trimmed());
        edited.id = original_.id;
        return edited;
    }

private:
    QJsonObject currentPayload() const
    {
        return StylePresetLibrary::makeTextPreset(*layer_, QStringLiteral("Snapshot"),
                                                   QStringLiteral("Internal")).payload;
    }

    void pushHistorySnapshot()
    {
        if (restoring_history_)
            return;
        const QJsonObject snapshot = currentPayload();
        if (history_index_ >= 0 && history_index_ < static_cast<int>(history_.size()) &&
            history_[history_index_] == snapshot) {
            updateHistoryButtons();
            return;
        }
        if (history_index_ + 1 < static_cast<int>(history_.size()))
            history_.erase(history_.begin() + history_index_ + 1, history_.end());
        history_.push_back(snapshot);
        if (history_.size() > 128)
            history_.erase(history_.begin());
        history_index_ = static_cast<int>(history_.size()) - 1;
        updateHistoryButtons();
    }

    void restoreHistory(int index)
    {
        if (index < 0 || index >= static_cast<int>(history_.size()) || index == history_index_)
            return;
        restoring_history_ = true;
        StylePreset snapshot;
        snapshot.kind = StylePresetKind::Text;
        snapshot.payload = history_[index];
        StylePresetLibrary::applyTextPreset(snapshot, *layer_);
        layer_->text_content = sample_->text().toUtf8().toStdString();
        layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);
        history_index_ = index;
        properties_->set_layer(layer_, 0.0);
        properties_->set_text_style_editor_mode(true);
        preview_->update();
        restoring_history_ = false;
        updateHistoryButtons();
    }

    void updateHistoryButtons()
    {
        properties_->set_undo_redo_available(history_index_ > 0,
                                              history_index_ + 1 < static_cast<int>(history_.size()));
    }

    const QString default_sample_ = QStringLiteral("Broadcast Graphics  Aa 123");
    StylePreset original_;
    std::shared_ptr<Layer> layer_;
    PropertiesPanel *properties_ = nullptr;
    TextStylePreview *preview_ = nullptr;
    QLineEdit *name_ = nullptr;
    QLineEdit *category_ = nullptr;
    QLineEdit *sample_ = nullptr;
    std::vector<QJsonObject> history_;
    int history_index_ = -1;
    bool restoring_history_ = false;
};
}

StylePresetLibrary::StylePresetLibrary()
{
    load();
}

QString StylePresetLibrary::storagePath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.flux-motion");
    QDir dir(base);
    dir.mkpath(QStringLiteral("style-presets"));
    return dir.filePath(QStringLiteral("style-presets/styles.json"));
}

bool StylePresetLibrary::load()
{
    presets_.clear();
    QFile file(storagePath());
    if (!file.exists()) {
        ensureDefaults();
        save();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        ensureDefaults();
        return false;
    }
    QJsonParseError parse_error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        ensureDefaults();
        return false;
    }
    const QJsonValue presets_value = doc.object().value(QStringLiteral("presets"));
    if (!presets_value.isArray()) {
        ensureDefaults();
        return false;
    }
    const auto array = presets_value.toArray();
    const int count = static_cast<int>(std::min<qsizetype>(array.size(), kMaxStylePresets));
    for (int index = 0; index < count; ++index) {
        StylePreset preset;
        if (presetFromJson(array.at(index), &preset, QStringLiteral("Default"), true))
            presets_.append(std::move(preset));
    }
    ensureDefaults();
    return true;
}

bool StylePresetLibrary::save() const
{
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray data = QJsonDocument(presetFileRoot(presets_)).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool StylePresetLibrary::importFromFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parse_error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject() ||
        !doc.object().value(QStringLiteral("presets")).isArray()) {
        if (error)
            *error = parse_error.error == QJsonParseError::NoError
                         ? QStringLiteral("Invalid style preset document")
                         : parse_error.errorString();
        return false;
    }
    QSet<QString> used_ids;
    for (const auto &preset : presets_)
        used_ids.insert(preset.id);
    const int original_count = presets_.size();
    const auto array = doc.object().value(QStringLiteral("presets")).toArray();
    const qsizetype remaining_capacity =
        std::max<qsizetype>(0, kMaxStylePresets - presets_.size());
    const int count = static_cast<int>(
        std::min<qsizetype>(array.size(), remaining_capacity));
    int imported = 0;
    for (int index = 0; index < count; ++index) {
        StylePreset preset;
        if (!presetFromJson(array.at(index), &preset, QStringLiteral("Imported"), false))
            continue;
        if (preset.id.isEmpty() || used_ids.contains(preset.id))
            preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        used_ids.insert(preset.id);
        presets_.append(std::move(preset));
        ++imported;
    }
    if (imported == 0) {
        if (error)
            *error = QStringLiteral("The file does not contain any valid style presets");
        return false;
    }
    const bool saved = save();
    if (!saved) {
        while (presets_.size() > original_count)
            presets_.removeLast();
        if (error)
            *error = QStringLiteral("Could not save the imported style presets");
        return false;
    }
    notifyChanged();
    return true;
}

bool StylePresetLibrary::exportToFile(const QString &path, StylePresetKind kind, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(presetFileRoot(presets_, kind)).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QList<StylePreset> StylePresetLibrary::presets(StylePresetKind kind) const
{
    QList<StylePreset> out;
    for (const auto &p : presets_) if (p.kind == kind) out.append(p);
    return out;
}

bool StylePresetLibrary::findById(const QString &id, StylePreset *out) const
{
    for (const auto &preset : presets_) {
        if (preset.id == id) {
            if (out) *out = preset;
            return true;
        }
    }
    return false;
}

QString StylePresetLibrary::displayNameForId(const QString &id) const
{
    StylePreset preset;
    return findById(id, &preset) ? preset.name : QStringLiteral("Missing preset (%1)").arg(id);
}

QStringList StylePresetLibrary::categories(StylePresetKind kind) const
{
    QStringList out;
    for (const auto &p : presets_) if (p.kind == kind && !out.contains(p.category)) out.append(p.category);
    out.sort(Qt::CaseInsensitive);
    return out;
}

void StylePresetLibrary::upsert(const StylePreset &preset)
{
    for (auto &p : presets_) {
        if (p.id == preset.id) {
            p = preset;
            save();
            notifyChanged();
            return;
        }
    }
    presets_.append(preset);
    save();
    notifyChanged();
}

bool StylePresetLibrary::remove(const QString &id)
{
    for (int i = 0; i < presets_.size(); ++i) {
        if (presets_[i].id == id) {
            presets_.removeAt(i);
            save();
            notifyChanged();
            return true;
        }
    }
    return false;
}

StylePreset StylePresetLibrary::makeTextPreset(const Layer &layer, const QString &name, const QString &category)
{
    /* Presets are serialized from the canonical document defaults, never from
     * possibly stale compatibility mirrors. A copy keeps this const operation
     * side-effect free while still migrating legacy titles on read. */
    Layer canonical_layer = layer;
    if (isTextLikeLayer(canonical_layer))
        rich_text_document_ensure_canonical(canonical_layer);
    const Layer &source = canonical_layer;

    StylePreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.category = category.isEmpty() ? QStringLiteral("User") : category;
    p.kind = StylePresetKind::Text;
    QJsonObject o;
    o[QStringLiteral("fontFamily")] = QString::fromStdString(source.font_family);
    o[QStringLiteral("fontStyle")] = QString::fromStdString(source.font_style);
    o[QStringLiteral("fontSize")] = source.font_size;
    o[QStringLiteral("bold")] = source.font_bold;
    o[QStringLiteral("italic")] = source.font_italic;
    o[QStringLiteral("underline")] = source.text_underline;
    o[QStringLiteral("strike")] = source.text_strikethrough;
    o[QStringLiteral("kerning")] = source.font_kerning;
    o[QStringLiteral("kerningMode")] = source.kerning_mode;
    o[QStringLiteral("manualKerning")] = source.manual_kerning;
    o[QStringLiteral("tracking")] = source.char_tracking;
    o[QStringLiteral("scaleX")] = source.char_scale_x;
    o[QStringLiteral("scaleY")] = source.char_scale_y;
    o[QStringLiteral("baseline")] = source.baseline_shift;
    o[QStringLiteral("leading")] = source.text_leading;
    o[QStringLiteral("textStyle")] = source.text_style;
    o[QStringLiteral("ligatures")] = source.text_ligatures;
    o[QStringLiteral("stylisticAlternates")] = source.text_stylistic_alternates;
    o[QStringLiteral("fractions")] = source.text_fractions;
    o[QStringLiteral("openTypeFeatures")] = source.text_opentype_features;
    o[QStringLiteral("language")] = QString::fromStdString(source.text_language);
    o[QStringLiteral("textColor")] = QString::number(source.text_color, 16);
    o[QStringLiteral("alignH")] = source.align_h;
    o[QStringLiteral("alignV")] = source.align_v;
    o[QStringLiteral("paragraphBefore")] = source.paragraph_space_before;
    o[QStringLiteral("paragraphAfter")] = source.paragraph_space_after;
    o[QStringLiteral("paragraphLeft")] = source.paragraph_indent_left;
    o[QStringLiteral("paragraphRight")] = source.paragraph_indent_right;
    o[QStringLiteral("paragraphFirst")] = source.paragraph_indent_first_line;
    o[QStringLiteral("paragraphHyphenate")] = source.paragraph_hyphenate;
    o[QStringLiteral("overflowMode")] = source.text_overflow_mode;
    o[QStringLiteral("fitMinScale")] = source.text_fit_min_scale;
    o[QStringLiteral("boxWidthToText")] = source.text_box_width_to_text;
    o[QStringLiteral("boxHeightToText")] = source.text_box_height_to_text;
    o[QStringLiteral("maxBoxWidth")] = source.max_text_box_width;
    o[QStringLiteral("maxBoxHeight")] = source.max_text_box_height;
    o[QStringLiteral("maxBoxWidthOverridden")] = source.max_text_box_width_overridden;
    o[QStringLiteral("maxBoxHeightOverridden")] = source.max_text_box_height_overridden;
    o[QStringLiteral("fillType")] = source.fill_type;
    o[QStringLiteral("gradient")] = gradientPayloadFromLayer(source);
    o[QStringLiteral("stroke")] = strokePayloadFromLayer(source);
    p.payload = o;
    return p;
}

StylePreset StylePresetLibrary::makeGradientPreset(const Layer &layer, const QString &name, const QString &category)
{
    Layer canonical_layer = layer;
    if (isTextLikeLayer(canonical_layer))
        rich_text_document_ensure_canonical(canonical_layer);
    StylePreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.category = category.isEmpty() ? QStringLiteral("User") : category;
    p.kind = StylePresetKind::Gradient;
    p.payload = gradientPayloadFromLayer(canonical_layer);
    p.payload[QStringLiteral("fillType")] = 1;
    return p;
}

StylePreset StylePresetLibrary::makeGradientPreset(const RichTextFill &fill,
                                                   const std::vector<GradientStop> &extra_stops,
                                                   const QString &name,
                                                   const QString &category)
{
    StylePreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.category = category.isEmpty() ? QStringLiteral("User") : category;
    p.kind = StylePresetKind::Gradient;
    p.payload = gradientPayloadFromFill(fill, extra_stops);
    return p;
}

bool StylePresetLibrary::applyTextPreset(const StylePreset &preset, Layer &layer)
{
    if (preset.kind != StylePresetKind::Text || !isTextLikeLayer(layer))
        return false;

    rich_text_document_ensure_canonical(layer);
    RichTextCharFormat character = layer.rich_text.default_format;
    RichTextParagraphFormat paragraph = layer.rich_text.default_paragraph_format;
    if (!textPresetToCharFormat(preset, character) ||
        !textPresetToParagraphFormat(preset, paragraph)) {
        return false;
    }

    const QJsonObject o = preset.payload;
    /* Preserve arbitrary gradient-stop arrays in their existing compatibility
     * storage. The authored fill/stroke fields themselves are committed only to
     * RichTextDocument below and then mirrored back to Layer. */
    if (o.contains(QStringLiteral("gradient")))
        applyGradientPayload(o.value(QStringLiteral("gradient")).toObject(), layer);
    if (o.value(QStringLiteral("stroke")).isObject())
        applyStrokePayloadToLayer(o.value(QStringLiteral("stroke")).toObject(), layer);

    rich_text_document_apply_default_char_format(
        layer.rich_text, character, textPresetCharMask(), true);
    rich_text_document_apply_default_paragraph_format(
        layer.rich_text, paragraph, textPresetParagraphMask(), true);

    layer.text_overflow_mode = std::clamp(
        o.value(QStringLiteral("overflowMode")).toInt(layer.text_overflow_mode),
        0, 2);
    layer.text_fit_min_scale = float(std::clamp(
        o.value(QStringLiteral("fitMinScale")).toDouble(layer.text_fit_min_scale),
        0.01, 1.0));
    layer.text_box_width_to_text = o.value(QStringLiteral("boxWidthToText"))
                                       .toBool(layer.text_box_width_to_text);
    layer.text_box_height_to_text = o.value(QStringLiteral("boxHeightToText"))
                                        .toBool(layer.text_box_height_to_text);
    layer.max_text_box_width = float(std::max(
        1.0, o.value(QStringLiteral("maxBoxWidth"))
                 .toDouble(layer.max_text_box_width)));
    layer.max_text_box_height = float(std::max(
        1.0, o.value(QStringLiteral("maxBoxHeight"))
                 .toDouble(layer.max_text_box_height)));
    layer.max_text_box_width_overridden =
        o.value(QStringLiteral("maxBoxWidthOverridden"))
            .toBool(layer.max_text_box_width_overridden);
    layer.max_text_box_height_overridden =
        o.value(QStringLiteral("maxBoxHeightOverridden"))
            .toBool(layer.max_text_box_height_overridden);
    if (!layer.max_text_box_width_overridden)
        layer.max_text_box_width = std::max(1.0f, layer.rect_width);
    if (!layer.max_text_box_height_overridden)
        layer.max_text_box_height = std::max(1.0f, layer.rect_height);

    rich_text_document_sync_layer_mirrors_canonical(layer);

    /* A style preset is a static authoring operation. Match the old contract by
     * replacing any animation on included text/paragraph properties, but derive
     * every static value from the canonical model after the commit. */
    layer.font_size_prop.static_value = layer.rich_text.default_format.font_size;
    layer.font_size_prop.keyframes.clear();
    layer.char_tracking_prop.static_value = layer.rich_text.default_format.tracking;
    layer.char_tracking_prop.keyframes.clear();
    layer.char_scale_x_prop.static_value = layer.rich_text.default_format.scale_x;
    layer.char_scale_x_prop.keyframes.clear();
    layer.char_scale_y_prop.static_value = layer.rich_text.default_format.scale_y;
    layer.char_scale_y_prop.keyframes.clear();
    layer.baseline_shift_prop.static_value =
        layer.rich_text.default_format.baseline_shift;
    layer.baseline_shift_prop.keyframes.clear();
    layer.paragraph_space_before_prop.static_value =
        layer.rich_text.default_paragraph_format.space_before;
    layer.paragraph_space_before_prop.keyframes.clear();
    layer.paragraph_space_after_prop.static_value =
        layer.rich_text.default_paragraph_format.space_after;
    layer.paragraph_space_after_prop.keyframes.clear();
    layer.paragraph_indent_left_prop.static_value =
        layer.rich_text.default_paragraph_format.indent_left;
    layer.paragraph_indent_left_prop.keyframes.clear();
    layer.paragraph_indent_right_prop.static_value =
        layer.rich_text.default_paragraph_format.indent_right;
    layer.paragraph_indent_right_prop.keyframes.clear();
    layer.paragraph_indent_first_line_prop.static_value =
        layer.rich_text.default_paragraph_format.indent_first_line;
    layer.paragraph_indent_first_line_prop.keyframes.clear();
    return true;
}

bool StylePresetLibrary::applyGradientPreset(const StylePreset &preset, Layer &layer)
{
    if (preset.kind != StylePresetKind::Gradient)
        return false;
    if (!isTextLikeLayer(layer)) {
        applyGradientPayload(preset.payload, layer);
        layer.fill_type = 1;
        return true;
    }

    rich_text_document_ensure_canonical(layer);
    RichTextCharFormat character = layer.rich_text.default_format;
    if (!gradientPresetToCharFormat(preset, character))
        return false;
    rich_text_document_apply_default_char_format(
        layer.rich_text, character, gradientPresetCharMask(), true);
    rich_text_document_sync_layer_mirrors_canonical(layer);
    return true;
}

bool StylePresetLibrary::gradientPresetToFill(const StylePreset &preset,
                                              RichTextFill &fill,
                                              std::vector<GradientStop> *extra_stops)
{
    if (preset.kind != StylePresetKind::Gradient)
        return false;
    fillFromGradientPayload(preset.payload, fill, extra_stops);
    return true;
}

bool StylePresetLibrary::isBuiltIn(const StylePreset &preset)
{
    return preset.category.compare(QStringLiteral("Built-in"), Qt::CaseInsensitive) == 0;
}

QString StylePresetLibrary::gradientDescription(const StylePreset &preset)
{
    if (preset.kind != StylePresetKind::Gradient)
        return preset.name;
    const int type = normalizedGradientType(preset.payload.value(QStringLiteral("gradientType")).toInt(0));
    QString type_name = fxm_tr("OBSTitles.LinearGradient");
    if (type == 1) type_name = fxm_tr("OBSTitles.RadialGradient");
    else if (type == 2) type_name = fxm_tr("OBSTitles.ConicalGradient");
    const int stop_count = 2 + preset.payload.value(QStringLiteral("stops")).toArray().size();
    return QStringLiteral("%1 — %2\n%3 · %4 stops")
        .arg(preset.name, preset.category, type_name)
        .arg(stop_count);
}

void StylePresetLibrary::subscribe(QObject *context, std::function<void()> callback)
{
    if (!context || !callback)
        return;
    auto &listeners = presetChangeListeners();
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [](const PresetChangeListener &listener) {
        return listener.context.isNull();
    }), listeners.end());
    listeners.push_back({context, std::move(callback)});
}

void StylePresetLibrary::notifyChanged()
{
    auto &listeners = presetChangeListeners();
    for (auto it = listeners.begin(); it != listeners.end();) {
        if (it->context.isNull()) {
            it = listeners.erase(it);
            continue;
        }
        if (it->callback)
            it->callback();
        ++it;
    }
}


bool StylePresetLibrary::textPresetToCharFormat(const StylePreset &preset, RichTextCharFormat &format)
{
    return preset.kind == StylePresetKind::Text &&
           fxm::style_presets::apply_text_preset_payload(preset.payload, format);
}

bool StylePresetLibrary::textPresetToParagraphFormat(const StylePreset &preset,
                                                     RichTextParagraphFormat &format)
{
    if (preset.kind != StylePresetKind::Text) return false;
    const auto o = preset.payload;
    format.align_h = std::clamp(o.value(QStringLiteral("alignH")).toInt(format.align_h), 0, 6);
    format.align_v = std::clamp(o.value(QStringLiteral("alignV")).toInt(format.align_v), 0, 3);
    format.indent_left = float(o.value(QStringLiteral("paragraphLeft")).toDouble(format.indent_left));
    format.indent_right = float(o.value(QStringLiteral("paragraphRight")).toDouble(format.indent_right));
    format.indent_first_line = float(o.value(QStringLiteral("paragraphFirst")).toDouble(format.indent_first_line));
    format.line_spacing = float(o.value(QStringLiteral("leading")).toDouble(format.line_spacing));
    format.space_before = float(o.value(QStringLiteral("paragraphBefore")).toDouble(format.space_before));
    format.space_after = float(o.value(QStringLiteral("paragraphAfter")).toDouble(format.space_after));
    format.hyphenate = o.value(QStringLiteral("paragraphHyphenate")).toBool(format.hyphenate);
    return true;
}

uint32_t StylePresetLibrary::textPresetParagraphMask()
{
    return RichTextParagraphAll;
}

bool StylePresetLibrary::gradientPresetToCharFormat(const StylePreset &preset, RichTextCharFormat &format)
{
    if (preset.kind != StylePresetKind::Gradient) return false;
    fillFormatFromGradientPayload(preset.payload, format);
    return true;
}

uint32_t StylePresetLibrary::textPresetCharMask()
{
    return fxm::style_presets::text_preset_char_mask();
}

uint32_t StylePresetLibrary::gradientPresetCharMask()
{
    return RichTextCharFillColor;
}

QPixmap StylePresetLibrary::thumbnail(const StylePreset &preset, const QSize &size)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRect r = pix.rect().adjusted(2, 2, -2, -2);
    p.setPen(QColor(60, 60, 60));
    if (preset.kind == StylePresetKind::Gradient) {
        drawCheckerboard(p, r);
        const int type = normalizedGradientType(preset.payload.value(QStringLiteral("gradientType")).toInt(0));
        const double opacity = std::clamp(preset.payload.value(QStringLiteral("opacity")).toDouble(1.0), 0.0, 1.0);
        const double angle = preset.payload.value(QStringLiteral("angle")).toDouble(0.0);
        const QPointF center(r.left() + std::clamp(preset.payload.value(QStringLiteral("centerX")).toDouble(0.5), 0.0, 1.0) * r.width(),
                             r.top() + std::clamp(preset.payload.value(QStringLiteral("centerY")).toDouble(0.5), 0.0, 1.0) * r.height());
        QGradientStops gradient_stops;
        gradient_stops.append(QGradientStop(
            std::clamp(preset.payload.value(QStringLiteral("startPos")).toDouble(0.0), 0.0, 1.0),
            gradientStopColor(parseArgb(preset.payload, "startColor", 0xff4b6ea8),
                              preset.payload.value(QStringLiteral("startOpacity")).toDouble(1.0), opacity)));
        const auto stops = preset.payload.value(QStringLiteral("stops")).toArray();
        for (const auto &entry : stops) {
            const auto s = entry.toObject();
            gradient_stops.append(QGradientStop(
                std::clamp(s.value(QStringLiteral("position")).toDouble(0.5), 0.0, 1.0),
                gradientStopColor(parseArgb(s, "color", 0xffffffff),
                                  s.value(QStringLiteral("opacity")).toDouble(1.0), opacity)));
        }
        gradient_stops.append(QGradientStop(
            std::clamp(preset.payload.value(QStringLiteral("endPos")).toDouble(1.0), 0.0, 1.0),
            gradientStopColor(parseArgb(preset.payload, "endColor", 0xff1b1b1b),
                              preset.payload.value(QStringLiteral("endOpacity")).toDouble(1.0), opacity)));
        std::sort(gradient_stops.begin(), gradient_stops.end(), [](const QGradientStop &a, const QGradientStop &b) {
            return a.first < b.first;
        });

        const auto spread = [&]() {
            switch (gradientSpreadFromPayload(preset.payload, 0)) {
            case 1: return QGradient::ReflectSpread;
            case 2: return QGradient::RepeatSpread;
            default: return QGradient::PadSpread;
            }
        }();
        QBrush gradient_brush;
        if (type == 1) {
            const double scale = std::max(0.05, preset.payload.value(QStringLiteral("scale")).toDouble(1.0));
            QRadialGradient gradient(center, std::max(1.0, 0.55 * std::min(r.width(), r.height()) * scale));
            gradient.setSpread(spread);
            gradient.setStops(gradient_stops);
            gradient_brush = QBrush(gradient);
        } else if (type == 2) {
            QConicalGradient gradient(center, -angle);
            gradient.setSpread(spread);
            gradient.setStops(gradient_stops);
            gradient_brush = QBrush(gradient);
        } else {
            constexpr double kPi = 3.14159265358979323846;
            const double radians = angle * kPi / 180.0;
            const QPointF direction(std::cos(radians), std::sin(radians));
            const double half = 0.5 * std::hypot(r.width(), r.height());
            QLinearGradient gradient(center - direction * half, center + direction * half);
            gradient.setSpread(spread);
            gradient.setStops(gradient_stops);
            gradient_brush = QBrush(gradient);
        }
        p.setBrush(gradient_brush);
        p.drawRoundedRect(r, 3, 3);
    } else {
        Layer preview_layer;
        applyTextPreset(preset, preview_layer);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(35, 35, 35));
        p.drawRoundedRect(r, 5, 5);

        QFont font(QString::fromStdString(preview_layer.font_family));
        if (!preview_layer.font_style.empty())
            font.setStyleName(QString::fromStdString(preview_layer.font_style));
        font.setPixelSize(24);
        font.setBold(preview_layer.font_bold);
        font.setItalic(preview_layer.font_italic);
        if (preview_layer.text_style == 2)
            font.setCapitalization(QFont::SmallCaps);

        QPainterPath path;
        path.addText(QPointF(0.0, QFontMetricsF(font).ascent()), font,
                     preview_layer.text_style == 1 ? QStringLiteral("AA")
                                                   : QStringLiteral("Aa"));
        QTransform character_scale;
        character_scale.scale(
            std::clamp(static_cast<double>(preview_layer.char_scale_x), 0.1, 5.0),
            std::clamp(static_cast<double>(preview_layer.char_scale_y), 0.1, 5.0));
        path = character_scale.map(path);
        QRectF bounds = path.boundingRect();
        const double fit = std::min(
            1.0, std::min((r.width() - 8.0) / std::max(1.0, bounds.width()),
                          (r.height() - 8.0) / std::max(1.0, bounds.height())));
        if (fit < 1.0) {
            QTransform fit_transform;
            fit_transform.scale(fit, fit);
            path = fit_transform.map(path);
            bounds = path.boundingRect();
        }
        path.translate(r.center() - bounds.center());
        bounds = path.boundingRect();

        const QBrush fill_brush = preview_layer.fill_type == 1
            ? previewGradientBrush(preview_layer.gradient_type, bounds,
                                   preview_layer.gradient_start_color,
                                   preview_layer.gradient_end_color,
                                   preview_layer.gradient_angle)
            : QBrush(colorFromArgb(preview_layer.text_color));
        const QBrush stroke_brush = preview_layer.stroke_fill_type == 2
            ? previewGradientBrush(preview_layer.stroke_gradient_type, bounds,
                                   preview_layer.stroke_gradient_start_color,
                                   preview_layer.stroke_gradient_end_color,
                                   preview_layer.stroke_gradient_angle)
            : QBrush(colorFromArgb(preview_layer.stroke_color));
        QPen stroke_pen(stroke_brush,
            std::clamp(static_cast<double>(preview_layer.stroke_width), 0.5, 6.0));
        stroke_pen.setJoinStyle(preview_layer.outline_join_style == 1 ? Qt::RoundJoin
                                : preview_layer.outline_join_style == 2 ? Qt::BevelJoin
                                                                        : Qt::MiterJoin);
        if (preview_layer.stroke_fill_type != 2) {
            QColor stroke_color = colorFromArgb(preview_layer.stroke_color);
            const double stroke_alpha = std::clamp(
                static_cast<double>(stroke_color.alphaF()) *
                    static_cast<double>(preview_layer.outline_opacity),
                0.0, 1.0);
            stroke_color.setAlphaF(stroke_alpha);
            stroke_pen.setColor(stroke_color);
        }
        const bool has_stroke = preview_layer.outline_enabled &&
            preview_layer.stroke_fill_type != 0 && preview_layer.stroke_width > 0.0f;
        if (has_stroke && !preview_layer.outline_on_front) {
            p.setPen(stroke_pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill_brush);
        p.drawPath(path);
        if (has_stroke && preview_layer.outline_on_front) {
            p.setPen(stroke_pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
    }
    return pix;
}

void StylePresetLibrary::ensureDefaults()
{
    bool hasText = false, hasGradient = false;
    for (const auto &p : presets_) {
        hasText |= p.kind == StylePresetKind::Text;
        hasGradient |= p.kind == StylePresetKind::Gradient;
    }
    if (!hasText) {
        Layer l;
        l.font_family = "Arial";
        l.font_size = 64;
        l.font_bold = true;
        l.text_color = 0xffffffff;
        presets_.append(makeTextPreset(l, QStringLiteral("Clean Broadcast"), QStringLiteral("Built-in")));
        l.font_italic = true;
        l.char_tracking = 40.0f;
        l.text_color = 0xff00a1b9;
        presets_.append(makeTextPreset(l, QStringLiteral("Editorial Accent"), QStringLiteral("Built-in")));
    }
    if (!hasGradient) {
        Layer l;
        l.fill_type = 1;
        l.gradient_start_color = 0xff00a1b9;
        l.gradient_end_color = 0xff0b2530;
        presets_.append(makeGradientPreset(l, QStringLiteral("Cyan News Bar"), QStringLiteral("Built-in")));
        l.gradient_start_color = 0xfff15b24;
        l.gradient_end_color = 0xff1b1b1b;
        presets_.append(makeGradientPreset(l, QStringLiteral("Orange Alert"), QStringLiteral("Built-in")));
    }
}

StylePresetPanel::StylePresetPanel(StylePresetKind kind, QWidget *parent)
    : QWidget(parent), kind_(kind)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *top = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(fxm_tr("OBSTitles.SearchStyles"));
    category_filter_ = new QComboBox(this);
    category_filter_->setEditable(false);
    top->addWidget(search_, 1);
    top->addWidget(category_filter_);
    layout->addLayout(top);

    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSpacing(kind_ == StylePresetKind::Gradient ? 4 : 2);
    list_->setIconSize(kind_ == StylePresetKind::Gradient ? QSize(32, 32) : QSize(96, 48));
    list_->setGridSize(kind_ == StylePresetKind::Gradient ? QSize(42, 42) : QSize(132, 92));
    list_->setUniformItemSizes(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(list_, 1);

    auto *buttons = new QHBoxLayout;
    add_button_ = new QToolButton(this); add_button_->setText(QStringLiteral("+"));
    add_button_->setToolTip(kind_ == StylePresetKind::Gradient
                                ? fxm_tr("OBSTitles.SaveGradientPreset")
                                : fxm_tr("OBSTitles.SaveStylePreset"));
    apply_button_ = new QToolButton(this); apply_button_->setText(QStringLiteral("✓")); apply_button_->setToolTip(fxm_tr("OBSTitles.ApplyStylePreset"));
    edit_button_ = new QToolButton(this); edit_button_->setText(QStringLiteral("Edit")); edit_button_->setToolTip(QStringLiteral("Edit text style properties"));
    delete_button_ = new QToolButton(this); delete_button_->setText(QStringLiteral("−")); delete_button_->setToolTip(fxm_tr("OBSTitles.DeleteStylePreset"));
    import_button_ = new QToolButton(this); import_button_->setText(QStringLiteral("Import")); import_button_->setToolTip(fxm_tr("OBSTitles.ImportStylePresets"));
    export_button_ = new QToolButton(this); export_button_->setText(QStringLiteral("Export")); export_button_->setToolTip(fxm_tr("OBSTitles.ExportStylePresets"));
    buttons->addWidget(add_button_);
    buttons->addWidget(apply_button_);
    buttons->addWidget(edit_button_);
    buttons->addWidget(delete_button_);
    buttons->addStretch(1);
    buttons->addWidget(import_button_);
    buttons->addWidget(export_button_);
    layout->addLayout(buttons);

    connect(search_, &QLineEdit::textChanged, this, &StylePresetPanel::refreshList);
    connect(category_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StylePresetPanel::refreshList);
    if (kind_ == StylePresetKind::Gradient) {
        connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
            applySelectedPreset();
        });
        apply_button_->hide();
        edit_button_->hide();
    } else {
        connect(list_, &QListWidget::itemDoubleClicked, this, [this]() {
            applySelectedPreset();
        });
    }
    connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) {
                const StylePreset *preset = selectedPreset();
                const bool user_preset = preset && !StylePresetLibrary::isBuiltIn(*preset);
                delete_button_->setEnabled(user_preset);
                edit_button_->setEnabled(preset && kind_ == StylePresetKind::Text);
                apply_button_->setEnabled(preset != nullptr);
            });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = list_->itemAt(pos);
        if (!item)
            return;
        list_->setCurrentItem(item);
        const StylePreset *preset = selectedPreset();
        if (!preset)
            return;
        QMenu menu(list_);
        QAction *edit_action = kind_ == StylePresetKind::Text
            ? menu.addAction(QStringLiteral("Edit")) : nullptr;
        QAction *remove_action = StylePresetLibrary::isBuiltIn(*preset)
            ? nullptr : menu.addAction(fxm_tr("OBSTitles.DeleteStylePreset"));
        if (!edit_action && !remove_action)
            return;
        QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
        if (chosen == edit_action)
            editSelectedPreset();
        else if (chosen == remove_action)
            deleteSelectedPreset();
    });
    connect(add_button_, &QToolButton::clicked, this, &StylePresetPanel::addCurrentAsPreset);
    connect(apply_button_, &QToolButton::clicked, this, &StylePresetPanel::applySelectedPreset);
    connect(edit_button_, &QToolButton::clicked, this, &StylePresetPanel::editSelectedPreset);
    connect(delete_button_, &QToolButton::clicked, this, &StylePresetPanel::deleteSelectedPreset);
    connect(import_button_, &QToolButton::clicked, this, &StylePresetPanel::importPresets);
    connect(export_button_, &QToolButton::clicked, this, &StylePresetPanel::exportPresets);

    rebuildCategoryFilter();
    refreshList();
    delete_button_->setEnabled(false);
    edit_button_->setEnabled(false);
    apply_button_->setEnabled(false);
    StylePresetLibrary::subscribe(this, [this]() {
        reload();
    });
}

void StylePresetPanel::setCreatePresetCallback(std::function<StylePreset(const QString &, const QString &)> callback)
{
    create_callback_ = std::move(callback);
}

void StylePresetPanel::setApplyPresetCallback(std::function<void(const StylePreset &)> callback)
{
    apply_callback_ = std::move(callback);
}

void StylePresetPanel::reload()
{
    library_.load();
    rebuildCategoryFilter();
    refreshList();
}

void StylePresetPanel::rebuildCategoryFilter()
{
    const QString current = category_filter_->currentData().toString();
    category_filter_->blockSignals(true);
    category_filter_->clear();
    category_filter_->addItem(fxm_tr("OBSTitles.AllCategories"), QString());
    for (const auto &category : library_.categories(kind_)) category_filter_->addItem(category, category);
    const int idx = category_filter_->findData(current);
    if (idx >= 0) category_filter_->setCurrentIndex(idx);
    category_filter_->blockSignals(false);
}

void StylePresetPanel::refreshList()
{
    const QString needle = search_->text().trimmed();
    const QString category = category_filter_->currentData().toString();
    list_->clear();
    for (const auto &preset : library_.presets(kind_)) {
        if (!category.isEmpty() && preset.category != category) continue;
        if (!needle.isEmpty() && !preset.name.contains(needle, Qt::CaseInsensitive) && !preset.category.contains(needle, Qt::CaseInsensitive)) continue;
        const bool gradient = kind_ == StylePresetKind::Gradient;
        const QSize thumbnail_size = gradient ? QSize(32, 32) : QSize(96, 48);
        const QString label = gradient ? QString() : preset.name + QStringLiteral("\n") + preset.category;
        auto *item = new QListWidgetItem(QIcon(StylePresetLibrary::thumbnail(preset, thumbnail_size)), label, list_);
        item->setData(Qt::UserRole, preset.id);
        item->setToolTip(gradient ? StylePresetLibrary::gradientDescription(preset)
                                  : preset.name + QStringLiteral(" — ") + preset.category);
        if (gradient)
            item->setSizeHint(QSize(42, 42));
    }
}

StylePreset *StylePresetPanel::selectedPreset()
{
    auto *item = list_->currentItem();
    if (!item) return nullptr;
    const QString id = item->data(Qt::UserRole).toString();
    static StylePreset selected;
    for (const auto &preset : library_.presets(kind_)) {
        if (preset.id == id) {
            selected = preset;
            return &selected;
        }
    }
    return nullptr;
}

const StylePreset *StylePresetPanel::selectedPreset() const
{
    return const_cast<StylePresetPanel *>(this)->selectedPreset();
}

void StylePresetPanel::addCurrentAsPreset()
{
    if (!create_callback_) return;
    bool ok = false;
    const QString dialog_title = kind_ == StylePresetKind::Gradient
        ? fxm_tr("OBSTitles.SaveGradientPreset")
        : fxm_tr("OBSTitles.SaveStylePreset");
    const QString name = QInputDialog::getText(this, dialog_title, fxm_tr("OBSTitles.StylePresetName"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const QString category = QInputDialog::getText(this, fxm_tr("OBSTitles.StylePresetCategory"), fxm_tr("OBSTitles.StylePresetCategory"), QLineEdit::Normal, QStringLiteral("User"), &ok);
    if (!ok) return;
    library_.upsert(create_callback_(name.trimmed(), category.trimmed().isEmpty() ? QStringLiteral("User") : category.trimmed()));
    rebuildCategoryFilter();
    refreshList();
}

void StylePresetPanel::applySelectedPreset()
{
    const StylePreset *preset = selectedPreset();
    if (preset && apply_callback_) apply_callback_(*preset);
}

void StylePresetPanel::editSelectedPreset()
{
    const StylePreset *preset = selectedPreset();
    if (!preset || kind_ != StylePresetKind::Text)
        return;
    TextStyleEditDialog dialog(*preset, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    library_.upsert(dialog.editedPreset());
    rebuildCategoryFilter();
    refreshList();
}

void StylePresetPanel::deleteSelectedPreset()
{
    const StylePreset *preset = selectedPreset();
    if (!preset) return;
    if (StylePresetLibrary::isBuiltIn(*preset)) return;
    if (QMessageBox::question(this, fxm_tr("OBSTitles.DeleteStylePreset"), fxm_tr("OBSTitles.DeleteStylePresetConfirm")) != QMessageBox::Yes) return;
    library_.remove(preset->id);
    rebuildCategoryFilter();
    refreshList();
}

void StylePresetPanel::importPresets()
{
    const QString path = QFileDialog::getOpenFileName(this, fxm_tr("OBSTitles.ImportStylePresets"), QString(), QStringLiteral("OBS FXM Style Presets (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!library_.importFromFile(path, &error)) QMessageBox::warning(this, fxm_tr("OBSTitles.ImportStylePresets"), error);
    rebuildCategoryFilter();
    refreshList();
}

void StylePresetPanel::exportPresets()
{
    const QString path = QFileDialog::getSaveFileName(this, fxm_tr("OBSTitles.ExportStylePresets"), QStringLiteral("style-presets.json"), QStringLiteral("OBS FXM Style Presets (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!library_.exportToFile(path, kind_, &error)) QMessageBox::warning(this, fxm_tr("OBSTitles.ExportStylePresets"), error);
}

} // namespace obsfxm
