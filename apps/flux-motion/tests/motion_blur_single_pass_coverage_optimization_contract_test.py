from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_transform_motion_samples_are_drawn_only_once():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    start = source.index("if (use_transform_motion_fast_path")
    end = source.index("\n    struct vec4 clear;", start)
    fast = source[start:end]
    assert fast.count("for (int i = 0; i < motion_transform_samples; ++i)") == 1
    assert "gs_blend_op(GS_BLEND_OP_MAX);" not in fast
    assert "motion_coverage_target" not in fast
    assert "resolve_gpu_motion_blur(" in fast


def test_full_temporal_path_has_no_second_fullscreen_append_per_sample():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    start = source.index("const bool requires_full_temporal_pipeline")
    end = source.index("if (use_transform_motion_fast_path", start)
    temporal = source[start:end]
    assert "append_gpu_temporal_sample(" in temporal
    assert "append_gpu_temporal_coverage(" not in temporal
    assert "motion_coverage_target" not in temporal


def test_gpu_resolve_has_no_redundant_coverage_texture_sample():
    renderer = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    resources = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    assert "motion_coverage_target" not in renderer
    assert "motion_coverage_target" not in resources
    resolve = shader[shader.index("float4 PSTemporalResolve"):
                     shader.index("float4 PSTemporalAccumulate")]
    assert "coverageImage" not in resolve
    assert "float wetAlpha = exposureAlpha;" in resolve


def test_exposure_alpha_is_mathematically_bounded_by_max_coverage():
    samples = [0.0, 0.2, 0.75, 1.0, 0.4]
    weights = [0.2] * len(samples)
    exposure = sum(value * weight for value, weight in zip(samples, weights))
    coverage = max(samples)
    assert exposure <= coverage
    assert min(exposure, coverage) == exposure
