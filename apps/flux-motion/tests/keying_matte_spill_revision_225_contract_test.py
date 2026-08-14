#!/usr/bin/env python3
"""Development Version 225 keying, matte and spill end-to-end contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
read = lambda rel: (ROOT / rel).read_text(encoding="utf-8")

enum = read("src/effects/layer-effects.h")
runtime = read("src/effects/effect-runtime.cpp")
catalog = read("src/effects/effect-preset-catalog.cpp")
panel = read("src/effects/effects-panel.cpp")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
compat = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache = read("src/cache/cache-manager/live-cue-state.inc")
loader = read("src/core/title-data.cpp")
registry = read("src/rendering/title-effect-registry.h")
schema = read("Core/title-serialization-schema.h")
shader = read("data/effect-transitions/shaders/keying/keying.effect")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 225:' in schema
assert manifest["development_version"] == 243

ids = {
    "ChromaKey": 38,
    "LumaKey": 39,
    "ColorRange": 40,
    "SpillSuppression": 41,
    "MatteChoker": 42,
}
for name, value in ids.items():
    assert re.search(rf"\b{name}\s*=\s*{value}\b", enum), name
    assert f"LayerEffectType::{name}" in runtime
    assert f"LayerEffectType::{name}" in catalog
    assert f"LayerEffectType::{name}" in panel
    assert f"LayerEffectType::{name}" in hierarchy
    assert f"LayerEffectType::{name}" in compat
    assert f"LayerEffectType::{name}" in gpu
    assert f"LayerEffectType::{name}" in cache

stable = {
    "ChromaKey": "fxm.builtin.chroma-key",
    "LumaKey": "fxm.builtin.luma-key",
    "ColorRange": "fxm.builtin.color-range",
    "SpillSuppression": "fxm.builtin.spill-suppression",
    "MatteChoker": "fxm.builtin.matte-choker",
}
for name, stable_id in stable.items():
    assert stable_id in runtime
    assert 'effect-transitions/shaders/keying/keying.effect' in runtime

assert 'static_cast<std::size_t>(LayerEffectType::DigitalDistortion) + 1' in registry
assert 'raw_effect_type <= (int)LayerEffectType::DigitalDistortion' in loader
assert 'effect.type == LayerEffectType::MatteChoker' in loader
assert 'effect_amount_min = -1.0' in loader and 'effect_amount_max = 1.0' in loader
assert 'std::clamp(evaluated_amount, -1.0, 1.0)' in runtime
assert 'validate_recover_title_shape(title, report);' in schema[schema.index('case 225:'):schema.index('default:', schema.index('case 225:'))]
assert 'gpu-effects-v17-keying-matte' in gpu

for technique, pixel in {
    "ChromaKey": "PSChromaKey",
    "LumaKey": "PSLumaKey",
    "ColorRange": "PSColorRange",
    "SpillSuppression": "PSSpillSuppression",
    "MatteChoker": "PSMatteChoker",
}.items():
    assert f"float4 {pixel}(VertDataOut v_in) : TARGET" in shader
    assert re.search(
        rf"technique\s+{technique}\s*\{{.*pixel_shader\s*=\s*{pixel}\(v_in\)",
        shader,
        re.S,
    )

assert 'key_color_distance' in shader
assert 'neutralize_key_spill' in shader
assert 'min_alpha = min' in shader and 'max_alpha = max' in shader
assert 'float output_alpha' in shader
assert '.xxx' not in shader
assert 'legacy' not in shader.lower()

for name in ("Chroma Key", "Luma Key", "Color Range", "Spill Suppression", "Matte Choker"):
    data = json.loads(read(f"data/effect-transitions/{name}.fxmeffect"))
    assert data["kind"] == "effect"
    assert data["category"] == "Effects/Keying & Matte"

rendering_tests = manifest["areas"]["rendering_2d_3d"]["python"]
assert "tests/keying_matte_spill_revision_225_contract_test.py" in rendering_tests

print("Development Version 225 keying/matte/spill contract passed")
