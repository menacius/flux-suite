#!/usr/bin/env python3
"""Standalone canvas artwork must share Editor overlay transform geometry."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Editor/software-title-render-session.cpp").read_text(
    encoding="utf-8"
)

for token in (
    'software_layer_local_rect',
    'layer.size.is_animated()',
    'layer.origin_prop.is_animated()',
    'resolved_layer_time(title, *layer, time)',
    'fxm::transform3d::layer_world_matrix(',
    'fxm::transform3d::projected_local_quad_transform(',
    'fxm::transform3d::layer_passes_backface_culling(',
    'warped_local_quad(layer, bounds, local_time)',
    'painter.setWorldTransform(local_to_canvas)',
):
    assert token in SOURCE, token

# These were the split-time fallback defects: every layer used the Image box
# and bypassed shared parent/camera projection with a hand-built transform.
for obsolete in (
    'const Vec2Value size = layer->image_size.evaluate(time);',
    'painter.translate(position.x, position.y);',
    'painter.rotate(layer->rotation.evaluate(time));',
    'painter.scale(scale.x, scale.y);',
    '-width * layer->image_anchor_x',
):
    assert obsolete not in SOURCE, obsolete

print("Standalone canvas transform contract passed")
