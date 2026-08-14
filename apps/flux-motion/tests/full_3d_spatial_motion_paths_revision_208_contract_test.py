#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing Development Version 208 contract: {label} ({needle})")


def main() -> None:
    cmake = read("CMakeLists.txt")
    build = read("src/core/build-info.h")
    schema = read("Core/title-serialization-schema.h")
    animation_h = read("Core/animation.h")
    animation_cpp = read("Core/animation.cpp")
    layer = read("Core/layer-model.h")
    title_h = read("Core/title-data.h")
    title_cpp = read("src/core/title-data.cpp")
    hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
    timeline_h = read("src/timeline/timeline-widget.h")
    timeline_cpp = read("src/timeline/timeline-widget.cpp")
    canvas = read("src/canvas/canvas-preview/spatial-bezier-keyframes.inc")
    canvas_h = read("src/canvas/canvas-preview.h")
    tools = read("src/canvas/canvas-preview/editor-3d-tools.inc")
    transform_h = read("src/rendering/layer-transform-3d.h")
    transform_cpp = read("src/rendering/layer-transform-3d.cpp")
    cache = read("src/cache/cache-manager/disk-cache-storage.inc")
    invalidation = read("src/cache/cache-manager/cache-policy-invalidation.inc")
    readme = read("README.md")
    changelog = read("docs/CHANGELOG.md")
    guide = read("docs/EDITOR_WORKFLOW.md")

    require(cmake, 'OBS_FXM_DEVELOPMENT_VERSION "243"', "CMake development version")
    require(build, 'FXM_DEVELOPMENT_VERSION "243"', "runtime development version")
    require(schema, "kCurrentDevelopmentVersion = 243", "serialization version")
    require(schema, "case 208:", "contiguous migration entry")
    require(schema, "migrate_full_3d_spatial_motion_paths", "208 migration")

    for needle, label in [
        ("struct Vec3Value", "XYZ value"),
        ("struct Vector3Keyframe", "complete 3D keyframe"),
        ("Vec3Value incoming_tangent", "incoming 3D handle"),
        ("Vec3Value outgoing_tangent", "outgoing 3D handle"),
        ("bool rove_across_time", "roving state"),
        ("struct AnimatedVec3Property", "unified Vector3 property"),
        ("double component_value", "separate graph component value"),
        ("double component_velocity", "separate graph component speed"),
        ("split_spatial_segment", "curve-preserving insertion"),
        ("recalculate_rove_times", "roving time calculation"),
    ]:
        require(animation_h, needle, label)
    require(animation_cpp, "AnimatedVec3Property::evaluate_spatial_segment", "XYZ Bezier evaluation")
    require(animation_cpp, "AnimatedVec3Property::set_spatial_mode", "Auto/Continuous/Manual modes")
    require(animation_cpp, "AnimatedVec3Property::recalculate_rove_times", "distance-based roving")

    require(layer, 'AnimatedVec3Property position_3d { "position_3d"', "3D layer Position track")
    require(layer, "promote_layer_position_to_3d_path", "legacy Position promotion")
    require(title_h, 'AnimatedVec3Property position_3d { "camera_position_3d"', "camera Position track")
    require(title_h, 'AnimatedVec3Property target_3d { "camera_target_3d"', "camera POI track")
    require(title_h, "promote_camera_spatial_tracks", "camera spatial promotion")
    require(layer, "promote_layer_position_to_3d_path(Layer &layer, bool activate = true)", "opt-in layer Vector3 activation")
    require(title_h, "promote_camera_spatial_tracks(TitleCamera &camera, bool activate = true)", "opt-in camera Vector3 activation")
    require(hierarchy, "bool *vector3_enabled", "timeline authority flag")
    require(hierarchy, "promote_layer_position_to_3d_path(layer, false)", "non-authoritative legacy layer view")
    require(hierarchy, "promote_camera_spatial_tracks(camera, false)", "non-authoritative legacy camera view")

    require(title_cpp, "vec3_aprop_to_json", "Vector3 serialization")
    require(title_cpp, "vec3_aprop_from_json", "Vector3 deserialization")
    require(title_cpp, 'j["position_3d"]', "layer Vector3 persistence")
    require(title_cpp, 'result["target_3d"]', "camera target Vector3 persistence")

    require(hierarchy, "AnimatedVec3Property *vector3", "timeline Vector3 property reference")
    require(hierarchy, "vector3_component", "X/Y/Z timeline component selection")
    require(hierarchy, 'layer.position_3d, 0, "position", &layer.position_3d_path_enabled', "aggregate Position row")
    require(hierarchy, 'camera.target_3d, 0, "camera_target"', "aggregate camera POI row")
    require(hierarchy, "vector3_component", "Graph Editor component selection remains available")
    if '"position_x", &layer.position_3d_path_enabled' in hierarchy or '"position_y", &layer.position_3d_path_enabled' in hierarchy:
        raise AssertionError("Development Version 212 must not restore separate XYZ timeline rows")
    require(hierarchy, "keyframe_spatial_mode", "spatial interpolation commands")
    require(hierarchy, "keyframe_roves_across_time", "timeline roving command")

    require(timeline_h, "Vector3Keyframe vector3_keyframe", "clipboard Vector3 payload")
    require(timeline_h, "bool is_vector3", "clipboard type marker")
    require(timeline_cpp, "entry.vector3_keyframe", "copy/paste full 3D keyframe")
    require(timeline_cpp, "pasted.time", "destination-time-only paste")
    require(timeline_cpp, "seen_vector3_keys", "shared X/Y/Z key deduplication")
    require(timeline_cpp, "vector3_grouped", "shared-track delete deduplication")

    require(canvas_h, "SpatialTangentDragState", "3D spatial tangent interaction")
    require(canvas, "position_path_is_3d", "XYZ path selection")
    require(canvas, "layer_parent_world_matrix", "parent-space to world-space path")
    require(canvas, "project_world_point", "world-space path projection")
    require(canvas, "canvas_point_on_world_plane", "camera-aware 3D handle unprojection")
    require(canvas, "split_spatial_segment", "on-path keyframe insertion")
    require(canvas, "RoveAcrossTime", "roving UI")
    require(transform_h, "canvas_world_ray", "shared renderer/editor world ray")
    require(transform_cpp, "canvas_point_on_world_plane", "world-plane intersection")
    require(tools, "position_3d.keyframes", "Frame Selected includes spatial keys")
    require(tools, "kFramePathSamplesPerSegment", "Frame Selected includes curved path samples")

    require(cache, "add_anim_vec3", "content hash includes 3D keyframes")
    require(cache, "add_vec3(layer->position_3d)", "visual hash includes evaluated XYZ")
    require(invalidation, "add_vec3(layer->position_3d", "runtime visual invalidation includes XYZ")

    require(readme, "Full 3D spatial motion paths", "README release entry")
    require(readme, "Full 3D spatial motion paths", "README feature section")
    require(changelog, "Development Version 208 — Full 3D Spatial Motion Paths", "changelog section")
    require(guide, "Temporal and spatial interpolation", "maintained 208 guide")
    require(guide, "parent space", "parent/world-space contract")

    print("Development Version 208 full 3D spatial motion-path contract passed")


if __name__ == "__main__":
    main()
