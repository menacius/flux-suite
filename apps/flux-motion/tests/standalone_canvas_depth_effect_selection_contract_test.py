#!/usr/bin/env python3
"""Standalone canvas keeps depth, shading, effects and text selection active."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = (ROOT / "Editor/software-title-render-session.cpp").read_text(
    encoding="utf-8"
)
CANVAS = (
    ROOT / "src/canvas/canvas-preview/keyboard-wheel-events.inc"
).read_text(encoding="utf-8")

for token in (
    "fxm::transform3d::ordered_root_layer_indices(",
    "fxm::transform3d::ordered_group_children(",
    "sample_software_lighting(",
    "lighting.specular",
    "shade_software_image(",
    "resolve_layer_effect(",
    "effect_bounds_expansion(",
    "apply_software_effect(",
    "software_blend_mode(layer->blend_mode)",
):
    assert token in RENDERER, token

for token in (
    "inline_text_selection_view_polygons()",
    "QPainter::CompositionMode_Difference",
    "The standalone canvas has no swap-chain compositor",
):
    assert token in CANVAS, token

assert "for (auto it = title.layers.rbegin();" not in RENDERER

print("Standalone depth/effect/text-selection contract passed")
