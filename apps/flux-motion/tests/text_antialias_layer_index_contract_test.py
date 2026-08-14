#!/usr/bin/env python3
"""Text fill antialiasing and layer-index foreground regression contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
stack = read("src/layers/layer-stack-widget.cpp")

# Static Text, Clock and Ticker layers must use the same exact QPainter raster
# that is selected by a static stroke. The SDF path is retained only for
# per-glyph animation, where a flattened compatibility raster is incorrect.
assert "Static Text, Clock and Ticker layers use the exact Qt compatibility raster" in gpu
assert "if (!text_animator_stack_has_enabled_animators(layer.text_animators))" in gpu
policy = gpu.split("static bool layer_can_use_gpu_text_raster", 1)[1].split(
    "static bool prepare_gpu_text_raster", 1
)[0]
assert policy.index(
    "if (!text_animator_stack_has_enabled_animators(layer.text_animators))"
) < policy.index("return true;")

# The layer number must participate in the same background-aware foreground
# refresh as the row's labels and tintable icons.
assert 'idx->setObjectName(QStringLiteral("layerRowIndex"))' in stack
assert "idx->setProperty(kLayerRowForegroundLabelProperty, true)" in stack
assert "widget->property(kLayerRowForegroundLabelProperty).toBool()" in stack
assert '"QLabel{color:%1;background:transparent;}"' in stack

print("text antialias/layer index contract: PASS")
