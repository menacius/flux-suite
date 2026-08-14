from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
model = read('Core/title-data.h')
data = read('src/core/title-data.cpp')
dock = read('src/editor/title-dock/live-text-cache-playlist.inc')
cache = read('src/cache/cache-manager/live-cue-state.inc')
utils = read('src/core/live-text-cue-utils.h')
schema = read('Core/title-serialization-schema.h')

def test_live_cue_fill_stroke_per_row_revision_255_contract():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "255"' in cmake
    assert 'FXM_DEVELOPMENT_VERSION "255"' in build

    assert 'struct LiveTextCueStyleOverride' in model
    assert 'live_text_cue_style_overrides' in model
    assert 'row_id' in model and 'layer_id' in model
    assert 'fill_color_set' in model and 'stroke_color_set' in model

    assert 'jt["live_text_cue_style_overrides"]' in data
    assert 'prune_live_text_cue_style_overrides' in data
    assert 'live_text_cue_effective_color' in data
    assert 'set_live_text_cue_color_override' in data
    assert 'clear_live_text_cue_color_override' in data
    assert 'live_text_layer_is_text_like(layer) ? layer.text_color : layer.fill_color' in data

    assert 'recover_array_member(title, "live_text_cue_style_overrides", report);' in schema
    assert 'remove_non_objects("live_text_cue_style_overrides");' in schema

    assert 'add_color_control(QStringLiteral("Fill:"), false);' in dock
    assert 'add_color_control(QStringLiteral("Stroke:"), true);' in dock
    assert 'set_live_text_cue_color_override(*title, *layer, cue_row, stroke, argb);' in dock
    assert 'clear_live_text_cue_color_override(*title, *layer, cue_row, stroke);' in dock
    assert 'live_text_cue_effective_color(*title, *exposed[col], row, stroke)' in dock
    assert 'Reset Fill to default' in dock and 'Reset Stroke to default' in dock

    assert 'apply_live_text_cue_style_to_layer(*cue_title, *target, row);' in cache
    assert 'live_text_cue_effective_color(*cue_title, *target, row, false)' in cache
    assert 'live_text_cue_effective_color(*cue_title, *target, row, true)' in cache
    assert 'fxm::live_text::is_exposed_cue_layer(layer)' in cache

    assert 'prune_live_text_cue_style_overrides(*title);' in utils
