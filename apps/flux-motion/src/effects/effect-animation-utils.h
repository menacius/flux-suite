#pragma once

#include "layer-effects.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace fxm::effects::animation {

inline QJsonObject tracks_object(const LayerEffect &effect)
{
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(effect.extension_keyframes_json));
    return doc.isObject() ? doc.object() : QJsonObject{};
}

inline void write_tracks_object(LayerEffect &effect, const QJsonObject &tracks)
{
    effect.extension_keyframes_json = QJsonDocument(tracks)
        .toJson(QJsonDocument::Compact).toStdString();
}

inline QJsonArray track_keys(const LayerEffect &effect, const QString &path)
{
    return tracks_object(effect).value(path).toArray();
}

inline void write_track_keys_preserve_order(LayerEffect &effect, const QString &path,
                                           const QJsonArray &keys)
{
    QJsonObject tracks = tracks_object(effect);
    if (keys.isEmpty())
        tracks.remove(path);
    else
        tracks.insert(path, keys);
    write_tracks_object(effect, tracks);
}

inline void write_track_keys(LayerEffect &effect, const QString &path, QJsonArray keys)
{
    QList<QJsonValue> sorted;
    sorted.reserve(keys.size());
    for (const QJsonValue &value : keys)
        sorted.append(value);
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue &a, const QJsonValue &b) {
        return a.toObject().value(QStringLiteral("time")).toDouble() <
               b.toObject().value(QStringLiteral("time")).toDouble();
    });
    keys = QJsonArray{};
    for (const QJsonValue &value : sorted)
        keys.append(value);
    QJsonObject tracks = tracks_object(effect);
    if (keys.isEmpty())
        tracks.remove(path);
    else
        tracks.insert(path, keys);
    write_tracks_object(effect, tracks);
}

inline QJsonValue state_path_value(const LayerEffect &effect, const QString &path)
{
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(effect.extension_parameters_json));
    if (!doc.isObject())
        return {};
    const QJsonObject root = doc.object();
    const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() == 1)
        return root.value(parts.front());
    if (parts.size() == 3 && parts.front() == QStringLiteral("elements")) {
        bool ok = false;
        const int index = parts.at(1).toInt(&ok);
        const QJsonArray elements = root.value(QStringLiteral("elements")).toArray();
        if (ok && index >= 0 && index < elements.size())
            return elements.at(index).toObject().value(parts.at(2));
    }
    return {};
}

inline void set_state_path_value(LayerEffect &effect, const QString &path,
                                 const QJsonValue &value)
{
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(effect.extension_parameters_json));
    QJsonObject root = doc.isObject() ? doc.object() : QJsonObject{};
    const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() == 1) {
        root.insert(parts.front(), value);
    } else if (parts.size() == 3 && parts.front() == QStringLiteral("elements")) {
        bool ok = false;
        const int index = parts.at(1).toInt(&ok);
        QJsonArray elements = root.value(QStringLiteral("elements")).toArray();
        if (ok && index >= 0 && index < elements.size()) {
            QJsonObject element = elements.at(index).toObject();
            element.insert(parts.at(2), value);
            elements.replace(index, element);
            root.insert(QStringLiteral("elements"), elements);
        }
    }
    effect.extension_parameters_json = QJsonDocument(root)
        .toJson(QJsonDocument::Compact).toStdString();
}

inline EasingType easing_from_key(const QJsonObject &key)
{
    const QString easing = key.value(QStringLiteral("easing")).toString();
    if (easing == QStringLiteral("ease-in")) return EasingType::EaseIn;
    if (easing == QStringLiteral("ease-out")) return EasingType::EaseOut;
    if (easing == QStringLiteral("ease-in-out")) return EasingType::EaseInOut;
    if (easing == QStringLiteral("bezier")) return EasingType::Bezier;
    if (easing == QStringLiteral("hold") ||
        key.value(QStringLiteral("interpolation")).toString() == QStringLiteral("hold"))
        return EasingType::Hold;
    return EasingType::Linear;
}

inline void set_key_easing(QJsonObject &key, EasingType easing)
{
    QString encoded = QStringLiteral("linear");
    switch (easing) {
    case EasingType::EaseIn: encoded = QStringLiteral("ease-in"); break;
    case EasingType::EaseOut: encoded = QStringLiteral("ease-out"); break;
    case EasingType::EaseInOut: encoded = QStringLiteral("ease-in-out"); break;
    case EasingType::Bezier: encoded = QStringLiteral("bezier"); break;
    case EasingType::Hold: encoded = QStringLiteral("hold"); break;
    default: break;
    }
    key.insert(QStringLiteral("easing"), encoded);
    key.insert(QStringLiteral("interpolation"),
               easing == EasingType::Hold ? QStringLiteral("hold")
                                          : QStringLiteral("linear"));
    if (!key.contains(QStringLiteral("cx1"))) key.insert(QStringLiteral("cx1"), 0.333);
    if (!key.contains(QStringLiteral("cy1"))) key.insert(QStringLiteral("cy1"), 0.0);
    if (!key.contains(QStringLiteral("cx2"))) key.insert(QStringLiteral("cx2"), 0.667);
    if (!key.contains(QStringLiteral("cy2"))) key.insert(QStringLiteral("cy2"), 1.0);
}

inline TemporalInterpolationMode temporal_mode_from_key(const QJsonObject &key)
{
    const int encoded = key.value(QStringLiteral("temporal_mode"))
        .toInt((int)TemporalInterpolationMode::AutoBezier);
    return (TemporalInterpolationMode)std::clamp(
        encoded, (int)TemporalInterpolationMode::Linear,
        (int)TemporalInterpolationMode::ManualBezier);
}

inline bool temporal_velocity_explicit(const QJsonObject &key)
{
    return key.value(QStringLiteral("temporal_velocity_explicit")).toBool(false);
}

inline void set_key_temporal_mode(QJsonObject &key, TemporalInterpolationMode mode)
{
    key.insert(QStringLiteral("temporal_mode"), (int)mode);
    key.insert(QStringLiteral("temporal_velocity_explicit"), true);
    key.insert(QStringLiteral("temporal_tangents_linked"),
               mode == TemporalInterpolationMode::AutoBezier ||
               mode == TemporalInterpolationMode::ContinuousBezier);
    if (!key.contains(QStringLiteral("temporal_in_influence")))
        key.insert(QStringLiteral("temporal_in_influence"), 33.3333333333);
    if (!key.contains(QStringLiteral("temporal_out_influence")))
        key.insert(QStringLiteral("temporal_out_influence"), 33.3333333333);
    if (!key.contains(QStringLiteral("temporal_in_speed")))
        key.insert(QStringLiteral("temporal_in_speed"), 0.0);
    if (!key.contains(QStringLiteral("temporal_out_speed")))
        key.insert(QStringLiteral("temporal_out_speed"), 0.0);
    set_key_easing(key, mode == TemporalInterpolationMode::Hold ? EasingType::Hold
                         : mode == TemporalInterpolationMode::Linear ? EasingType::Linear
                         : EasingType::Bezier);
}

inline double cubic_bezier_y(double x, double cx1, double cy1,
                             double cx2, double cy2)
{
    auto sample = [](double t, double p1, double p2) {
        const double inv = 1.0 - t;
        return 3.0 * inv * inv * t * p1 +
               3.0 * inv * t * t * p2 + t * t * t;
    };
    auto slope = [](double t, double p1, double p2) {
        const double inv = 1.0 - t;
        return 3.0 * inv * inv * p1 +
               6.0 * inv * t * (p2 - p1) +
               3.0 * t * t * (1.0 - p2);
    };
    x = std::clamp(x, 0.0, 1.0);
    cx1 = std::clamp(cx1, 0.0, 1.0);
    cx2 = std::clamp(cx2, 0.0, 1.0);
    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double dx = sample(t, cx1, cx2) - x;
        const double derivative = slope(t, cx1, cx2);
        if (std::abs(dx) < 1e-6 || std::abs(derivative) < 1e-6) break;
        t = std::clamp(t - dx / derivative, 0.0, 1.0);
    }
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 12; ++i) {
        const double bx = sample(t, cx1, cx2);
        if (std::abs(bx - x) < 1e-6) break;
        if (bx < x) lo = t; else hi = t;
        t = 0.5 * (lo + hi);
    }
    return std::clamp(sample(t, cy1, cy2), 0.0, 1.0);
}

inline double eased_mix(double x, const QJsonObject &left)
{
    x = std::clamp(x, 0.0, 1.0);
    switch (easing_from_key(left)) {
    case EasingType::EaseIn: return x * x;
    case EasingType::EaseOut: return x * (2.0 - x);
    case EasingType::EaseInOut:
        return x < 0.5 ? 2.0 * x * x : -1.0 + (4.0 - 2.0 * x) * x;
    case EasingType::Bezier:
        return cubic_bezier_y(x,
            left.value(QStringLiteral("cx1")).toDouble(0.333),
            left.value(QStringLiteral("cy1")).toDouble(0.0),
            left.value(QStringLiteral("cx2")).toDouble(0.667),
            left.value(QStringLiteral("cy2")).toDouble(1.0));
    case EasingType::Hold: return 0.0;
    default: return x;
    }
}

inline QJsonValue interpolate_value(const QJsonValue &a, const QJsonValue &b,
                                    double mix)
{
    if (a.isDouble() && b.isDouble())
        return a.toDouble() + (b.toDouble() - a.toDouble()) * mix;
    if (a.isArray() && b.isArray()) {
        const QJsonArray aa = a.toArray();
        const QJsonArray bb = b.toArray();
        QJsonArray out;
        const qsizetype count = std::min(aa.size(), bb.size());
        for (qsizetype i = 0; i < count; ++i)
            out.append(aa.at(i).toDouble() +
                       (bb.at(i).toDouble() - aa.at(i).toDouble()) * mix);
        return out;
    }
    return mix < 1.0 ? a : b;
}

inline QJsonValue evaluate_track(const LayerEffect &effect, const QString &path,
                                 double time, const QJsonValue &fallback)
{
    const QJsonArray keys = track_keys(effect, path);
    if (keys.isEmpty()) return fallback;
    int left_index = 0;
    int right_index = keys.size() - 1;
    QJsonObject left = keys.first().toObject();
    QJsonObject right = keys.last().toObject();
    for (int index = 0; index < keys.size(); ++index) {
        const QJsonObject key = keys.at(index).toObject();
        const double key_time = key.value(QStringLiteral("time")).toDouble();
        if (key_time <= time) { left = key; left_index = index; }
        if (key_time >= time) { right = key; right_index = index; break; }
    }
    const double left_time = left.value(QStringLiteral("time")).toDouble();
    const double right_time = right.value(QStringLiteral("time")).toDouble();
    if (!(right_time > left_time)) return left.value(QStringLiteral("value"));

    const QJsonValue left_value = left.value(QStringLiteral("value"));
    const QJsonValue right_value = right.value(QStringLiteral("value"));
    const bool explicit_velocity = temporal_velocity_explicit(left) ||
                                   temporal_velocity_explicit(right);
    if (explicit_velocity && left_value.isDouble() && right_value.isDouble()) {
        TemporalBezierSegment segment;
        segment.start_time = left_time;
        segment.end_time = right_time;
        segment.start_value = left_value.toDouble();
        segment.end_value = right_value.toDouble();
        segment.hold = temporal_velocity_explicit(left)
            ? temporal_mode_from_key(left) == TemporalInterpolationMode::Hold
            : easing_from_key(left) == EasingType::Hold;
        const double linear_speed = (segment.end_value - segment.start_value) /
                                    (right_time - left_time);
        auto numeric_slope = [&](int a, int b) {
            if (a < 0 || b < 0 || a >= keys.size() || b >= keys.size()) return 0.0;
            const QJsonObject ka = keys.at(a).toObject();
            const QJsonObject kb = keys.at(b).toObject();
            const QJsonValue va = ka.value(QStringLiteral("value"));
            const QJsonValue vb = kb.value(QStringLiteral("value"));
            const double dt = kb.value(QStringLiteral("time")).toDouble() -
                              ka.value(QStringLiteral("time")).toDouble();
            return va.isDouble() && vb.isDouble() && dt > 1e-10
                ? (vb.toDouble() - va.toDouble()) / dt : 0.0;
        };
        auto automatic_speed = [&](int index) {
            if (keys.size() < 2) return 0.0;
            if (index <= 0) return numeric_slope(0, 1);
            if (index + 1 >= keys.size()) return numeric_slope(index - 1, index);
            return numeric_slope(index - 1, index + 1);
        };
        auto influence_for = [&](const QJsonObject &key, bool incoming) {
            if (!temporal_velocity_explicit(key) && easing_from_key(left) == EasingType::Bezier)
                return incoming
                    ? 100.0 * (1.0 - left.value(QStringLiteral("cx2")).toDouble(0.667))
                    : 100.0 * left.value(QStringLiteral("cx1")).toDouble(0.333);
            if (!temporal_velocity_explicit(key)) return 33.3333333333;
            const TemporalInterpolationMode mode = temporal_mode_from_key(key);
            if (mode == TemporalInterpolationMode::Linear ||
                mode == TemporalInterpolationMode::AutoBezier)
                return 33.3333333333;
            return key.value(incoming ? QStringLiteral("temporal_in_influence")
                                      : QStringLiteral("temporal_out_influence"))
                .toDouble(33.3333333333);
        };
        auto speed_for = [&](const QJsonObject &key, int index, bool incoming) {
            if (!temporal_velocity_explicit(key)) {
                if (easing_from_key(left) == EasingType::Bezier) {
                    if (incoming) {
                        const double influence = std::max(1e-9,
                            1.0 - left.value(QStringLiteral("cx2")).toDouble(0.667));
                        return (segment.end_value - segment.start_value) *
                            (1.0 - left.value(QStringLiteral("cy2")).toDouble(1.0)) /
                            (std::max(right_time - left_time, 1e-10) * influence);
                    }
                    const double influence = std::max(1e-9,
                        left.value(QStringLiteral("cx1")).toDouble(0.333));
                    return (segment.end_value - segment.start_value) *
                        left.value(QStringLiteral("cy1")).toDouble(0.0) /
                        (std::max(right_time - left_time, 1e-10) * influence);
                }
                return linear_speed;
            }
            switch (temporal_mode_from_key(key)) {
            case TemporalInterpolationMode::Linear: return linear_speed;
            case TemporalInterpolationMode::AutoBezier: return automatic_speed(index);
            case TemporalInterpolationMode::Hold: return 0.0;
            case TemporalInterpolationMode::ContinuousBezier:
            case TemporalInterpolationMode::ManualBezier:
            default:
                return key.value(incoming ? QStringLiteral("temporal_in_speed")
                                          : QStringLiteral("temporal_out_speed"))
                    .toDouble(linear_speed);
            }
        };
        segment.outgoing_influence = std::clamp(influence_for(left, false), 0.0, 100.0) / 100.0;
        segment.incoming_influence = std::clamp(influence_for(right, true), 0.0, 100.0) / 100.0;
        segment.outgoing_speed = speed_for(left, left_index, false);
        segment.incoming_speed = speed_for(right, right_index, true);
        return evaluate_temporal_segment(segment, time);
    }

    const double linear = (time - left_time) / (right_time - left_time);
    return interpolate_value(left_value, right_value, eased_mix(linear, left));
}

inline bool has_keyframe_at(const LayerEffect &effect, const QString &path,
                            double time, double epsilon = 0.0001)
{
    const QJsonArray keys = track_keys(effect, path);
    for (const QJsonValue &value : keys)
        if (std::abs(value.toObject().value(QStringLiteral("time")).toDouble() - time) < epsilon)
            return true;
    return false;
}

inline int keyframe_index_at(const LayerEffect &effect, const QString &path,
                             double time, double epsilon = 0.0001)
{
    const QJsonArray keys = track_keys(effect, path);
    for (int i = 0; i < keys.size(); ++i)
        if (std::abs(keys.at(i).toObject().value(QStringLiteral("time")).toDouble() - time) < epsilon)
            return i;
    return -1;
}

inline void add_or_replace_keyframe(LayerEffect &effect, const QString &path,
                                    double time, const QJsonValue &value,
                                    EasingType easing = EasingType::Linear)
{
    QJsonArray keys = track_keys(effect, path);
    const int existing = keyframe_index_at(effect, path, time);
    QJsonObject key{{QStringLiteral("time"), time},
                    {QStringLiteral("value"), value}};
    if (existing >= 0) {
        key = keys.at(existing).toObject();
        key.insert(QStringLiteral("time"), time);
        key.insert(QStringLiteral("value"), value);
        if (key.value(QStringLiteral("easing")).isUndefined())
            set_key_easing(key, easing);
        keys.replace(existing, key);
    } else {
        set_key_easing(key, easing);
        keys.append(key);
    }
    write_track_keys(effect, path, keys);
}

inline void remove_keyframe_at(LayerEffect &effect, const QString &path,
                               double time, double epsilon = 0.0001)
{
    QJsonArray keys = track_keys(effect, path);
    for (int i = keys.size() - 1; i >= 0; --i)
        if (std::abs(keys.at(i).toObject().value(QStringLiteral("time")).toDouble() - time) < epsilon)
            keys.removeAt(i);
    write_track_keys(effect, path, keys);
}

inline void toggle_keyframe(LayerEffect &effect, const QString &path,
                            double time, const QJsonValue &value,
                            EasingType easing = EasingType::Linear)
{
    if (has_keyframe_at(effect, path, time))
        remove_keyframe_at(effect, path, time);
    else
        add_or_replace_keyframe(effect, path, time, value, easing);
}

inline void set_animated_value(LayerEffect &effect, const QString &path,
                               double time, const QJsonValue &value)
{
    if (track_keys(effect, path).isEmpty())
        set_state_path_value(effect, path, value);
    else
        add_or_replace_keyframe(effect, path, time, value);
}

inline bool effect_has_any_keyframes(const LayerEffect &effect)
{
    const auto native = std::initializer_list<const AnimatedProperty *>{
        &effect.enabled_prop, &effect.brightness_prop, &effect.contrast_prop,
        &effect.saturation_prop, &effect.opacity_prop, &effect.size_prop,
        &effect.distance_prop, &effect.angle_prop, &effect.spread_prop,
        &effect.falloff_prop, &effect.amount_prop, &effect.scale_prop,
        &effect.softness_prop, &effect.roundness_prop, &effect.speed_prop,
        &effect.center_x_prop, &effect.center_y_prop, &effect.complexity_prop,
        &effect.evolution_prop, &effect.stroke_width_prop,
        &effect.trim_start_prop, &effect.trim_end_prop,
        &effect.trim_offset_prop,
        &effect.stroke_opacity_prop, &effect.padding_left_prop,
        &effect.padding_right_prop, &effect.padding_top_prop,
        &effect.padding_bottom_prop, &effect.corner_radius_tl_prop,
        &effect.corner_radius_tr_prop, &effect.corner_radius_br_prop,
        &effect.corner_radius_bl_prop, &effect.gradient_start_pos_prop,
        &effect.gradient_end_pos_prop, &effect.gradient_start_opacity_prop,
        &effect.gradient_end_opacity_prop, &effect.gradient_angle_prop,
        &effect.gradient_center_x_prop, &effect.gradient_center_y_prop,
        &effect.gradient_scale_prop, &effect.gradient_focal_x_prop,
        &effect.gradient_focal_y_prop, &effect.gradient_opacity_prop,
        &effect.gradient_start_color_a, &effect.gradient_start_color_r,
        &effect.gradient_start_color_g, &effect.gradient_start_color_b,
        &effect.gradient_end_color_a, &effect.gradient_end_color_r,
        &effect.gradient_end_color_g, &effect.gradient_end_color_b,
        &effect.color_a, &effect.color_r, &effect.color_g, &effect.color_b,
        &effect.stroke_color_a, &effect.stroke_color_r, &effect.stroke_color_g,
        &effect.stroke_color_b, &effect.secondary_color_a,
        &effect.secondary_color_r, &effect.secondary_color_g,
        &effect.secondary_color_b
    };
    for (const AnimatedProperty *prop : native)
        if (prop && prop->is_animated()) return true;
    const QJsonObject tracks = tracks_object(effect);
    for (auto it = tracks.begin(); it != tracks.end(); ++it)
        if (!it.value().toArray().isEmpty()) return true;
    return false;
}

} // namespace fxm::effects::animation
