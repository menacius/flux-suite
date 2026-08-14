#!/usr/bin/env python3
"""Dev340: inserted 3D assets retain their authored camera/composition space."""

from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
model = read("Core/layer-model.h")
title_model = read("Core/title-data.h")
serialization = read("src/core/title-data.cpp")
insertion = read("src/editor/title-editor/layout-template-tools.inc")
transform = read("src/rendering/layer-transform-3d.cpp")
title_properties = read("src/editor/title-properties-panel.cpp")
layer_properties = read("src/editor/properties-panel/selection-refresh.inc")
timeline = read("src/editor/title-editor-internal/hierarchy-model.inc")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 340
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 340
assert manifest["development_version"] >= 340

# New Asset Layers opt into a versioned, self-contained source-space snapshot.
for field in (
    "asset_isolated_3d_space",
    "asset_space_width",
    "asset_space_height",
    "asset_space_center_x",
    "asset_space_center_y",
    "asset_camera_uses_owner_time",
):
    assert field in model
    assert field in serialization
    assert field in insertion
assert "asset_isolated_3d_space = false" in model
assert "asset_layer->asset_isolated_3d_space = true" in insertion

# Authored cameras are copied, remapped and explicitly assigned to descendants.
assert "asset_space_owner_id" in title_model
assert '"asset_space_owner_id"' in serialization
assert "camera_id_map" in insertion
assert "camera.asset_space_owner_id" in insertion
assert "clone->camera_assignment = asset_title->active_camera" in insertion
assert "clone->asset_camera_uses_owner_time = true" in insertion
assert "clone->camera_assignment.static_value = remapped_asset_camera" in insertion
assert "key.value = remapped_asset_camera(key.value, key.time)" in insertion

# Two-stage projection removes the Asset Layer transform before the authored
# camera pass, then places that flattened point through the host hierarchy.
assert "isolated_asset_owner" in transform
assert "owner_inverse *" in transform
assert "asset_space_camera" in transform
assert "source_point.x() -" in transform
assert "project_layer_local_point_impl" in transform
assert "result.camera_depth = source_depth" in transform
assert "layer.asset_camera_uses_owner_time" in transform
assert "projected_points.front().camera_position" in transform
assert "!isolated_asset_owner(title, layer)" in transform
assert "OBS' single model-view hardware-depth pass cannot" in transform

# Internal camera snapshots are not user-selectable host cameras.
for ui_source in (title_properties, layer_properties, timeline):
    assert "asset_space_owner_id.empty()" in ui_source

test_name = "tests/development_version_340_isolated_3d_asset_space_contract_test.py"
for area in (
    "editor_gui",
    "serialization_migration",
    "rendering_2d_3d",
    "persistence_cueing",
    "platform_build",
):
    assert test_name in manifest["areas"][area]["python"]

print("Dev340 isolated 3D asset-space contract passed")
