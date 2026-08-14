#!/usr/bin/env python3
"""Parent binds must be applied once, after local raster generation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc").read_text(
    encoding="utf-8"
)

start = source.index("static void neutralize_layer_transform_for_effect_cache")
end = source.index("static int gaussian_blur_downsample", start)
neutralize = source[start:end]

assert "layer.parent_id.clear();" in neutralize
assert "layer.transform_parent_id.clear();" in neutralize
assert "layer.parent_bind_enabled = false;" in neutralize
assert "layer.parent_bind_matrix = {" in neutralize

print("Parent-bind local raster contract: PASS")
