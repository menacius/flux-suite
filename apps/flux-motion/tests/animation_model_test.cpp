#include "animation.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

static bool near(double actual, double expected, double epsilon = 1e-9)
{
    return std::abs(actual - expected) < epsilon;
}

static bool near_vec(const Vec2Value &actual, const Vec2Value &expected,
                     double epsilon = 1e-9)
{
    return near(actual.x, expected.x, epsilon) &&
           near(actual.y, expected.y, epsilon);
}

int main()
{
    AnimatedProperty opacity{"opacity", 0.25};
    assert(near(opacity.evaluate(1.0), 0.25));

    opacity.keyframes.push_back({0.0, 0.0, EasingType::Linear});
    opacity.keyframes.push_back({2.0, 1.0, EasingType::Linear});
    assert(near(opacity.evaluate(-1.0), 0.0));
    assert(near(opacity.evaluate(1.0), 0.5));
    assert(near(opacity.evaluate(3.0), 1.0));

    opacity.keyframes[0].easing = EasingType::Hold;
    assert(near(opacity.evaluate(1.0), 0.0));

    opacity.keyframes[0].easing = EasingType::EaseInOut;
    const double eased_midpoint = opacity.evaluate(1.0);
    assert(eased_midpoint > 0.0 && eased_midpoint < 1.0);

    /* Legacy vector keys have no spatial metadata and therefore stay linear. */
    AnimatedVec2Property legacy_position{"position", {0.0, 0.0}};
    VectorKeyframe legacy_start;
    legacy_start.time = 0.0;
    legacy_start.value = {0.0, 0.0};
    legacy_start.easing = EasingType::Linear;
    VectorKeyframe legacy_end;
    legacy_end.time = 2.0;
    legacy_end.value = {100.0, 100.0};
    legacy_end.easing = EasingType::Linear;
    legacy_position.keyframes = {legacy_start, legacy_end};
    assert(near_vec(legacy_position.evaluate(1.0), {50.0, 50.0}));

    /* Manual spatial tangents bend the path without changing temporal easing. */
    AnimatedVec2Property curved_position{"position", {0.0, 0.0}};
    VectorKeyframe curve_start;
    curve_start.time = 0.0;
    curve_start.value = {0.0, 0.0};
    curve_start.easing = EasingType::Linear;
    curve_start.spatial_mode = SpatialInterpolationMode::ManualBezier;
    curve_start.outgoing_tangent = {0.0, 100.0};
    curve_start.spatial_tangents_linked = false;
    VectorKeyframe curve_end;
    curve_end.time = 1.0;
    curve_end.value = {100.0, 0.0};
    curve_end.easing = EasingType::Linear;
    curve_end.spatial_mode = SpatialInterpolationMode::ManualBezier;
    curve_end.incoming_tangent = {0.0, 100.0};
    curve_end.spatial_tangents_linked = false;
    curved_position.keyframes = {curve_start, curve_end};

    const Vec2Value curved_midpoint = curved_position.evaluate(0.5);
    assert(near(curved_midpoint.x, 50.0));
    assert(near(curved_midpoint.y, 75.0));
    assert(near_vec(curved_position.evaluate(0.5), curved_midpoint));

    /* Temporal easing maps time to progress before the spatial curve is
     * evaluated; changing temporal easing does not rewrite path geometry. */
    curved_position.keyframes[0].easing = EasingType::EaseIn;
    assert(near_vec(curved_position.evaluate(0.5),
                    curved_position.evaluate_spatial_segment(0, 0.25)));
    curved_position.keyframes[0].easing = EasingType::Hold;
    assert(near_vec(curved_position.evaluate(0.5), curve_start.value));
    curved_position.keyframes[0].easing = EasingType::Linear;

    /* Linear mode ignores any stale stored handles, preserving exact legacy
     * straight-line interpolation even after mode round-trips. */
    AnimatedVec2Property linear_with_handles = curved_position;
    for (VectorKeyframe &keyframe : linear_with_handles.keyframes)
        keyframe.spatial_mode = SpatialInterpolationMode::Linear;
    assert(near_vec(linear_with_handles.evaluate(0.5), {50.0, 0.0}));

    /* Auto Bezier is deterministic and produces resolved local-space handles. */
    AnimatedVec2Property auto_position{"position", {0.0, 0.0}};
    VectorKeyframe auto_a;
    auto_a.time = 0.0;
    auto_a.value = {0.0, 0.0};
    auto_a.easing = EasingType::Linear;
    auto_a.spatial_mode = SpatialInterpolationMode::AutoBezier;
    VectorKeyframe auto_b;
    auto_b.time = 1.0;
    auto_b.value = {100.0, 100.0};
    auto_b.easing = EasingType::Linear;
    auto_b.spatial_mode = SpatialInterpolationMode::AutoBezier;
    VectorKeyframe auto_c;
    auto_c.time = 2.0;
    auto_c.value = {200.0, 0.0};
    auto_c.easing = EasingType::Linear;
    auto_c.spatial_mode = SpatialInterpolationMode::AutoBezier;
    auto_position.keyframes = {auto_a, auto_b, auto_c};
    const Vec2Value auto_first = auto_position.evaluate(0.25);
    const Vec2Value auto_second = auto_position.evaluate(0.25);
    assert(near_vec(auto_first, auto_second));
    assert(!near(auto_first.y, 25.0));

    /* Applying an affine parent/group transform after local evaluation is
     * equivalent to transforming key positions as points and tangents as
     * vectors. This is the local-space invariance required by hierarchy and
     * nested-composition placement. */
    auto affine_point = [](const Vec2Value &p) {
        return Vec2Value{2.0 * p.x - 0.5 * p.y + 30.0,
                         0.25 * p.x + 1.5 * p.y - 12.0};
    };
    auto affine_vector = [](const Vec2Value &v) {
        return Vec2Value{2.0 * v.x - 0.5 * v.y,
                         0.25 * v.x + 1.5 * v.y};
    };
    AnimatedVec2Property transformed_position = curved_position;
    transformed_position.static_value = affine_point(curved_position.static_value);
    for (VectorKeyframe &keyframe : transformed_position.keyframes) {
        keyframe.value = affine_point(keyframe.value);
        keyframe.incoming_tangent = affine_vector(keyframe.incoming_tangent);
        keyframe.outgoing_tangent = affine_vector(keyframe.outgoing_tangent);
    }
    const Vec2Value local = curved_position.evaluate(0.37);
    assert(near_vec(transformed_position.evaluate(0.37), affine_point(local), 1e-8));

    /* Splitting a spatial cubic with de Casteljau adds an editable vertex
     * without changing the authored path or its linear-time playback. */
    AnimatedVec2Property split_curve = curved_position;
    const Vec2Value before_split_20 = split_curve.evaluate(0.20);
    const Vec2Value before_split_50 = split_curve.evaluate(0.50);
    const Vec2Value before_split_80 = split_curve.evaluate(0.80);
    const size_t inserted_index = split_curve.split_spatial_segment(0, 0.5, 0.5);
    assert(inserted_index == 1);
    assert(split_curve.keyframes.size() == 3);
    assert(near_vec(split_curve.keyframes[1].value, before_split_50, 1e-8));
    assert(near_vec(split_curve.evaluate(0.20), before_split_20, 1e-8));
    assert(near_vec(split_curve.evaluate(0.50), before_split_50, 1e-8));
    assert(near_vec(split_curve.evaluate(0.80), before_split_80, 1e-8));

    /* Roving interior vertices are timed by deterministic local-space arc
     * length between the surrounding fixed-time anchors. */
    AnimatedVec2Property roving_position{"position", {0.0, 0.0}};
    for (const auto &[time, x] : std::initializer_list<std::pair<double, double>>{
             {0.0, 0.0}, {1.0, 10.0}, {2.0, 40.0}, {10.0, 100.0}}) {
        VectorKeyframe keyframe;
        keyframe.time = time;
        keyframe.value = {x, 0.0};
        keyframe.easing = EasingType::Linear;
        keyframe.spatial_mode = SpatialInterpolationMode::Linear;
        roving_position.keyframes.push_back(keyframe);
    }
    roving_position.set_rove_across_time(1, true);
    roving_position.set_rove_across_time(2, true);
    assert(near(roving_position.keyframes[1].time, 1.0, 1e-8));
    assert(near(roving_position.keyframes[2].time, 4.0, 1e-8));
    assert(roving_position.keyframes[1].rove_across_time);
    assert(roving_position.keyframes[2].rove_across_time);
    roving_position.set_rove_across_time(0, true);
    assert(!roving_position.keyframes.front().rove_across_time);

    /* Interpolation mode changes may reshape adjacent segments, but never
     * rewrite the keyframe vertex itself. */
    const Vec2Value stable_vertex = split_curve.keyframes[1].value;
    split_curve.set_spatial_mode(1, SpatialInterpolationMode::AutoBezier);
    assert(near_vec(split_curve.keyframes[1].value, stable_vertex));
    split_curve.set_spatial_mode(1, SpatialInterpolationMode::ContinuousBezier);
    assert(near_vec(split_curve.keyframes[1].value, stable_vertex));
    split_curve.set_spatial_mode(1, SpatialInterpolationMode::Linear);
    assert(near_vec(split_curve.keyframes[1].value, stable_vertex));

    AnimatedVec2Property editable_linear = legacy_position;
    editable_linear.set_spatial_mode(0, SpatialInterpolationMode::ManualBezier);
    assert(std::hypot(editable_linear.keyframes[0].outgoing_tangent.x,
                      editable_linear.keyframes[0].outgoing_tangent.y) > 1e-9);
    assert(!editable_linear.keyframes[0].spatial_tangents_linked);

    /* Manual temporal velocity handles are evaluated in real time/value space,
     * preserve authored overshoot, and never clamp property values. */
    TemporalBezierSegment temporal_overshoot;
    temporal_overshoot.start_time = 0.0;
    temporal_overshoot.end_time = 1.0;
    temporal_overshoot.start_value = -10.0;
    temporal_overshoot.end_value = 10.0;
    temporal_overshoot.outgoing_influence = 0.45;
    temporal_overshoot.incoming_influence = 0.35;
    temporal_overshoot.outgoing_speed = 120.0;
    temporal_overshoot.incoming_speed = -80.0;
    bool overshot = false;
    for (int sample = 1; sample < 100; ++sample) {
        const double time = sample / 100.0;
        const double value = evaluate_temporal_segment(temporal_overshoot, time);
        assert(std::isfinite(value));
        assert(std::isfinite(evaluate_temporal_segment_velocity(temporal_overshoot, time)));
        overshot = overshot || value > 10.0 || value < -10.0;
    }
    assert(overshot);
    for (int sample = 2; sample < 98; sample += 3) {
        const double time = sample / 100.0;
        const double h = 1.0e-6;
        const double numeric =
            (evaluate_temporal_segment(temporal_overshoot, time + h) -
             evaluate_temporal_segment(temporal_overshoot, time - h)) / (2.0 * h);
        const double analytic =
            evaluate_temporal_segment_velocity(temporal_overshoot, time);
        assert(near(numeric, analytic, 2.0e-3));
    }

    /* Explicit scalar handles produce custom easing, including negative values,
     * while Easy Ease commands set only the requested temporal side. */
    AnimatedProperty manual_value{"manual", -50.0};
    Keyframe manual_start;
    manual_start.time = 0.0;
    manual_start.value = -50.0;
    manual_start.easing = EasingType::Linear;
    Keyframe manual_end;
    manual_end.time = 1.0;
    manual_end.value = 50.0;
    manual_end.easing = EasingType::Linear;
    manual_value.keyframes = {manual_start, manual_end};
    manual_value.set_temporal_mode(0, TemporalInterpolationMode::ManualBezier);
    manual_value.set_temporal_mode(1, TemporalInterpolationMode::ManualBezier);
    manual_value.set_temporal_handle(0, false, 65.0, 260.0, true);
    manual_value.set_temporal_handle(1, true, 45.0, -140.0, true);
    assert(!manual_value.keyframes[0].temporal_tangents_linked);
    assert(manual_value.evaluate(0.25) != -25.0);
    assert(std::isfinite(manual_value.velocity(0.5)));
    assert(near(manual_value.evaluate(0.5), manual_value.evaluate(0.5), 1e-12));

    manual_value.apply_easy_ease(0, false, true);
    assert(near(manual_value.keyframes[0].outgoing_speed, 0.0));
    assert(!near(manual_value.keyframes[0].incoming_speed, 0.0) ||
           !manual_value.keyframes[0].temporal_tangents_linked);
    manual_value.apply_easy_ease(1, true, false);
    assert(near(manual_value.keyframes[1].incoming_speed, 0.0));

    /* Linked temporal handles move as a pair; Alt/break-style edits preserve
     * the opposite side and break only the selected keyframe pair. */
    AnimatedProperty linked_value{"linked", 0.0};
    linked_value.keyframes = {{0.0, 0.0, EasingType::Linear},
                              {1.0, 10.0, EasingType::Linear},
                              {2.0, 20.0, EasingType::Linear}};
    linked_value.set_temporal_mode(1, TemporalInterpolationMode::ContinuousBezier);
    linked_value.set_temporal_handle(1, false, 42.0, 8.0, false);
    assert(near(linked_value.keyframes[1].incoming_influence, 42.0));
    assert(near(linked_value.keyframes[1].incoming_speed, 8.0));
    const double preserved_in_speed = linked_value.keyframes[1].incoming_speed;
    linked_value.set_temporal_handle(1, false, 25.0, -4.0, true);
    assert(!linked_value.keyframes[1].temporal_tangents_linked);
    assert(near(linked_value.keyframes[1].incoming_speed, preserved_in_speed));
    assert(near(linked_value.keyframes[1].outgoing_speed, -4.0));

    /* Crossed temporal influences remain authored exactly (CSS/AE-style x
     * controls are valid independently in 0..100) so graph handles and final
     * evaluation agree instead of silently renormalizing the pair. */
    TemporalBezierSegment crossed;
    crossed.start_time = 0.0; crossed.end_time = 1.0;
    crossed.start_value = 0.0; crossed.end_value = 10.0;
    crossed.outgoing_influence = 0.9; crossed.incoming_influence = 0.9;
    crossed.outgoing_speed = 3.0; crossed.incoming_speed = -2.0;
    assert(near(evaluate_temporal_segment_velocity(crossed, 0.0), 3.0, 1e-7));
    assert(near(evaluate_temporal_segment_velocity(crossed, 1.0), -2.0, 1e-7));
    assert(std::isfinite(evaluate_temporal_segment(crossed, 0.5)));

    /* Very short keyframe intervals remain finite and deterministic. */
    AnimatedProperty short_interval{"short", 0.0};
    Keyframe short_a;
    short_a.time = 0.0;
    short_a.value = -1000.0;
    short_a.temporal_velocity_explicit = true;
    short_a.temporal_mode = TemporalInterpolationMode::ManualBezier;
    short_a.outgoing_influence = 95.0;
    short_a.outgoing_speed = 1.0e10;
    Keyframe short_b = short_a;
    short_b.time = 1.0e-8;
    short_b.value = 1000.0;
    short_b.incoming_influence = 95.0;
    short_b.incoming_speed = -1.0e10;
    short_interval.keyframes = {short_a, short_b};
    const double short_value = short_interval.evaluate(5.0e-9);
    const double short_speed = short_interval.velocity(5.0e-9);
    assert(std::isfinite(short_value));
    assert(std::isfinite(short_speed));
    assert(near(short_value, short_interval.evaluate(5.0e-9), 1e-9));

    TemporalBezierSegment sub_epsilon;
    sub_epsilon.start_time = 1.0;
    sub_epsilon.end_time = 1.0 + 1.0e-12;
    sub_epsilon.start_value = -7.0;
    sub_epsilon.end_value = 9.0;
    assert(near(evaluate_temporal_segment(sub_epsilon, 1.0), -7.0));
    assert(near(evaluate_temporal_segment(sub_epsilon, sub_epsilon.end_time), 9.0));
    assert(near(evaluate_temporal_segment_velocity(sub_epsilon, 1.0), 0.0));

    /* Position uses the same temporal evaluator before spatial geometry. The
     * reported speed graph samples therefore agree with final motion values. */
    AnimatedVec2Property temporal_position{"position", {0.0, 0.0}};
    VectorKeyframe temporal_a;
    temporal_a.time = 0.0;
    temporal_a.value = {0.0, 0.0};
    temporal_a.easing = EasingType::Linear;
    temporal_a.spatial_mode = SpatialInterpolationMode::Linear;
    VectorKeyframe temporal_b = temporal_a;
    temporal_b.time = 1.0;
    temporal_b.value = {100.0, 0.0};
    temporal_position.keyframes = {temporal_a, temporal_b};
    temporal_position.apply_easy_ease(0, false, true);
    temporal_position.apply_easy_ease(1, true, false);
    assert(temporal_position.evaluate(0.25).x < 25.0);
    assert(near(temporal_position.evaluate(0.5).x, 50.0, 1e-7));
    assert(std::isfinite(temporal_position.speed(0.5)));
    assert(near(temporal_position.path_value(0.5), 50.0, 1e-5));

    /* Camera switching and camera assignment are discrete Hold tracks. They
     * preserve their static baseline before the first key, sort authoring
     * operations deterministically, replace coincident keys, and remove using
     * the same frame-sized tolerance used by the timeline. */
    AnimatedDiscreteProperty camera_switch{"active_camera", "default"};
    assert(camera_switch.evaluate(0.0) == "default");
    camera_switch.set(2.0, "camera-c");
    camera_switch.set(1.0, "camera-b");
    assert(camera_switch.keyframes.size() == 2);
    assert(camera_switch.keyframes[0].time == 1.0);
    assert(camera_switch.evaluate(0.5) == "default");
    assert(camera_switch.evaluate(1.5) == "camera-b");
    assert(camera_switch.evaluate(2.0) == "camera-c");
    camera_switch.set(1.0, "camera-b-updated");
    assert(camera_switch.keyframes.size() == 2);
    assert(camera_switch.evaluate(1.5) == "camera-b-updated");
    camera_switch.remove(1.0);
    assert(camera_switch.keyframes.size() == 1);
    assert(camera_switch.evaluate(1.5) == "default");
    assert(camera_switch.evaluate(std::numeric_limits<double>::quiet_NaN()) ==
           "default");

    std::cout << "animation model temporal/spatial separation, legacy linear compatibility, "
                 "manual cubic paths, deterministic auto tangents, local-space transforms, "
                 "curve splitting, stable mode changes, roving timing, manual temporal velocity, "
                 "negative/overshoot values, short intervals, and shared graph evaluation passed\n";
    return 0;
}
