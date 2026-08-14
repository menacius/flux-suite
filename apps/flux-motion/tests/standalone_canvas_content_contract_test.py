#!/usr/bin/env python3
"""Standalone canvas must consume shared shape, rich-text and 3D light data."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Editor/software-title-render-session.cpp").read_text(
    encoding="utf-8"
)

for token in (
    '#include "path-geometry.h"',
    'fxm::layer_shape_path(layer, bounds)',
    'rich_text_document_canonical_copy(layer)',
    'rich_text_document_with_evaluated_defaults_canonical(',
    'rich_text_document_with_auto_styles_canonical(',
    'cached_text_layout(request)',
    'text_layout_cluster_paint_slices(',
    'raw.pathForGlyph(glyph.glyph_id)',
    'software_lighting_factor(',
    'layer.material_accepts_lights',
    'candidate.casts_shadows',
    'draw_software_3d_shadow(',
):
    assert token in SOURCE, token

# The old split-time fallback flattened both geometry and styled text.
assert 'painter.drawRect(bounds);' in SOURCE  # retained only for true boxes
assert 'painter.drawText(bounds' in SOURCE  # retained only as glyph fallback
assert SOURCE.index('if (!draw_rich_text(') < SOURCE.index('painter.drawText(bounds')

print("Standalone canvas content contract passed")
