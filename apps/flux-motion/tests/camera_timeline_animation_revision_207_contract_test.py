#!/usr/bin/env python3
"""Source contract for the compatibility-first Development Version 207 rebuild."""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def digest(path: str) -> str:
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
animation_h = read("Core/animation.h")
animation_cpp = read("Core/animation.cpp")
title_h = read("Core/title-data.h")
title_cpp = read("src/core/title-data.cpp")
layer_h = read("Core/layer-model.h")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
timeline_h = read("src/timeline/timeline-widget.h")
timeline_cpp = read("src/timeline/timeline-widget.cpp")
graph = read("src/timeline/temporal-graph-editor.inc")
stack_h = read("src/layers/layer-stack-widget.h")
stack_cpp = read("src/layers/layer-stack-widget.cpp")
commands = read("src/editor/title-editor/commands-docks.inc")
properties_h = read("src/editor/title-properties-panel.h")
properties_cpp = read("src/editor/title-properties-panel.cpp")
transform_h = read("src/rendering/layer-transform-3d.h")
transform_cpp = read("src/rendering/layer-transform-3d.cpp")
cache_storage = read("src/cache/cache-manager/disk-cache-storage.inc")
cache_policy = read("src/cache/cache-manager/cache-policy-invalidation.inc")
cache_keying = read("src/cache/cache-manager/visual-hash-keying.inc")
source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
three_d = read("docs/EDITOR_WORKFLOW.md")

# Version and migration continuity.
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)
assert "case 207:" in schema and "migrate_camera_timeline_animation" in schema

# Discrete Hold-keyframe primitive used by switches and assignments.
assert "struct DiscreteKeyframe" in animation_h
assert "struct AnimatedDiscreteProperty" in animation_h
for token in (
    "AnimatedDiscreteProperty::evaluate",
    "AnimatedDiscreteProperty::set",
    "AnimatedDiscreteProperty::remove",
    "AnimatedDiscreteProperty::sort_keyframes",
):
    assert token in animation_cpp

# Persistent camera/title/layer data.
for token in ("orientation_x", "orientation_y", "orientation_z", "projection_mode"):
    assert token in title_h
assert 'AnimatedDiscreteProperty active_camera { "active_camera", "default" }' in title_h
assert 'AnimatedDiscreteProperty camera_assignment { "camera_assignment", "" }' in layer_h
assert 'jt["active_camera"] = discrete_property_to_json(t.active_camera)' in title_cpp
assert 'j["camera_assignment"] = discrete_property_to_json(l.camera_assignment)' in title_cpp
assert 'j["camera_id"] = l.camera_assignment.static_value' in title_cpp
assert 'jt["active_camera_id"] = t.active_camera.static_value' in title_cpp
assert 'l->camera_id = l->camera_assignment.static_value' in title_cpp
assert 't->active_camera_id = t->active_camera.static_value' in title_cpp
assert '{"projection_mode", aprop_to_json(camera.projection_mode)}' in title_cpp
assert '{"orientation_x", aprop_to_json(camera.orientation_x)}' in title_cpp

# AE-style owner rows: collapsed by default, channels only after disclosure.
for token in (
    "kTitleCameraSwitchOwner",
    "camera_timeline_owner_id",
    "timeline_camera_properties",
    "timeline_property_for_owner",
    "timeline_owner_proxy",
    "Camera Switches",
):
    assert token in hierarchy
for token in (
    "camera_position_x", "camera_position_y", "camera_position_z",
    "camera_target_x", "camera_target_y", "camera_target_z",
    "camera_orientation_x", "camera_orientation_y", "camera_orientation_z",
    "camera_rotation_x", "camera_rotation_y", "camera_rotation_z",
    "camera_focal_length", "camera_field_of_view", "camera_zoom",
    "camera_near_clip", "camera_far_clip", "camera_projection",
):
    assert token in hierarchy
assert "camera_switches_expanded" in title_h
assert "camera.timeline_expanded" in hierarchy
assert "camera_expand_changed" in stack_h and "camera_expand_changed" in stack_cpp
assert "is_hold_only" in hierarchy and 'scalar->name == "camera_projection"' in hierarchy
assert "AnimatedDiscreteProperty *discrete" in hierarchy

# Timeline/Graph Editor operations use owner IDs rather than requiring a Layer.
assert "hit_owner_id" in timeline_cpp
assert "timeline_owner_locked" in timeline_cpp
assert "find_timeline_property(dragged.ref.layer_id" in timeline_cpp
assert "ClipboardKeyframe" in timeline_h and "discrete_key" in timeline_h
assert "timeline_owner_proxy" in graph and "graph_eligible" in graph
assert "timeline_property_for_owner" in stack_cpp
assert "timeline_owner_in_time" in stack_cpp
assert "timeline_property_for_owner" in commands

# Inspector playhead evaluation and independent Orientation controls.
for token in (
    "spn_camera_orientation_x_",
    "spn_camera_orientation_y_",
    "spn_camera_orientation_z_",
):
    assert token in properties_h and token in properties_cpp
assert "set_playhead(double timeline_time)" in properties_cpp
assert "camera->projection_mode" in properties_cpp
assert "TemporalInterpolationMode::Hold" in properties_cpp

# Minimal render integration. Static mirrors stay authoritative until keyed.
for token in (
    "resolved_active_camera_id",
    "resolved_layer_camera_id",
    "evaluated_camera_projection",
):
    assert token in transform_h and token in transform_cpp
assert "if (!title.active_camera.is_animated())\n        return title.active_camera_id;" in transform_cpp
assert "if (!layer.camera_assignment.is_animated())\n        return layer.camera_id;" in transform_cpp
assert "if (!camera.projection_mode.is_animated())\n        return camera.projection;" in transform_cpp
assert "Keep the Development Version 206 rotation path literally intact" in transform_cpp
assert "forward = orientation.rotatedVector(forward);" in transform_cpp
assert "up = orientation.rotatedVector(up);" in transform_cpp
assert "if (has_axis_orientation)" in transform_cpp
assert "1000.0 / focal" not in transform_cpp
assert "layer->camera_assignment.is_animated()" in transform_cpp
assert "layer.camera_assignment.is_animated()" in source_runtime

# Cache identity changes only for actual authored camera animation data.
assert "add_discrete(title.active_camera)" in cache_storage
assert "add_discrete(layer->camera_assignment)" in cache_storage
assert "camera.orientation_x" in cache_storage and "camera.projection_mode" in cache_storage
assert "add_discrete(title.active_camera)" in cache_policy
assert "add_discrete(layer->camera_assignment)" in cache_policy
assert "v38-camera-timeline-animation-static-compatible" in cache_keying

# The compatibility text raster core remains stable. The two orchestration files
# legitimately gained Video/Image routing in Development Version 231; their
# release hashes protect the existing camera/text path from accidental edits.
version_206_hashes = {
    "src/editor/title-editor-internal/text-layout-rendering.inc": "6b2275e65b7117c16d1207c0c6e55f3d4cecd2a0d486df3444e540f6c81f90f3",
    "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc": "a02df4e9802c272d009b0dba0d6ff2e47e9c53c0299e956ae7d13f70271ec41a",
    "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc": "f010da7b8409359632ea259d92c7a82628be7998be282f5079e1b3786e720434",
    "src/rendering/title-gpu-text-sdf.cpp": "6bc62749496b7c276397a81ecd27d014410709b884f6d9aea3cce89f967232b1",
}
for path, expected in version_206_hashes.items():
    assert digest(path) == expected, f"Version 206 raster/preview baseline changed: {path}"

# Documentation states the compatibility boundary and next milestone clearly.
assert "Cameras are first-class animated timeline objects" in readme
assert "unchanged legacy 2D path" in readme
assert "Development Version 207 — Camera Timeline and Animation" in changelog
assert "Camera Switches" in three_d
assert "Full XYZ spatial motion paths" in three_d
assert "Static cameras and unkeyed assignments retain the same render/cache behavior" in three_d

editor_3d = read("src/canvas/canvas-preview/editor-3d-tools.inc")
assert "resolved_layer_camera_id" in editor_3d

print("Development Version 207 compatibility-first camera timeline contract passed")
