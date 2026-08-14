#!/usr/bin/env python3
"""Development Version 224 end-to-end built-in effects pipeline contract."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
enum = read("src/effects/layer-effects.h")
runtime = read("src/effects/effect-runtime.cpp")
defaults = read("src/effects/effect-preset-catalog.cpp")
panel = read("src/effects/effects-panel.cpp")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
gaussian = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
compatibility = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
registry = read("src/rendering/title-effect-registry.cpp")
loader = read("src/core/title-data.cpp")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 224:' in schema and 'case 225:' in schema
assert manifest["development_version"] == 243

# The repaired 222/223 effects must use the actual auxiliary Gaussian output.
for effect in ("Sharpen", "UnsharpMask", "HighPass", "Clarity", "BilateralSharpen", "Glare", "Halation"):
    assert f"case LayerEffectType::{effect}:" in gaussian
assert 'fxm_effect_param(pass_effect, "blurredImage")' in gpu
assert 'static bool is_modern_gpu_pixel_effect_type' in compatibility
for effect in ("Sharpen", "UnsharpMask", "HighPass", "Clarity", "BilateralSharpen"):
    assert f"case LayerEffectType::{effect}:" in compatibility
assert 'is_modern_gpu_pixel_effect_type(effect.type)' in compatibility
for technique in ("Sharpen", "UnsharpMask", "HighPass", "Clarity", "BilateralSharpen", "GlareComposite", "HalationComposite"):
    assert f'technique = "{technique}"' in gpu
assert 'Effect technique executed no passes' in gpu

# Glare is source-driven and no longer reuses Lens Flare code/UI.
assert 'shaders/glare/glare.effect' in runtime
assert 'shaders/lens-flare/lens-flare.effect", true, 1, LayerEffectSpace::ScreenSpace' not in runtime
assert '(selected_effect()->type == LayerEffectType::LensFlare || selected_effect()->type == LayerEffectType::Glare)' not in panel
assert 'selected_effect()->type == LayerEffectType::Glare' in panel
assert 'Streak Length' in panel and 'Chromatic Dispersion' in panel
assert 'kEmbeddedGlareEffect' in registry

# Halation is a dedicated spectral composite, not an alias of Bloom.
assert 'shaders/halation/halation.effect' in runtime
assert 'selected_effect()->type == LayerEffectType::Halation' in panel
assert 'Outer Spectral Color' in panel and 'Diffusion' in panel
assert 'kEmbeddedHalationEffect' in registry

# 224 built-ins are append-only, discoverable, editable and mapped to techniques.
new_effects = {
    "LensDistortion": "Lens Distortion.fxmeffect",
    "ChromaticAberration": "Chromatic Aberration.fxmeffect",
    "DirectionalBlur": "Directional Blur.fxmeffect",
    "ZoomBlur": "Zoom Blur.fxmeffect",
    "RadialBlur": "Radial Blur.fxmeffect",
    "Ripple": "Ripple.fxmeffect",
    "WaveWarp": "Wave Warp.fxmeffect",
    "Pixelate": "Pixelate.fxmeffect",
    "EdgeDetect": "Edge Detect.fxmeffect",
    "Posterize": "Posterize.fxmeffect",
    "Threshold": "Threshold.fxmeffect",
    "Scanlines": "Scanlines.fxmeffect",
}
finishing = read("data/effect-transitions/shaders/finishing/finishing.effect")
for effect, preset in new_effects.items():
    assert f"{effect} =" in enum
    assert f"LayerEffectType::{effect}" in runtime
    assert f'case LayerEffectType::{effect}: technique = "{effect}"' in gpu
    assert f"technique {effect}" in finishing
    assert (ROOT / "data/effect-transitions" / preset).is_file(), preset
assert 'selected_effect()->type >= LayerEffectType::LensDistortion' in panel
assert 'selected_effect()->type <= LayerEffectType::Scanlines' in panel
assert 'kEmbeddedFinishingEffect' in registry
assert 'effect.type == LayerEffectType::Ripple ||' in runtime
assert 'effect.type == LayerEffectType::WaveWarp' in runtime

# Changed built-ins have new schemas and old instances reset to current defaults.
for stable_id in ("fxm.builtin.sharpen", "fxm.builtin.glare", "fxm.builtin.halation"):
    line = next(line for line in runtime.splitlines() if stable_id in line)
    assert ', 2, LayerEffectSpace::LayerSpace' in line
for stable_id in ("fxm.builtin.sharpen", "fxm.builtin.unsharp-mask", "fxm.builtin.high-pass", "fxm.builtin.clarity", "fxm.builtin.bilateral-sharpen"):
    line = next(line for line in runtime.splitlines() if stable_id in line)
    assert 'EffectAlphaContract::PremultipliedPreserve, 3, true' in line
assert 'effect.extension_schema_version < descriptor->schema_version' in loader
assert 'make_default_layer_effect(effect.type)' in loader
assert 'case LayerEffectType::Glare:' in defaults

print("Development Version 224 functional effects pipeline contract passed")
