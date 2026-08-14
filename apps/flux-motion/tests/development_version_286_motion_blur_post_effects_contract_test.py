from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def motion_branch() -> str:
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    start = source.index("const LayerEffect *motion_config")
    end = source.index("gs_texture_t *source_texture", start)
    return source[start:end]


def test_development_and_gpu_cache_versions_are_286_or_newer():
    cmake = read("CMakeLists.txt")
    build = read("src/core/build-info.h")
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
    build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
    pipeline_version = re.search(r'\|gpu-text-pipeline=(\d+)', lifecycle)
    assert cmake_version and int(cmake_version.group(1)) >= 286
    assert build_version and int(build_version.group(1)) >= 286
    assert pipeline_version and int(pipeline_version.group(1)) >= 286


def test_shutter_exposure_is_normalized_and_resolved_without_sharp_reinforcement():
    branch = motion_branch()
    assert "render_temporal_sample(title_time)" in branch
    assert "resolve_gpu_motion_blur(" in branch
    assert "1.0f / static_cast<float>(samples)" in branch
    assert "render_and_accumulate(title_time, 1.0f)" not in branch
    shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    assert "float4 result = lerp(currentFrame, wet, mixAmount);" in shader
    assert "max(sharpAlpha" not in shader


def test_motion_blur_samples_complete_effected_and_transitioned_layer():
    branch = motion_branch()
    assert "Layer temporal_layer = layer_without_motion_blur_effects(layer);" in branch
    assert "resolved_text_transition_animator_stack(" in branch
    assert "render_gpu_layer_to_target(" in branch
    assert "motion_sample_target, apply_pixel_effects" in branch
    assert "active_text_transition" in branch
    assert "active_general_blur" in branch
    assert "requires_full_temporal_pipeline" in branch

    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    normal_start = source.index("gs_texture_t *source_texture", source.index("const LayerEffect *motion_config"))
    normal_end = source.index("static bool copy_full_canvas_gpu_texture", normal_start)
    normal_path = source[normal_start:normal_end]
    assert "apply_gpu_layer_effect_stack(" in normal_path
    assert "LayerEffectSpace::LayerSpace" in normal_path
    assert "LayerEffectSpace::ScreenSpace" in normal_path


def test_trim_paths_and_dynamic_text_regenerate_source_raster_per_sample():
    branch = motion_branch()
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    motion_start = source.index("const LayerEffect *motion_config")
    missing_raster_check = source.index("if (!transition_input && !entry.texture)")
    assert missing_raster_check > motion_start
    assert "effect.type != LayerEffectType::TrimPaths" in branch
    assert (
        "layer_has_non_transform_animation(raster_probe)" in branch or
        "layer_non_transform_animation_changes_during_interval(" in branch
    )
    assert "render_gpu_layer_base_raster(" in branch
    assert "sample_resolved_time" in branch
    assert "Empty Trim Paths/text-transition samples" in branch


def test_temporal_composite_preserves_premultiplied_alpha_contract():
    effects = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    assert "kGpuTemporalCompositeEffect" in effects
    shader_start = effects.index("kGpuTemporalCompositeEffect")
    shader_end = effects.index(')";', shader_start)
    shader = effects[shader_start:shader_end]
    assert "result += sampleImage.Sample" in shader
    assert "float4 exposure" in shader
    assert "float4 currentFrame" in shader
    assert "float4 result = lerp(currentFrame, wet, mixAmount);" in shader
    assert "technique Resolve" in shader
    assert "result = saturate(result);" in shader
    assert "result.rgb = min(result.rgb" in shader


def test_nested_source_aware_motion_uses_isolated_targets():
    session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    branch = motion_branch()
    assert "int motion_temporal_depth = 0;" in session
    assert "nested_temporal_pass" in branch
    assert "local_motion_sample_target" in branch
    assert "local_motion_accum_target" in branch
    assert "++session->motion_temporal_depth" in branch
    assert "--session->motion_temporal_depth" in branch


def test_changelog_documents_revision_286():
    changelog = read("docs/CHANGELOG.md")
    assert "# v0.8.12-alpha — Development Version 286" in changelog
    assert "opaque post-effects Motion Blur" in changelog
