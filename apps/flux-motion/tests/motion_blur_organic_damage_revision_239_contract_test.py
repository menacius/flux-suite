#!/usr/bin/env python3
"""Development Version 243 motion blur and organic damage shader contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = ROOT.parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
schema = read('Core/title-serialization-schema.h')
manifest = json.loads(read('tests/test-suite-manifest.json'))
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')

assert re.search(r'OBS_FXM_DEVELOPMENT_VERSION \"(239|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})\"', cmake)
assert re.search(r'FXM_DEVELOPMENT_VERSION \"(239|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})\"', build)
assert re.search(r'kCurrentDevelopmentVersion = (239|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})', schema) and 'case 239:' in schema
assert manifest['development_version'] >= 239
assert re.search(r'Development Version (239|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})', readme)
assert re.search(r'# v0.8.11-alpha — Development Version (239|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})', changelog)

cpu_motion = (REPOSITORY_ROOT / 'packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc').read_text(encoding='utf-8')
sampling = (REPOSITORY_ROOT / 'packages/flux-common/Shared' /
            'motion-blur-sampling.cpp').read_text(encoding='utf-8')
gpu_motion = (REPOSITORY_ROOT / 'packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc').read_text(encoding='utf-8')
shader = read('data/effect-transitions/shaders/damage-distortion/damage-distortion.effect')
embedded = read('src/rendering/title-effect-registry.cpp')
runtime = read('src/effects/effect-runtime.cpp')

# Image/video motion blur must not use the old low 12/24 sample ceiling that
# made bitmap and video exposures look like posterized ghost frames.
assert 'imageLike ? 1.5 : 0.8' in sampling
assert 'imageLike ? 96 : 48' in sampling
assert '? (image_or_video_motion ? 96 : 48)' in gpu_motion
assert ': (image_or_video_motion ? 64 : 40);' in gpu_motion
assert '? (image_or_video_motion ? 20 : 16)' in gpu_motion
assert ': (image_or_video_motion ? 16 : 12));' in gpu_motion
assert 'gpu-effects-v26-3d-lighting-materials-shadows' in gpu_motion
assert 'gpu-effects-v20-unified-effect-technique-video238' not in gpu_motion

# The damage shader should be layered and organic, not a single noise overlay.
for token in [
    'film_vertical_scratch', 'film_hair_fiber', 'film_dust_blob', 'film_blotch', 'emulsion_grain',
    'analog_yiq', 'analog_from_yiq', 'analog_dropout', 'tracking_band', 'head_switch', 'interlace',
    'macroblock compression artifact selector', 'digital_ringing', 'packet_jump', 'quant_levels', 'block_local',
]:
    assert token in shader, token
    assert token in embedded, token

for token in ['PSDamage', 'composite_film', 'composite_analog', 'composite_digital', 'technique Draw']:
    assert token in shader and token in embedded, token

# Damage controls used by the organic shader are part of animatable metadata.
for token in ['"effect_roundness"', '"effect_center"', '"effect_secondary_color"']:
    assert token in runtime, token

# Blur effects must still use the existing fast Gaussian backend.
blur_backend = (REPOSITORY_ROOT / 'packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc').read_text(encoding='utf-8')
assert 'render_separable_gaussian' in blur_backend
assert 'gs_effect_loop(blur, "Gaussian")' in blur_backend

print('Development Version 243 motion blur and organic damage contract: PASS')
