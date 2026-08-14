from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_and_gpu_raster_cache_revision_are_285_or_newer():
    cmake = read("CMakeLists.txt")
    build = read("src/core/build-info.h")
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
    build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
    pipeline_version = re.search(r'\|gpu-text-pipeline=(\d+)', lifecycle)
    assert cmake_version and int(cmake_version.group(1)) >= 285
    assert build_version and int(build_version.group(1)) >= 285
    assert pipeline_version and int(pipeline_version.group(1)) >= 285


def test_trim_paths_survives_base_raster_effect_filtering():
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    start = lifecycle.index("Layer base_key_layer = layer;")
    end = lifecycle.index("const double base_local_time", start)
    block = lifecycle[start:end]
    assert "base_key_layer.effects.clear()" not in block
    assert "base_key_layer.effects.erase(" in block
    assert "std::remove_if(" in block
    assert "effect.type != LayerEffectType::TrimPaths" in block


def test_trim_values_and_animation_participate_in_base_raster_identity():
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    compositor = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
    start = lifecycle.index("Layer base_key_layer = layer;")
    end = lifecycle.index("auto &entry = session->layers[id];", start)
    block = lifecycle[start:end]
    assert "effect_layer_cache_key(" in block
    assert "nullptr, session->title, base_key_layer" in block
    assert "layer_has_non_transform_animation(base_key_layer)" in lifecycle
    assert "effect.trim_start_prop.is_animated() ? effect.trim_start_prop.evaluate(t)" in compositor
    assert "effect.trim_end_prop.is_animated() ? effect.trim_end_prop.evaluate(t)" in compositor
    assert "effect.trim_offset_prop.is_animated() ? effect.trim_offset_prop.evaluate(t)" in compositor
    assert "effect.effect_trim_multiple_shapes" in compositor


def test_trim_paths_remains_geometry_stage_and_pixel_effects_stay_post_raster():
    properties = read("../../packages/flux-common/Shared/rendering-engine/title-source/scene-masks-properties.inc")
    compositor = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
    assert "apply_layer_trim_paths_partitioned" in properties
    assert "results = fxm::apply_trim_paths_geometry_partitioned(results, options);" in properties
    assert "effect_config.type == LayerEffectType::TrimPaths" in compositor
    assert "continue;" in compositor[compositor.index("effect_config.type == LayerEffectType::TrimPaths"):][:200]


def test_changelog_documents_revision_285():
    changelog = read("docs/CHANGELOG.md")
    assert "# v0.8.12-alpha — Development Version 285" in changelog
    assert "Trim Paths base-raster live invalidation fix" in changelog
