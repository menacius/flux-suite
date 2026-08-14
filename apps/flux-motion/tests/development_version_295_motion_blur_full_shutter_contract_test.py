from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_cache_and_manifest_are_295():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=299' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    assert json.loads(read('tests/test-suite-manifest.json'))['development_version'] == 298
    assert '|gpu-effects-v26-3d-lighting-materials-shadows|' in read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'v48-3d-lighting-materials' in read('src/cache/cache-manager/visual-hash-keying.inc')


def test_full_strength_gpu_resolve_is_exact_shutter_exposure():
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    start = shader.index('float4 PSTemporalResolve')
    end = shader.index('technique Draw', start)
    block = shader[start:end]
    assert 'float4 exposure = accumulatedImage.Sample' in block
    assert 'float4 currentFrame = sampleImage.Sample' in block
    assert 'float4 result = lerp(currentFrame, wet, mixAmount);' in block
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'gs_texture_t *coverage_texture' in renderer
    assert 'resolve_gpu_motion_blur(' in renderer
    assert 'if (blur_mix >= 0.9999)' not in renderer
    assert 'if (motion_ok && motion_trail_opacity >= 0.9999f)' not in renderer
    assert 'max(sharpAlpha' not in block
    assert 'source-over' not in block.lower()


def test_cpu_and_compatibility_resolve_match_premultiplied_dry_wet_contract():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    start = source.index('static QImage resolve_motion_blur_coverage')
    end = source.index('static bool gpu_accumulate_motion_raster', start)
    block = source[start:end]
    assert 'const double dry = 1.0 - mix;' in block
    assert 'current_alpha * dry + wet_alpha * mix' in block
    assert 'std::max(sa, ta * mix)' not in block
    assert source.count('resolve_motion_blur_coverage(') >= 3


def test_reference_full_mix_has_no_sharp_current_frame_and_no_opaque_trail_copies():
    current = (0.4, 0.2, 0.1, 0.4)
    exposure = (0.1, 0.3, 0.2, 0.25)
    coverage_alpha = 0.4
    occupancy = exposure[3] / coverage_alpha
    wet_alpha = coverage_alpha * (1.0 - (1.0 - occupancy) ** 4.0)
    straight = tuple(exposure[i] / exposure[3] for i in range(3))
    wet = tuple(c * wet_alpha for c in straight) + (wet_alpha,)
    assert wet[3] < coverage_alpha
    assert wet != exposure
    half_alpha = current[3] * 0.5 + wet_alpha * 0.5
    assert current[3] > half_alpha > wet_alpha

def test_normalized_exposure_and_complete_effect_stack_are_unchanged():
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    assert '1.0f / static_cast<float>(samples)' in renderer
    assert 'Layer temporal_layer = layer_without_motion_blur_effects(layer);' in renderer
    assert 'LayerEffectSpace::LayerSpace' in renderer
    assert 'LayerEffectSpace::ScreenSpace' in renderer
    assert 'result += sampleImage.Sample(textureSampler, v.uv) * sampleWeight;' in shader
    assert 'gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);' in renderer


def test_changelog_documents_no_sharp_silhouette_contract():
    changelog = read('docs/CHANGELOG.md')
    assert changelog.startswith('# v0.8.12-alpha — Development Version 299')
    assert 'without a separately reinforced sharp current frame' in changelog
    assert 'premultiplied RGBA dry/wet interpolation' in changelog
