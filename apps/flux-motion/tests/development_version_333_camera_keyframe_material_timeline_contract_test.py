#!/usr/bin/env python3
"""Dev333: camera inspector keyframes and authored-only timeline tracks."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
header = read("src/editor/title-properties-panel.h")
panel = read("src/editor/title-properties-panel.cpp")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
title_data = read("Core/title-data.h")
docks = read("src/editor/title-editor/commands-docks.inc")

assert int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 333
assert int(build.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 333
assert manifest["development_version"] >= 333

camera_buttons = (
    "projection", "position", "target", "orientation", "rotation",
    "focal", "fov", "zoom", "near", "far",
)
for name in camera_buttons:
    member = f"btn_kf_camera_{name}_"
    assert member in header
    assert member in panel

assert "make_camera_keyframe_button" in panel
assert "make_camera_keyframed_row" in panel
assert "connect_camera_vector_keyframe(btn_kf_camera_position_, false);" in panel
assert "connect_camera_vector_keyframe(btn_kf_camera_target_, true);" in panel
assert "connect_camera_scalar_group_keyframe(" in panel
assert panel.count("connect_camera_scalar_keyframe(btn_kf_camera_") >= 5
assert "toggle_keyframe(camera->projection_mode" in panel
assert "key.temporal_mode = TemporalInterpolationMode::Hold;" in panel
assert "set_camera_keyframe_icon" in panel

# The shared row model owns default-camera visibility for both list and timeline.
assert "if (default_camera && !title_camera_has_authored_keyframes(camera))" in hierarchy
assert "if (layers_) layers_->refresh();" in docks
assert "camera.position_3d_path_enabled" in title_data
assert "camera.target_3d_path_enabled" in title_data

# A 3D material channel no longer bypasses the authored-keyframe filter.
assert "authorable_material_property" not in hierarchy
assert "if (!prop.is_animated()) continue;" in hierarchy

for area in ("editor_gui", "timeline_graph", "platform_build"):
    assert (
        "tests/development_version_333_camera_keyframe_material_timeline_contract_test.py"
        in manifest["areas"][area]["python"]
    )

print("Dev333 camera keyframe/material timeline contract passed")
