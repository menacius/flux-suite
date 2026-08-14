from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

def test_versions_and_cache_revision():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=299' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')

def test_gpu_resolve_preserves_premultiplied_alpha_without_sharp_union():
    s = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    start = s.index('float4 PSTemporalResolve')
    end = s.index('technique Draw', start)
    block = s[start:end]
    assert 'float4 result = lerp(currentFrame, wet, mixAmount);' in block
    assert 'max(sharpAlpha' not in block
    assert 'float4 over = sharp + trail' not in block
    assert 'result += exposure' not in block
    assert 'coverageAlpha' in block

def test_fast_path_uses_shared_resolve_not_direct_add_then_over():
    s = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    start = s.index('if (use_transform_motion_fast_path')
    end = s.index('struct vec4 clear;', start)
    block = s[start:end]
    assert 'motion_accum_target' in block
    assert 'motion_sample_target' in block
    assert 'resolve_gpu_motion_blur(' in block
    assert 'motion_coverage_target' in block
    assert 'gs_blend_op(GS_BLEND_OP_MAX);' in block

def test_readback_and_cairo_fallback_use_coverage_resolver():
    s = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    assert 'static QImage resolve_motion_blur_coverage' in s
    assert s.count('resolve_motion_blur_coverage(') >= 3
    assert 'current_alpha * dry + wet_alpha * mix' in s
    assert 'accumulate_motion_coverage_max(coverage_canvas, sample_canvas);' in s

def test_changelog_records_future_safe_contract():
    s = read('docs/CHANGELOG.md')
    assert s.startswith('# v0.8.12-alpha — Development Version 299')
    assert 'semi-transparent fills, shadows, glows' in s
    assert 'premultiplied RGBA dry/wet interpolation' in s
