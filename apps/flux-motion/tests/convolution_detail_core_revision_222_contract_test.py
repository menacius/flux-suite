#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def text(p): return (root/p).read_text(encoding="utf-8")
build=text("src/core/build-info.h")
enum=text("src/effects/layer-effects.h")
runtime=text("src/effects/effect-runtime.cpp")
panel=text("src/effects/effects-panel.cpp")
loader=text("src/core/title-data.cpp")
shader=text("data/effect-transitions/shaders/detail/detail.effect")
noise=text("data/effect-transitions/shaders/noise/noise.effect")
registry=text("src/rendering/title-effect-registry.cpp")
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
for name in ("Sharpen","UnsharpMask","HighPass","Clarity","BilateralSharpen"):
    assert f"LayerEffectType::{name}" in runtime
    assert name in enum
for token in ("blurredImage","protectAlpha","highlightProtection","midtoneBias","technique BilateralSharpen"):
    assert token in shader
assert "NoiseEngineLegacy" not in panel
assert "noiseVersion" not in noise
assert "legacy_profile" not in noise
assert "kEmbeddedLegacyNoiseFallbackEffect" not in registry
assert "loaded_builtin_effect && descriptor" in loader
assert "effect.extension_schema_version < descriptor->schema_version" in loader
assert "make_default_layer_effect(effect.type)" in loader
assert 'schema 3' in text("docs/EFFECTS_AND_EXTENSIONS.md")
print("Development Version 222 convolution/detail and schema-reset contract passed")
