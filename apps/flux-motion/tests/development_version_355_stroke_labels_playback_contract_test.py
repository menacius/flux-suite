from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
model = read("Core/layer-model.h")
serialization = read("src/core/title-data.cpp")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
render = read("src/editor/title-editor-internal/canvas-rendering-helpers.inc")
source_render = read("../../packages/flux-common/Shared/rendering-engine/title-source/scene-masks-properties.inc")
source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
asset_runtime = read("Core/asset-runtime.cpp")
asset_runtime_test = read("tests/asset_runtime_contract_test.cpp")
properties = read("src/editor/properties-panel/popup-state.inc")
property_sync = read("src/editor/properties-panel/property-synchronization.inc")
property_menus = read("src/editor/properties-panel/construction-transform-character.inc")
refresh = read("src/editor/properties-panel/selection-refresh.inc")
transport = read("src/editor/title-editor/signal-handlers.inc")
transport_actions = read("src/editor/title-editor/layout-template-tools.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 355
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 355

# Appearance keeps Fill first, places Emissive immediately after it, and gives
# Stroke the same real keyframe control/navigation/context-menu behavior.
appearance = properties[properties.index("btn_kf_appearance_fill_") :
                        properties.index("fxm_add_panel_section(vl, appearance_box_")]
assert appearance.index("add_appearance_row(0") < appearance.index(
    "1, btn_kf_material_emissive_color_") < appearance.index(
        "add_appearance_row(3")
assert 'btn_kf_appearance_stroke_ = mk_kf_button' in properties
assert "connect(btn_kf_appearance_stroke_" in property_sync
assert "install_group_delete_all(btn_kf_appearance_stroke_" in property_menus
assert "set_group_kf_icon(btn_kf_appearance_stroke_" in refresh

# Stroke color and size share one keyframe control. Width is persisted and
# evaluated in the timeline/render/cache/runtime paths along with ARGB.
for channel in "argb":
    name = f"stroke_color_{channel}"
    assert name in model
    assert f'j["{name}"] = aprop_to_json' in serialization
    assert f'j.contains("{name}")' in serialization
    assert name in hierarchy
    assert name in render
    assert name in source_render
    assert name in source_runtime
    assert name in asset_runtime
assert 'scalar_group_timeline_property("stroke_color"' in hierarchy
assert 'stroke_width_prop { "stroke_width", 0.0 }' in model
assert 'j["stroke_width_prop"] = aprop_to_json' in serialization
assert 'j.contains("stroke_width_prop")' in serialization
assert '{&layer.stroke_width_prop, nullptr}' in hierarchy
assert 'if (name == "stroke_width") return fxm_tr("OBSTitles.StrokeWidth")' in hierarchy
assert 'layer.stroke_width_prop.evaluate(t)' in render
assert 'layer.stroke_width_prop.is_animated()' in source_runtime
assert 'layer.stroke_width_prop.is_animated()' in asset_runtime
assert "return eval_stroke_color(layer, t);" in render
assert "stroke color keyframes must mark an asset as animated" in asset_runtime_test

# Every nested Properties label and switch text (including Lock Scale) receives
# one camera-inspector typography pass using the current OBS palette.
assert "for (QLabel *label : inner->findChildren<QLabel *>())" in property_sync
assert "for (FxmSwitch *toggle : inner->findChildren<FxmSwitch *>())" in property_sync
assert "property_label_font.setPixelSize(10);" in property_sync
assert "toggle_palette.setColor(QPalette::WindowText, subtle_text);" in property_sync
assert 'chk_scale_lock_->setText(QStringLiteral("Lock Scale"));' in properties

# Start, preview Mode, authored Playback Mode and A/V cadence are independent
# and deterministic. A cached-only terminal frame is checked before transport
# is stopped, so the final frame is never skipped.
start_block = transport_actions[transport_actions.index("if (cache_settings.from_beginning)") :
                                transport_actions.index("act_play_->setText", transport_actions.index("if (cache_settings.from_beginning)"))]
assert "on_playhead_changed(0.0);" in start_block
assert "title_->loop_start" not in start_block
assert "if (!cache_settings.play_every_frame)" in transport
assert "dt = std::max(0.000001, obs_frame_duration());" in transport
assert "cache_settings.mode == CachePlaybackMode::Loop" in transport
assert "cache_settings.mode == CachePlaybackMode::PingPong" in transport
assert "cache_settings.mode == CachePlaybackMode::PlayOnce" in transport
assert "switch (title_->playback_mode)" in transport
assert "const bool authored_ping_pong" in transport
assert "if (!preview_ping_pong && !authored_ping_pong)" in transport
assert "const bool approaching_pause" in transport
terminal = transport[transport.index("double next_playhead = snap_to_obs_frame(t);") :]
assert terminal.index("prepare_cached_playback_frame(next_playhead)") < terminal.index(
    "if (stop_after_frame)")
assert "sync_editor_audio_preview(false);" in terminal

print("Development Version 355 stroke/labels/playback contract: PASS")
