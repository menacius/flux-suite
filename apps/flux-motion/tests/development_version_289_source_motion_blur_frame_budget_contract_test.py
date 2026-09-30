from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_development_and_gpu_cache_versions_include_289_feature_in_current_build():
    cmake = int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt")).group(1))
    build = int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h")).group(1))
    pipeline = int(re.search(r'\|gpu-text-pipeline=(\d+)', read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")).group(1))
    manifest = json.loads(read("tests/test-suite-manifest.json"))["development_version"]
    assert cmake == build == manifest
    assert min(cmake, pipeline) >= 289


def test_live_source_and_stinger_sessions_are_explicitly_realtime():
    session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    header = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc")
    stinger = read("../../plugins/obs/flux-motion/src/stinger-transition.cpp")
    assert "std::atomic<bool> realtime_output { false };" in session
    assert "title_gpu_render_session_set_realtime_output" in header
    assert "session->realtime_output.store(" in lifecycle
    assert "title_gpu_render_session_is_realtime_output" in lifecycle
    assert re.search(
        r"data->gpu_render_session = title_gpu_render_session_create\(\);\s*"
        r"title_gpu_render_session_set_realtime_output\(\s*"
        r"data->gpu_render_session, true\);", source)
    assert re.search(
        r"data->manual_render_session = title_gpu_render_session_create\(\);\s*"
        r"title_gpu_render_session_set_realtime_output\(\s*"
        r"data->manual_render_session, true\);", stinger)


def test_samples_restore_authored_minimum_with_path_specific_caps():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    branch_start = source.index("const LayerEffect *motion_config")
    branch_end = source.index("gs_texture_t *source_texture", branch_start)
    branch = source[branch_start:branch_end]
    assert branch.count("motion_blur_quality_sample_count(") >= 2
    helper = read("../../packages/flux-common/Shared/motion-blur-sampling.cpp")
    assert "std::max(authored, adaptive)" in helper
    assert "std::min(configured, adaptive)" not in branch
    assert "gpu_motion_blur_realtime_sample_cap(session, false, false)" in branch
    assert "session, true, temporal_base_raster" in branch

def test_source_budget_distinguishes_transform_gpu_and_cpu_raster_paths():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    start = source.index("static int gpu_motion_blur_realtime_sample_cap")
    end = source.index("static double gpu_motion_blur_temporal_raster_scale", start)
    helper = source[start:end]
    assert "bool requires_cpu_raster = false" in helper
    assert "return pixels >= 3000000ull ? 2 : 3;" in helper
    assert "return 12;" in helper
    assert "return 64;" in helper
    assert "return 32;" in helper
    assert "return pixels >= 3000000ull ? 2 : 3;" in helper


def test_only_historical_cpu_rasters_are_resolution_reduced():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    helper_start = source.index("static double gpu_motion_blur_temporal_raster_scale")
    helper_end = source.index("static bool render_gpu_layer_to_target", helper_start)
    helper = source[helper_start:helper_end]
    assert "return physical_scale * 0.375;" in helper
    assert "return physical_scale * 0.5;" in helper
    assert "return physical_scale * 0.625;" in helper
    assert "gpu_motion_blur_temporal_raster_scale(session)" in source
    assert "render_temporal_sample(title_time)" in source
    assert "resolve_gpu_motion_blur(" in source


def test_dynamic_rerender_predicate_is_limited_to_current_shutter_interval():
    runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    assert "animated_property_changes_during_interval" in runtime
    assert "animated_vec2_property_changes_during_interval" in runtime
    assert "effect_animation_changes_during_interval" in runtime
    assert "layer_non_transform_animation_changes_during_interval" in runtime
    assert "temporal_local_start" in source
    assert "temporal_local_end" in source
    assert source.count(
        "layer_non_transform_animation_changes_during_interval(") >= 2


def test_compatibility_path_keeps_cpu_budget_but_restores_gpu_density():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc")
    start = source.index("static bool render_motion_blurred_layer")
    end = source.index("static void render_layer_unmasked_with_stackable_effects", start)
    block = source[start:end]
    assert "title_gpu_render_session_is_realtime_output(" in block
    assert "reusable_transform_raster" in block
    assert "MotionBlurSamplingRequest sampling_request" in block
    assert "sampling_request.imageLike = sharp_image_layer" in block
    assert "sampling_request.sourceChangesDuringShutter = !reusable_transform_raster" in block
    assert "makeMotionBlurSamplingPlan(" in block

    presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    fast_start = presentation.index("if (!requires_full_temporal_pipeline)")
    fast_end = presentation.index("} else {", fast_start)
    fast = presentation[fast_start:fast_end]
    assert "image_or_video_motion ? 1.50 : 1.00" in fast
    assert "std::min(sample_cap, 24)" not in fast

    policy = read("../../packages/flux-common/Shared/motion-blur-sampling.cpp")
    assert "request.imageLike ? 1.5 : 1.0" in policy
    assert "request.imageLike ? 64 : 48" in policy

def test_documentation_records_source_only_average_render_fix():
    changelog = read("docs/CHANGELOG.md")
    guide = read("docs/RENDERING_AND_CACHE.md")
    assert "## Development Version 289 — OBS source Motion Blur frame budget" in changelog
    assert "OBS source Motion Blur frame budget" in changelog
    assert "32–40 GPU draws" in changelog
    assert "CPU-raster temporal passes" in guide
    assert "minimum quality request" in guide
    assert "CPU-raster temporal passes" in guide
