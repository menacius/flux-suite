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


def compatibility_motion_block() -> str:
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc")
    start = source.index("Build a normalized shutter exposure")
    end = source.index("cairo_restore(cr);", start)
    return source[start:end]


def test_development_and_gpu_cache_versions_are_287_or_newer():
    cmake = read("CMakeLists.txt")
    build = read("src/core/build-info.h")
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
    build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
    pipeline_version = re.search(r'\|gpu-text-pipeline=(\d+)', lifecycle)
    assert cmake_version and int(cmake_version.group(1)) >= 287
    assert build_version and int(build_version.group(1)) >= 287
    assert pipeline_version and int(pipeline_version.group(1)) >= 287


def test_gpu_shutter_exposure_is_normalized_and_excludes_sharp_frame():
    branch = motion_branch()
    assert "1.0f / static_cast<float>(samples)" in branch
    assert "render_and_accumulate(title_time, 1.0f)" not in branch
    assert "blur_mix / static_cast<double>(samples)" not in branch
    assert "render_temporal_sample(title_time)" in branch
    assert "resolve_gpu_motion_blur(" in branch


def test_gpu_resolve_uses_premultiplied_dry_wet_without_additive_alpha():
    effects = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    start = effects.index("float4 PSTemporalResolve")
    end = effects.index("technique Draw", start)
    shader = effects[start:end]
    assert "float4 exposure = accumulatedImage.Sample" in shader
    assert "float4 currentFrame = sampleImage.Sample" in shader
    assert "float4 result = lerp(currentFrame, wet, mixAmount);" in shader
    assert "max(sharpAlpha" not in shader
    assert "result += exposure" not in shader


def test_compatibility_fallback_resolves_normalized_exposure_with_dry_mix():
    block = compatibility_motion_block()
    assert "const double sample_alpha = 1.0 / (double)sample_times.size();" in block
    assert "resolve_motion_blur_coverage(" in block
    assert "exposure_canvas" in block
    assert "coverage_canvas" in block
    assert "current_canvas" in block
    assert "sharp_mix" not in block


def test_gpu_readback_path_accumulates_only_normalized_exposure_then_resolves():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc")
    start = source.index("static bool gpu_accumulate_motion_raster")
    end = source.index("struct MotionBaseRaster", start)
    block = source[start:end]
    assert "current_opacity / static_cast<double>(sample_times.size())" in block
    assert "draw_at_time(title_time" not in block
    assert "resolve_motion_blur_coverage(" in block
    assert "exposure, coverage, current_canvas, mix" in block
    assert "apply_layer_world_transform(current_canvas_cr.get(), title, layer, title_time);" in block


def test_changelog_documents_revision_287():
    changelog = read("docs/CHANGELOG.md")
    assert "# v0.8.12-alpha — Development Version 287" in changelog
    assert "normalized Motion Blur trail resolve" in changelog
    assert "stacked-copy/brightening artifact" in changelog
