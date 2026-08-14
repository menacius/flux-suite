#!/usr/bin/env python3
"""Development Version 243 effect-pipeline audit contract."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
schema = read('Core/title-serialization-schema.h')
manifest = json.loads(read('tests/test-suite-manifest.json'))
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 238:' in schema
assert manifest['development_version'] == 243
assert 'Development Version 243' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

surface = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
registry = read('src/rendering/title-effect-registry.cpp')
shader = read('data/effect-transitions/shaders/damage-distortion/damage-distortion.effect')
runtime = read('src/effects/effect-runtime.cpp')
blur_backend = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')

# New damage effects must go through one shared effect technique selector, not
# a private compatibility-only branch that can be skipped or alias targets.
for token in (
    'static const char *gpu_effect_primary_technique',
    'effect_uses_damage_artifact_pipeline',
    'return "Draw";',
    'gs_effect_loop(gpu_effect, technique)',
):
    assert token in surface, token
assert 'const char *technique = gpu_effect_primary_technique(resolved, blurred != nullptr);' in gpu
assert 'gs_effect_loop(pass_effect, technique)' in gpu
assert 'gpu-effects-v21-organic-damage-motion239' in gpu

for removed in (
    'DamageMap', 'DamageComposite', 'damageMap', 'damage_distortion_pass',
    'gpu-effects-v19-damage-multipass-video237', 'PSDamageSingle',
):
    assert removed not in shader, removed
    assert removed not in registry, removed
    if removed != 'PSDamageSingle':
        assert removed not in surface, removed
        assert removed not in gpu, removed

# Damage shader/fallback must contain distinct Film, Analog and Digital artifact logic.
for token in (
    'composite_film', 'film_scratches', 'film_dust',
    'composite_analog', 'analog_dropout', 'scan', 'chroma',
    'composite_digital', 'macroblock', 'packet', 'quant',
    'damageProfile', 'technique Draw', 'pixel_shader = PSDamage',
):
    assert token in shader, token
    assert token in registry, token

# Runtime descriptors use dedicated damage parameter metadata, not Noise metadata.
assert 'constexpr EffectParameterDescriptor kDamageParameters[]' in runtime
for effect in ('FilmDistortion', 'AnalogDistortion', 'DigitalDistortion'):
    descriptor_line = next(line for line in runtime.splitlines() if f'LayerEffectType::{effect}' in line)
    assert 'kDamageParameters, countof(kDamageParameters)' in descriptor_line
    assert 'kNoiseParameters' not in descriptor_line

# Blur effects must keep the existing fast separable Gaussian backend.
for token in (
    'effect_uses_separable_gaussian',
    'render_separable_gaussian',
    'compile(LayerEffectType::Blur)',
    'gs_effect_loop(blur, "Downsample")',
    'gs_effect_loop(blur, "Gaussian")',
):
    assert token in blur_backend, token
for blur_type in ('Blur', 'Glow', 'DropShadow', 'Bloom', 'Halation', 'Glare'):
    assert f'LayerEffectType::{blur_type}' in blur_backend
assert 'has_gaussian_lowpass' in surface

print('Development Version 243 effect-pipeline audit contract: PASS')
