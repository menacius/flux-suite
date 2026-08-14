#!/usr/bin/env python3
"""Dev339: restore legacy Asset Library storage, retain independent playback."""

from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
model = read("Core/title-data.h")
data = read("src/core/title-data.cpp")
editor = read("src/editor/title-editor/playback-cache-preferences.inc")
asset_runtime_h = read("Core/asset-runtime.h")
asset_runtime = read("Core/asset-runtime.cpp")
editor_timer = read("src/editor/title-editor/window-session.inc")
editor_helpers = read("src/editor/title-editor-internal/canvas-rendering-helpers.inc")
canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")
source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
gpu_lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
cache_policy = read("src/cache/title-cache-policy.h")
cache_invalidation = read("src/cache/cache-manager/cache-policy-invalidation.inc")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 339
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 339
assert manifest["development_version"] >= 339

# Packed title/template import-export remains available, but reusable Asset
# Library entries return to the pre-338 in-store persistence path.
assert "export_packed_title" in model
assert "export_packed_title" in data
for removed in (
    "save_packed_asset",
    "packed_asset_package_path",
    'obs_module_config_path("packed-assets")',
):
    assert removed not in model
    assert removed not in data
    assert removed not in editor

save_asset = editor[editor.index("void TitleEditor::save_title_as_asset()") :
                    editor.index("void TitleEditor::export_title_template")]
assert "stored->is_asset = true" in save_asset
assert "notify_change()" in save_asset
assert "save_async()" in save_asset

# The independent-playback repair from 338 remains intact.
for text in (asset_runtime_h, asset_runtime):
    assert "title_has_independent_playback" in text
    assert "layer_uses_independent_playback" in text
assert "layer.type == LayerType::Video" in asset_runtime
assert "layer.type == LayerType::Audio" in asset_runtime
assert "title_has_independent_playback(*title)" in source_runtime
assert "title_has_independent_playback(*title_)" in canvas
assert "title_has_independent_playback(*title)" in editor_helpers
assert "editor_playback_ui_timer_interval_ms()" in editor_timer
assert "independent_asset_clock" in gpu_lifecycle
assert "raster_changed || independent_asset_clock" in gpu_lifecycle
assert "layer_uses_independent_playback" in cache_policy
assert "layer_uses_independent_playback" in cache_invalidation

test_name = "tests/development_version_339_legacy_asset_storage_independent_playback_contract_test.py"
for area in (
    "editor_gui",
    "serialization_migration",
    "rendering_2d_3d",
    "persistence_cueing",
    "platform_build",
):
    assert test_name in manifest["areas"][area]["python"]

print("Dev339 legacy asset storage and independent playback contract passed")
