#pragma once

#include "animation.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace fxm::editor::animation_edit {

inline void apply_linear_easing(Keyframe &keyframe)
{
    keyframe.easing = EasingType::Linear;
    keyframe.temporal_velocity_explicit = false;
    keyframe.temporal_tangents_linked = true;
    keyframe.cx1 = 0.333f;
    keyframe.cy1 = 0.0f;
    keyframe.cx2 = 0.667f;
    keyframe.cy2 = 1.0f;
}

inline void apply_linear_easing(VectorKeyframe &keyframe)
{
    keyframe.easing = EasingType::Linear;
    keyframe.temporal_velocity_explicit = false;
    keyframe.temporal_tangents_linked = true;
    keyframe.cx1 = 0.333f;
    keyframe.cy1 = 0.0f;
    keyframe.cx2 = 0.667f;
    keyframe.cy2 = 1.0f;
}

inline void apply_linear_easing(Vector3Keyframe &keyframe)
{
    keyframe.easing = EasingType::Linear;
    keyframe.temporal_velocity_explicit = false;
    keyframe.temporal_tangents_linked = true;
    keyframe.cx1 = 0.333f;
    keyframe.cy1 = 0.0f;
    keyframe.cx2 = 0.667f;
    keyframe.cy2 = 1.0f;
}

inline void add_or_replace_keyframe(AnimatedProperty &property, double time,
                                    double value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    property.static_value = value;
    for (auto &keyframe : property.keyframes) {
        if (std::abs(keyframe.time - time) <= kEpsilon) {
            keyframe.time = time;
            keyframe.value = value;
            return;
        }
    }
    Keyframe keyframe;
    keyframe.time = time;
    keyframe.value = value;
    apply_linear_easing(keyframe);
    property.keyframes.push_back(keyframe);
    std::sort(property.keyframes.begin(), property.keyframes.end(),
              [](const Keyframe &left, const Keyframe &right) {
                  return left.time < right.time;
              });
}

inline void add_or_replace_keyframe(AnimatedVec2Property &property,
                                    double time, Vec2Value value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    property.static_value = value;
    for (auto &keyframe : property.keyframes) {
        if (std::abs(keyframe.time - time) <= kEpsilon) {
            keyframe.time = time;
            keyframe.value = value;
            return;
        }
    }
    VectorKeyframe keyframe;
    keyframe.time = time;
    keyframe.value = value;
    apply_linear_easing(keyframe);
    property.keyframes.push_back(keyframe);
    std::sort(property.keyframes.begin(), property.keyframes.end(),
              [](const VectorKeyframe &left, const VectorKeyframe &right) {
                  return left.time < right.time;
              });
}

inline void add_or_replace_keyframe(AnimatedVec3Property &property,
                                    double time, Vec3Value value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    property.static_value = value;
    for (auto &keyframe : property.keyframes) {
        if (std::abs(keyframe.time - time) <= kEpsilon) {
            keyframe.time = time;
            keyframe.value = value;
            return;
        }
    }
    Vector3Keyframe keyframe;
    keyframe.time = time;
    keyframe.value = value;
    apply_linear_easing(keyframe);
    keyframe.temporal_mode = TemporalInterpolationMode::Linear;
    property.keyframes.push_back(keyframe);
    std::sort(property.keyframes.begin(), property.keyframes.end(),
              [](const Vector3Keyframe &left, const Vector3Keyframe &right) {
                  return left.time < right.time;
              });
    property.recalculate_rove_times();
}

inline void set_animated_value(AnimatedProperty &property, double time,
                               double value)
{
    if (property.is_animated())
        add_or_replace_keyframe(property, time, value);
    else
        property.static_value = value;
}

inline void set_animated_value(AnimatedVec2Property &property, double time,
                               Vec2Value value)
{
    if (property.is_animated())
        add_or_replace_keyframe(property, time, value);
    else
        property.static_value = value;
}

inline void set_animated_value(AnimatedVec3Property &property, double time,
                               Vec3Value value)
{
    if (property.is_animated())
        add_or_replace_keyframe(property, time, value);
    else
        property.static_value = value;
}

inline bool keyframe_at_time(const AnimatedProperty &property, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    return std::any_of(property.keyframes.begin(), property.keyframes.end(),
                       [time](const Keyframe &keyframe) {
                           return std::abs(keyframe.time - time) <= kEpsilon;
                       });
}

inline bool keyframe_at_time(const AnimatedVec2Property &property, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    return std::any_of(property.keyframes.begin(), property.keyframes.end(),
                       [time](const VectorKeyframe &keyframe) {
                           return std::abs(keyframe.time - time) <= kEpsilon;
                       });
}

inline bool keyframe_at_time(const AnimatedVec3Property &property, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    return std::any_of(property.keyframes.begin(), property.keyframes.end(),
                       [time](const Vector3Keyframe &keyframe) {
                           return std::abs(keyframe.time - time) <= kEpsilon;
                       });
}

inline void remove_keyframe_at(AnimatedProperty &property, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    property.keyframes.erase(
        std::remove_if(property.keyframes.begin(), property.keyframes.end(),
                       [time](const Keyframe &keyframe) {
                           return std::abs(keyframe.time - time) <= kEpsilon;
                       }),
        property.keyframes.end());
}

inline void remove_keyframe_at(AnimatedVec2Property &property, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    property.keyframes.erase(
        std::remove_if(property.keyframes.begin(), property.keyframes.end(),
                       [time](const VectorKeyframe &keyframe) {
                           return std::abs(keyframe.time - time) <= kEpsilon;
                       }),
        property.keyframes.end());
}

inline void remove_keyframe_at(AnimatedVec3Property &property, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    property.keyframes.erase(
        std::remove_if(property.keyframes.begin(), property.keyframes.end(),
                       [time](const Vector3Keyframe &keyframe) {
                           return std::abs(keyframe.time - time) <= kEpsilon;
                       }),
        property.keyframes.end());
    property.recalculate_rove_times();
}

inline void toggle_keyframe(AnimatedProperty &property, double time,
                            double value)
{
    if (keyframe_at_time(property, time))
        remove_keyframe_at(property, time);
    else
        add_or_replace_keyframe(property, time, value);
}

inline void toggle_keyframe(AnimatedVec2Property &property, double time,
                            Vec2Value value)
{
    if (keyframe_at_time(property, time))
        remove_keyframe_at(property, time);
    else
        add_or_replace_keyframe(property, time, value);
}

inline void toggle_keyframe(AnimatedVec3Property &property, double time,
                            Vec3Value value)
{
    if (keyframe_at_time(property, time))
        remove_keyframe_at(property, time);
    else
        add_or_replace_keyframe(property, time, value);
}

inline bool any_keyframe_at_time(
    std::initializer_list<const AnimatedProperty *> properties, double time)
{
    for (const auto *property : properties)
        if (property && keyframe_at_time(*property, time))
            return true;
    return false;
}

inline bool any_keyframes(
    std::initializer_list<const AnimatedProperty *> properties)
{
    for (const auto *property : properties)
        if (property && property->is_animated())
            return true;
    return false;
}

} // namespace fxm::editor::animation_edit
