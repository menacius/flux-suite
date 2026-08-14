from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_is_at_least_283_and_cache_revision_is_283():
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read('CMakeLists.txt'))
    build_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read('src/core/build-info.h'))
    assert cmake_match and int(cmake_match.group(1)) >= 283
    assert build_match and int(build_match.group(1)) >= 283
    pipeline_match = re.search(r'\|gpu-text-pipeline=(\d+)', read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc'))
    assert pipeline_match and int(pipeline_match.group(1)) >= 283


def test_trim_paths_cache_key_tracks_evaluated_controls():
    compositor = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
    assert 'effect.trim_start_prop.is_animated() ? effect.trim_start_prop.evaluate(t)' in compositor
    assert 'effect.trim_end_prop.is_animated() ? effect.trim_end_prop.evaluate(t)' in compositor
    assert 'effect.trim_offset_prop.is_animated() ? effect.trim_offset_prop.evaluate(t)' in compositor
    assert 'effect.effect_trim_multiple_shapes' in compositor
    assert '|strokeoffset=' in compositor


def test_stroke_offset_is_general_stroke_property_not_trim_parameter():
    layer = read('Core/layer-model.h')
    ui = read('src/editor/properties-panel/color-gradient-editing.inc')
    effect_ui = read('src/effects/effects-panel.cpp')
    descriptor = read('data/effect-transitions/Trim Paths.fxmeffect')
    runtime = read('src/effects/effect-runtime.cpp')
    assert 'float       stroke_offset = 0.0f' in layer
    assert 'AnimatedProperty stroke_offset_prop { "stroke_offset", 0.0 }' in layer
    assert 'OBSTitles.StrokeOffsetColon' in ui
    assert 'toggle_keyframe(layer_->stroke_offset_prop' in ui
    trim_section = effect_ui[effect_ui.index('selected_effect()->type == LayerEffectType::TrimPaths'):]
    trim_section = trim_section[:trim_section.index('selected_effect()->type == LayerEffectType::BackgroundColor')]
    assert 'Stroke Offset' not in trim_section
    assert 'strokeOffset' not in descriptor
    trim_params = runtime[runtime.index('kTrimPathsParameters'):runtime.index('kAppearanceParameters')]
    assert 'effect_stroke_offset' not in trim_params


def test_offset_geometry_precedes_trim_geometry():
    properties = read('../../packages/flux-common/Shared/rendering-engine/title-source/scene-masks-properties.inc')
    header = read('src/layers/stroke-path-geometry.h')
    assert 'apply_stroke_offset_geometry_partitioned' in header
    fn = properties[properties.index('apply_layer_trim_paths_partitioned'):properties.index('static QPainterPath apply_layer_trim_paths')]
    assert fn.index('apply_stroke_offset_geometry_partitioned') < fn.index('apply_trim_paths_geometry_partitioned')


def test_layer_offset_serialization_animation_presets_and_migration():
    data = read('src/core/title-data.cpp')
    hierarchy = read('src/editor/title-editor-internal/hierarchy-model.inc')
    presets = read('src/editor/style-presets.cpp')
    asset = read('Core/asset-runtime.cpp')
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
    defaults = read('src/editor/properties-panel/panel-defaults.inc')
    assert 'j["stroke_offset"] = l.stroke_offset' in data
    assert 'j["stroke_offset_prop"] = aprop_to_json(l.stroke_offset_prop)' in data
    assert 'Development Version 283 migration' in data
    assert 'effect.stroke_offset_prop.is_animated()' in data
    assert '{&layer.stroke_offset_prop, nullptr}' in hierarchy
    assert 'stroke[QStringLiteral("offset")] = layer.stroke_offset' in presets
    assert 'layer.stroke_offset_prop.is_animated()' in asset
    assert 'layer.stroke_offset_prop.is_animated()' in source
    assert 'layer_->stroke_offset = defaults.stroke_offset' in defaults


def test_bounds_and_cache_fingerprints_include_general_offset():
    live = read('src/cache/cache-manager/live-cue-state.inc')
    disk = read('src/cache/cache-manager/disk-cache-storage.inc')
    compositor = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
    assert 'animated_scalar_extents(layer.stroke_offset_prop)' in live
    assert 'add(layer->stroke_offset); add_anim(layer->stroke_offset_prop);' in disk
    assert 'add_anim(layer->stroke_offset_prop);' in disk
    assert 'std::abs(eval_layer_stroke_offset(layer, t))' in compositor
    assert 'layer.stroke_offset_prop.is_animated()' in compositor
    assert 'layer.stroke_offset_prop.is_animated() ||' in read('../../packages/flux-common/Shared/rendering-engine/title-source/scene-masks-properties.inc')


def test_dev282_effect_offset_is_read_only_for_migration():
    data = read('src/core/title-data.cpp')
    write_block = data[data.index('static json layer_to_json'):data.index('static std::shared_ptr<Layer> layer_from_json') if 'static std::shared_ptr<Layer> layer_from_json' in data else data.index('layer_from_json')]
    assert '{"effect_stroke_offset", effect.effect_stroke_offset}' not in write_block
    assert '{"stroke_offset_prop", aprop_to_json(effect.stroke_offset_prop)}' not in write_block
    assert 'json_double(effect_json, "effect_stroke_offset"' in data
    assert 'effect_json.contains("stroke_offset_prop")' in data


def test_changelog_documents_revision_283():
    changelog = read('docs/CHANGELOG.md')
    assert '# v0.8.12-alpha — Development Version 283' in changelog
    assert 'Trim Paths live refresh and general Stroke Offset' in changelog
