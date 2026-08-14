#!/usr/bin/env python3
"""Contract for the canvas 3D toolbar placement, visibility, and supplied icons."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


commands = read("src/editor/title-editor/commands-docks.inc")
events = read("src/editor/title-editor/editor-events.inc")
layer_icons = read("src/editor/title-editor-internal/text-layout-rendering.inc")

assert 'canvas_layout->insertWidget(0, editor_3d_bar);' in commands
assert "update_3d_toolbar_visibility();" in commands
assert "layer->dimension_mode == LayerDimensionMode::ThreeD" in events

for name in (
    "move.svg",
    "rotate.svg",
    "scale.svg",
    "bounding-box-light.svg",
    "asset-title.svg",
):
    svg = read(f"data/icons/{name}")
    assert "currentColor" in svg, f"{name} must follow the active palette"
    assert name in commands or name in layer_icons, f"{name} is not wired into the editor"

assert 'case LayerType::Asset: return "asset-title.svg";' in layer_icons

print("Flux Editor canvas toolbar/icon contract: PASS")
