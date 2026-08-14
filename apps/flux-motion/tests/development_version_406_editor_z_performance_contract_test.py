#!/usr/bin/env python3
"""Development 406: Z edits stay transform-only and software preview stays bounded."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TITLE_DATA = (ROOT / "src/core/title-data.cpp").read_text(encoding="utf-8")
SOFTWARE = (ROOT / "Editor/software-title-render-session.cpp").read_text(
    encoding="utf-8"
)
TRANSFORM = (ROOT / "src/rendering/layer-transform-3d.cpp").read_text(
    encoding="utf-8"
)
ASSET_RUNTIME = (ROOT / "Core/asset-runtime.cpp").read_text(encoding="utf-8")

fingerprint = TITLE_DATA.split("std::string layer_render_fingerprint", 1)[1]
fingerprint = fingerprint.split("static void migrate_and_validate_extension_state", 1)[0]
for key in (
    "transform_quad_tl",
    "transform_quad_br",
    "dimension_mode",
    "position_z",
    "position_3d_path_enabled",
    "position_3d",
    "rotation_x",
    "rotation_y",
    "scale_z",
    "anchor_z",
    "orientation_x",
    "orientation_y",
    "orientation_z",
    "camera_assignment",
    "depth_mode",
    "depth_test",
    "write_to_depth",
):
    assert f'"{key}"' in fingerprint, key

for token in (
    "SoftwareLayerRasterCache",
    "cached_software_layer_raster(",
    "layer_render_fingerprint(layer)",
    "layer_has_raster_animation(layer)",
    "transform_only_update && existing != cache->end()",
    "existing->second.source_bounds == bounds",
    "software_title_changes_with_time(title)",
    "same_model && same_quality",
    "kLightingCellPixels = 12",
    "bilinearly reconstruct",
):
    assert token in SOFTWARE, token

assert "bool layer_has_raster_animation(const Layer &layer)" in ASSET_RUNTIME
assert ("depth_sort_passthrough" in TRANSFORM or
        "std::map<std::string, CameraDepthSet> by_camera" in TRANSFORM)
assert ("candidate_slots" in TRANSFORM or
        "set.positions.push_back(slot)" in TRANSFORM)

print("Development Version 406 editor Z/performance contract: PASS")
