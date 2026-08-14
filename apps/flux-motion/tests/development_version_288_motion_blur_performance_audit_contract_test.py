from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def motion_branch() -> str:
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    start = source.index("const LayerEffect *motion_config")
    end = source.index("gs_texture_t *source_texture", start)
    return source[start:end]


def append_helper() -> str:
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    start = source.index("static bool append_gpu_temporal_sample")
    end = source.index("static bool resolve_gpu_motion_blur", start)
    return source[start:end]


def test_development_and_cache_versions_are_288_or_newer():
    cmake = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    build = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h"))
    pipeline = re.search(r'\|gpu-text-pipeline=(\d+)', read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc"))
    manifest = json.loads(read("tests/test-suite-manifest.json"))[
        "development_version"]
    assert cmake and int(cmake.group(1)) >= 288
    assert build and int(build.group(1)) >= 288
    assert pipeline and int(pipeline.group(1)) >= 288
    assert manifest >= 288


def test_transform_motion_reuses_one_effected_texture():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    assert "use_transform_motion_fast_path" in source
    assert "The fast path uses the same normalized exposure plus temporal" in source
    fast_start = source.index("if (use_transform_motion_fast_path")
    fast_end = source.index("\n    struct vec4 clear;", fast_start)
    fast = source[fast_start:fast_end]
    assert "apply_gpu_layer_effect_stack(" not in fast
    assert "render_gpu_layer_base_raster(" not in fast
    assert "render_gpu_layer_to_target(" not in fast
    assert "draw_gpu_layer_texture(" in fast
    assert "motion_transform_samples" in fast
    assert "resolve_gpu_motion_blur(" in fast
    assert "motion_accum_targets" not in fast
    assert "GS_BLEND_ONE, GS_BLEND_INVSRCALPHA" in fast


def test_dormant_transition_descriptors_do_not_select_full_sampler():
    primitives = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc")
    branch = motion_branch()
    assert "motion_shutter_overlaps_transition" in primitives
    assert "layer_has_text_transition_during_shutter" in primitives
    assert "layer_has_general_blur_during_shutter" in primitives
    assert "active_text_transition" in branch
    assert "active_general_blur" in branch
    assert "transition_managed" in branch
    assert "has_timed_transition" not in branch


def test_complete_pipeline_sampler_has_strict_realtime_budget():
    branch = motion_branch()
    assert "int sample_cap = temporal_base_raster" in branch
    assert "temporal_base_raster && image_or_video_motion" in branch
    assert "temporal_base_raster ? 4 : 6" in branch
    assert "gpu_motion_blur_realtime_sample_cap(" in branch
    assert "const double density = temporal_base_raster" in branch
    assert "projected_motion ? 0.18 : 0.12" in branch
    assert "projected_motion ? 0.42 : 0.30" in branch


def test_dynamic_exposure_accumulates_in_place_without_ping_pong_readback():
    helper = append_helper()
    effects = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    branch = motion_branch()
    assert "gs_texrender_reset(target);" in helper
    assert "gs_clear(" not in helper
    assert 'gs_effect_loop(effect, "Accumulate")' in helper
    assert "GS_BLEND_ONE, GS_BLEND_ONE" in helper
    assert "technique Accumulate" in effects
    assert "append_gpu_temporal_sample(" in branch
    assert "composite_gpu_temporal_sample(" not in branch
    assert "motion_accum_target" in branch
    session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    assert "motion_accum_targets[2]" not in session


def test_video_does_not_automatically_force_complete_temporal_rerender():
    branch = motion_branch()
    temporal_start = branch.index("const bool temporal_source_change")
    temporal_end = branch.index("const bool requires_full_temporal_pipeline", temporal_start)
    predicate = branch[temporal_start:temporal_end]
    assert "LayerType::Video" not in predicate
    assert "LayerType::Ticker" in predicate
    assert "LayerType::TransitionInput" in predicate


def test_documentation_records_the_render_time_regression_and_fix():
    readme = read("README.md")
    changelog = read("docs/CHANGELOG.md")
    assert "Development Version 288" in readme
    assert "# v0.8.12-alpha — Development Version 288" in changelog
    assert "Average time to render frame" in changelog
    assert "transform-only GPU path" in changelog
    assert "in-place additive accumulation" in changelog
