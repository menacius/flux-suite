from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def resolved_alpha(exposure_alpha: float, coverage_alpha: float) -> float:
    if coverage_alpha <= 0.0:
        return 0.0
    occupancy = max(0.0, min(1.0, exposure_alpha / coverage_alpha))
    return coverage_alpha * (1.0 - (1.0 - occupancy) ** 4.0)


def test_version_manifest_and_pixel_cache_are_297():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=299' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    assert json.loads(read('tests/test-suite-manifest.json'))['development_version'] == 299
    assert '|gpu-effects-v26-3d-lighting-materials-shadows|' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'v48-3d-lighting-materials' in read(
        'src/cache/cache-manager/visual-hash-keying.inc')


def test_gpu_resolve_uses_coverage_as_ceiling_not_output():
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    block = shader[shader.index('float4 PSTemporalResolve'):
                   shader.index('float4 PSTemporalAccumulate')]
    assert 'exposureAlpha / coverageAlpha' in block
    assert 'float unoccupied2 = unoccupied * unoccupied;' in block
    assert '1.0 - unoccupied2 * unoccupied2' in block
    assert 'float wetAlpha = coverageAlpha * resolvedOccupancy;' in block
    assert 'float4 wet = float4(exposureStraight * wetAlpha, wetAlpha);' in block
    assert 'float4 wet = float4(exposureStraight * coverageAlpha, coverageAlpha);' not in block
    assert 'lerp(currentFrame, wet, mixAmount)' in block


def test_cpu_resolve_matches_gpu_occupancy_contract():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    block = source[source.index('static QImage resolve_motion_blur_coverage'):
                   source.index('static void accumulate_motion_coverage_max')]
    assert 'const double unoccupied2 = unoccupied * unoccupied;' in block
    assert 'exposure_alpha / coverage_alpha' in block
    assert 'coverage_alpha * resolved_occupancy' in block
    assert 'current_alpha * dry + wet_alpha * mix' in block
    assert 'current_alpha * dry + coverage_alpha * mix' not in block


def test_alpha_response_satisfies_all_motion_blur_requirements():
    # No motion/full overlap: authored alpha is exact.
    assert resolved_alpha(1.0, 1.0) == 1.0
    assert abs(resolved_alpha(0.3, 0.3) - 0.3) < 1e-12

    # Dense body overlap remains visually solid.
    assert resolved_alpha(0.5, 1.0) > 0.9
    assert resolved_alpha(0.75, 1.0) > 0.99

    # Sparse trails are visible but never become opaque copies.
    assert 0.0 < resolved_alpha(0.05, 1.0) < 0.25
    assert 0.0 < resolved_alpha(0.10, 1.0) < 0.5

    # Translucent effects preserve their authored ceiling and fade in trails.
    assert 0.0 < resolved_alpha(0.03, 0.3) < 0.3
    assert resolved_alpha(0.3, 0.3) == 0.3


def test_effect_pipeline_sampling_and_performance_contracts_are_untouched():
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    runtime = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
    assert 'Layer temporal_layer = layer_without_motion_blur_effects(layer);' in renderer
    assert 'LayerEffectSpace::LayerSpace' in renderer
    assert 'LayerEffectSpace::ScreenSpace' in renderer
    assert 'motion_blur_quality_sample_count' in renderer
    assert 'gpu_motion_blur_realtime_sample_cap' in renderer
    assert 'gpu_motion_blur_temporal_raster_scale' in renderer
    assert 'return effect_is_time_variant(effect);' in runtime


def test_all_render_paths_share_the_same_resolver():
    gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    compatibility = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    assert gpu.count('resolve_gpu_motion_blur(') >= 3
    assert compatibility.count('resolve_motion_blur_coverage(') >= 3
    assert 'append_gpu_temporal_coverage' in gpu
    assert 'accumulate_motion_coverage_max' in compatibility


def test_documentation_records_the_full_contract():
    changelog = read('docs/CHANGELOG.md')
    readme = read('README.md')
    guide = read('docs/RENDERING_AND_CACHE.md')
    assert changelog.startswith('# v0.8.12-alpha — Development Version 299')
    assert 'Temporal-occupancy Motion Blur alpha resolve' in changelog
    assert 'sparse historical trails fade progressively' in readme
    assert 'exposure alpha / coverage alpha' in guide
