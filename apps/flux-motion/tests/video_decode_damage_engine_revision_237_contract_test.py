from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
schema = read('Core/title-serialization-schema.h')
manifest = read('tests/test-suite-manifest.json')
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 237:' in schema
assert '"development_version": 243' in manifest
assert 'Development Version 243' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

video = read('Core/title-video-runtime.cpp')
assert 'requested_media_frame_rate' in video
assert 'prefetch_count = 4' in video
assert 'look-ahead cache keeps normal forward playback' in video
assert 'media-requested:' in video
assert 'frame_for_layer(layer, title_time, project_frame_rate)' not in video.split('std::string FrameRuntime::frame_cache_key_for_layer', 1)[1].split('void FrameRuntime::forget_layer', 1)[0]

surface = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
registry = read('src/rendering/title-effect-registry.cpp')
shader = read('data/effect-transitions/shaders/damage-distortion/damage-distortion.effect')
runtime = read('src/effects/effect-runtime.cpp')

for token in ['damageProfile', 'composite_film', 'composite_analog', 'composite_digital', 'PSDamage', 'technique Draw']:
    assert token in shader, token
    assert token in registry, token

for removed in ['DamageMap', 'DamageComposite', 'damageMap', 'damage_distortion_pass', 'PSDamageSingle']:
    assert removed not in shader, removed
    assert removed not in registry, removed

assert 'effect_is_damage_distortion' in surface
assert 'effect_uses_damage_artifact_pipeline' in surface
assert 'gpu_effect_primary_technique' in surface
assert 'if (effect_uses_damage_artifact_pipeline(resolved.type))' in surface
assert 'return "Draw";' in surface
assert 'const char *technique = gpu_effect_primary_technique(resolved, false);' in surface
assert 'const char *technique = gpu_effect_primary_technique(resolved, blurred != nullptr);' in gpu
assert 'gs_effect_loop(gpu_effect, technique)' in surface
assert 'gs_effect_loop(pass_effect, technique)' in gpu
assert 'gpu-effects-v21-organic-damage-motion239' in gpu
assert 'gpu-effects-v19-damage-multipass-video237' not in gpu
assert 'kDamageParameters' in runtime
assert 'kNoiseParameters, countof(kNoiseParameters)' not in '\n'.join(
    line for line in runtime.splitlines() if 'FilmDistortion' in line or 'AnalogDistortion' in line or 'DigitalDistortion' in line
)

for preset in ['Film Distortion.fxmeffect', 'Analog Distortion.fxmeffect', 'Digital Distortion.fxmeffect']:
    text = read(f'data/effect-transitions/{preset}')
    assert '"secondaryColor"' in text
    assert '"amount": 0.' in text

print('Development Version 243 video decode and damage unified effect engine contract: PASS')
