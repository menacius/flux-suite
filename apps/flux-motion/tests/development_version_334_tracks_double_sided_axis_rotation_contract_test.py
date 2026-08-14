#!/usr/bin/env python3
"""Dev334: authored-only tracks, text double-sided and axis-correct rotation."""

from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
transform = read("src/rendering/layer-transform-3d.cpp")
transform_h = read("src/rendering/layer-transform-3d.h")
gizmo = read("src/canvas/canvas-preview/editor-3d-tools.inc")
canvas_h = read("src/canvas/canvas-preview.h")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 334
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 334
assert manifest["development_version"] >= 334

# Every property track is authored-only. Object/layer owner rows remain visible.
assert "const bool show_camera_switches = title->active_camera.is_animated();" in hierarchy
assert "title->camera_switches_expanded && camera_switch.is_animated()" in hierarchy
assert hierarchy.count("if (!property.is_animated())") >= 2
assert "for (auto &light : title->lights)" in hierarchy
assert "if (!prop.is_animated()) continue;" in hierarchy
assert "authorable_camera_assignment" not in hierarchy
assert "authorable_material_property" not in hierarchy

# Double-sided must win for typography as it does for every planar layer.
assert "const bool readable_front_only" not in transform
assert "(!layer.backface_culling || layer.double_sided)" in transform
assert "including Text/Clock/Ticker" in transform

# Orientation is the base frame and Rotation XYZ is applied within it.
local_start = transform.index("QMatrix4x4 local_matrix(")
local_end = transform.index("std::vector<std::string> group_chain", local_start)
local_matrix = transform[local_start:local_end]
assert local_matrix.index("orientation.z") < local_matrix.index("rotation.z")
assert "parent * T(position) * Orientation * Rz * Ry * Rx" in transform_h

# Rings and applied rotation use scale-free bases with complete XYZ rebuild.
# Dev336 supersedes the original ray-plane angle acquisition with screen scrub.
for token in (
    "gizmo_rotation_basis",
    "gizmo_axis_world_direction",
    "gizmo_decompose_zyx_rotation",
    "QVector3D::crossProduct",
    "orientation_inverse * parent_inverse * delta",
    "write_rotation(layer->rotation_x, next.x)",
    "write_rotation(layer->rotation_y, next.y)",
    "write_rotation(layer->rotation, next.z)",
):
    assert token in gizmo or token in canvas_h
assert "const bool local_rotation" not in gizmo
assert "v53-axis-space-double-sided" in cache_abi

test_name = (
    "tests/development_version_334_tracks_double_sided_axis_rotation_contract_test.py"
)
for area in ("editor_gui", "timeline_graph", "rendering_2d_3d", "platform_build"):
    assert test_name in manifest["areas"][area]["python"]

print("Dev334 tracks/double-sided/axis rotation contract passed")
