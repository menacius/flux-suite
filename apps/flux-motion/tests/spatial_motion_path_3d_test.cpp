#include "animation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool near(double a, double b, double epsilon = 1.0e-5)
{
    return std::abs(a - b) <= epsilon;
}

bool near_vec(const Vec3Value &a, const Vec3Value &b, double epsilon = 1.0e-5)
{
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) &&
           near(a.z, b.z, epsilon);
}

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

Vector3Keyframe linear_key(double time, Vec3Value value)
{
    Vector3Keyframe key;
    key.time = time;
    key.value = value;
    key.easing = EasingType::Linear;
    key.temporal_mode = TemporalInterpolationMode::Linear;
    key.spatial_mode = SpatialInterpolationMode::Linear;
    return key;
}

} // namespace

int main()
{
    AnimatedVec3Property path{"position_3d", {0.0, 0.0, 0.0}};
    path.keyframes = {
        linear_key(0.0, {0.0, 0.0, 0.0}),
        linear_key(1.0, {10.0, -20.0, 30.0}),
    };

    require(near_vec(path.evaluate(0.5), {5.0, -10.0, 15.0}),
            "linear XYZ interpolation");
    require(near(path.component_value(0.5, 0), 5.0), "separate X graph channel");
    require(near(path.component_value(0.5, 1), -10.0), "separate Y graph channel");
    require(near(path.component_value(0.5, 2), 15.0), "separate Z graph channel");
    require(path.component_velocity(0.5, 2) > 0.0, "Z velocity channel");

    path.keyframes[0].spatial_mode = SpatialInterpolationMode::ManualBezier;
    path.keyframes[1].spatial_mode = SpatialInterpolationMode::ManualBezier;
    path.keyframes[0].outgoing_tangent = {5.0, 10.0, 20.0};
    path.keyframes[1].incoming_tangent = {-5.0, 10.0, -10.0};
    const Vec3Value curved = path.evaluate(0.5);
    require(curved.y > -10.0, "3D Bezier Y handle affects path");
    require(!near(curved.z, 15.0), "3D Bezier Z handle affects path");

    const Vec3Value before_split = path.evaluate(0.37);
    const size_t inserted = path.split_spatial_segment(0, 0.37, 0.37);
    require(inserted == 1 && path.keyframes.size() == 3,
            "split preserves a full XYZ keyframe");
    require(near_vec(path.evaluate(0.37), before_split, 2.0e-4),
            "split preserves authored 3D curve");

    path.set_spatial_mode(1, SpatialInterpolationMode::ContinuousBezier);
    require(path.keyframes[1].spatial_tangents_linked,
            "continuous spatial tangents remain linked");
    path.set_spatial_mode(1, SpatialInterpolationMode::ManualBezier);
    require(!path.keyframes[1].spatial_tangents_linked,
            "manual spatial tangents can be independent");

    AnimatedVec3Property roving{"position_3d", {}};
    roving.keyframes = {
        linear_key(0.0, {0.0, 0.0, 0.0}),
        linear_key(0.25, {1.0, 0.0, 0.0}),
        linear_key(0.75, {9.0, 0.0, 0.0}),
        linear_key(1.0, {10.0, 0.0, 0.0}),
    };
    roving.set_rove_across_time(1, true);
    roving.set_rove_across_time(2, true);
    require(roving.keyframes[1].time < roving.keyframes[2].time,
            "roving keys keep a stable order");
    require(roving.keyframes[1].time < 0.5 && roving.keyframes[2].time > 0.5,
            "roving time follows 3D path distance");

    path.set_temporal_mode(0, TemporalInterpolationMode::ManualBezier);
    path.set_temporal_handle(0, false, 70.0, 0.0, true);
    require(path.keyframes[0].temporal_velocity_explicit,
            "temporal easing is independent from spatial interpolation");

    std::cout << "full XYZ motion paths, 3D Bezier handles, temporal/spatial interpolation, "
                 "separate graph channels, curve splitting, and roving keyframes passed\n";
    return 0;
}
