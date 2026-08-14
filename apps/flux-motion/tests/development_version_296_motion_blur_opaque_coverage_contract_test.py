from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_and_cache_identity_are_current():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=299' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    assert json.loads(read('tests/test-suite-manifest.json'))['development_version'] == 298
    assert '|gpu-effects-v26-3d-lighting-materials-shadows|' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'v48-3d-lighting-materials' in read(
        'src/cache/cache-manager/visual-hash-keying.inc')


def test_gpu_resolve_uses_temporal_color_and_temporal_max_alpha():
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    block = shader[shader.index('float4 PSTemporalResolve'):
                   shader.index('float4 PSTemporalAccumulate')]
    assert 'coverageImage.Sample(textureSampler, v.uv).a' in block
    assert 'exposure.rgb / exposureAlpha' in block
    assert 'float occupancy = coverageAlpha > 0.00001' in block
    assert 'float wetAlpha = coverageAlpha * resolvedOccupancy;' in block
    assert 'float4 wet = float4(exposureStraight * wetAlpha, wetAlpha);' in block
    assert 'lerp(currentFrame, wet, mixAmount)' in block
    assert 'lerp(currentFrame, exposure, mixAmount)' not in block
    assert 'sharp + trail' not in block
    assert 'technique Coverage' in shader


def test_all_gpu_motion_paths_accumulate_max_coverage():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    resources = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    lifecycle = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    assert 'motion_coverage_target' in resources
    assert '!create_target(session->motion_coverage_target)' in resources
    assert 'destroy_target(session->motion_coverage_target);' in lifecycle
    assert 'append_gpu_temporal_coverage' in source
    assert 'gs_blend_op(GS_BLEND_OP_MAX);' in source
    assert source.count('resolve_gpu_motion_blur(') >= 3
    assert 'coverage_texture' in source
    # Full strength goes through the coverage-aware resolve, not a raw exposure copy.
    dynamic = source[source.index('if (accumulation_ok && accumulated_samples > 0)'):
                     source.index('release_temporal_entry();')]
    assert 'resolve_gpu_motion_blur' in dynamic
    assert 'copy_full_canvas_gpu_texture' not in dynamic


def test_cpu_and_gpu_readback_paths_use_the_same_contract():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    block = source[source.index('static QImage resolve_motion_blur_coverage'):
                   source.index('static void accumulate_motion_coverage_max')]
    assert 'coverage_image' in block
    assert 'coverage_alpha' in block
    assert '(exposure_channel / 255.0) / exposure_alpha' in block
    assert 'current_alpha * dry + wet_alpha * mix' in block
    assert 'exposure_alpha / coverage_alpha' in block
    assert 'coverage_alpha * resolved_occupancy' in block
    assert 'accumulate_motion_coverage_max(coverage_canvas, sample_canvas);' in source
    assert 'GS_BLEND_OP_MAX' in source
    assert 'g_temporal_gpu.coverage_target' in source


def test_reference_math_preserves_body_and_fades_trails_without_sharp_color():
    def resolve_alpha(exposure_alpha, coverage_alpha):
        occupancy = exposure_alpha / coverage_alpha if coverage_alpha else 0.0
        occupancy = max(0.0, min(1.0, occupancy))
        return coverage_alpha * (1.0 - (1.0 - occupancy) ** 4.0)

    # Full temporal overlap preserves authored opacity.
    assert resolve_alpha(1.0, 1.0) == 1.0
    assert abs(resolve_alpha(0.3, 0.3) - 0.3) < 1e-12

    # Sparse historical positions remain translucent instead of inheriting the
    # full max-alpha envelope from a single shutter sample.
    opaque_trail = resolve_alpha(0.1, 1.0)
    shadow_trail = resolve_alpha(0.03, 0.3)
    assert 0.0 < opaque_trail < 1.0
    assert 0.0 < shadow_trail < 0.3

    # Dense overlap becomes nearly solid without adding current-frame RGB.
    assert resolve_alpha(0.5, 1.0) > 0.9

def test_unrelated_motion_blur_contracts_remain_present():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'Layer temporal_layer = layer_without_motion_blur_effects(layer);' in source
    assert 'LayerEffectSpace::LayerSpace' in source
    assert 'LayerEffectSpace::ScreenSpace' in source
    assert 'motion_blur_quality_sample_count' in source
    assert 'gpu_motion_blur_realtime_sample_cap' in source
    changelog = read('docs/CHANGELOG.md')
    assert changelog.startswith('# v0.8.12-alpha — Development Version 299')
    assert 'Leaves effect ordering, Noise/Grain, Trim Paths, transitions' in changelog
