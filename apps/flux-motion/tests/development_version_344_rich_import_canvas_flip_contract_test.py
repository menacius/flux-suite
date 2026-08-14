#!/usr/bin/env python3
"""Dev344: rich SVG/PSD import and canvas-space flip contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
importer = (ROOT / "src/editor/title-editor/import-documents.inc").read_text(encoding="utf-8")
editing = (ROOT / "src/editor/title-editor/document-shape-editing.inc").read_text(encoding="utf-8")
manifest = json.loads((ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 344
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 344
assert manifest["development_version"] >= 344

for token in [
    "svg_css_rules", "svg_selector_matches", "currentColor", "rgba(", "hsla(",
    "font-family", "font-size", "font-weight", "letter-spacing", "text-anchor",
    "native_text", "stroke_gradient_stops", "gradientTransform", "spreadMethod",
    "gradient_center_x", "gradient_focal_x", "shapes.size() > 1",
]:
    assert token in importer, token

for token in [
    'key=="lfx2"||key=="lmfx"', 'key=="lrFX"', "psd_descriptor",
    "psd_layer_effects", "psd_legacy_layer_effects", "LayerEffectType::DropShadow",
    "LayerEffectType::InnerShadow", "LayerEffectType::Glow",
    "LayerEffectType::InnerGlow", "LayerEffectType::ColorOverlay",
    "LayerEffectType::Outline", "LayerEffectType::Emboss", "layer->effects=source.effects",
    'mode == "sftl"', 'mode == "hrdl"', 'mode == "lmns"',
]:
    assert token in importer, token

for token in [
    "editor_layer_world_transform_for_parenting", "editor_parent_world_transform_for_parenting",
    "const QTransform reflection", "world * reflection", "parent_inverse",
    "std::atan2(local.m12(), local.m11())", "editor_set_local_position_xy_for_parenting",
]:
    assert token in editing, token

print("Dev344 rich import and canvas-space flip contract passed")
