from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding='utf-8')

model_h = read('Core/title-data.h')
model_cpp = read('src/core/title-data.cpp')
panel_h = read('src/editor/title-properties-panel.h')
panel_cpp = read('src/editor/title-properties-panel.cpp')
timeline_h = read('src/timeline/timeline-widget.h')
timeline_cpp = read('src/timeline/timeline-widget.cpp')
actions = read('src/editor/title-dock/title-actions.inc')
dock_icons = read('src/editor/title-dock/import-export-helpers.inc')
library = read('src/editor/title-dock/dock-ui.inc')
transition = read('../../plugins/obs/flux-motion/src/stinger-transition.cpp')
plugin = read('../../plugins/obs/flux-motion/src/plugin-main.cpp')
cmake = read('CMakeLists.txt')
locale = read('data/locale/en-US.ini')

for token in (
    'enum class TitleGraphicType',
    'Title = 0', 'Graphic = 1', 'Mask = 2', 'Stinger = 3',
    'stinger_transition_point', 'stinger_audio_enabled',
    'stinger_alpha_output', 'stinger_pre_roll', 'stinger_post_roll',
    'StingerRenderMode', 'StingerSwitchMode', 'StingerEditorBackground',
    'validate_stinger_title', 'ensure_stinger_transition_input_layers',
):
    assert token in model_h, token

for key in (
    '"graphic_type"', '"stinger_transition_point"',
    '"stinger_audio_enabled"',
    '"stinger_alpha_output"', '"stinger_pre_roll"',
    '"stinger_post_roll"', '"stinger_render_mode"',
    '"stinger_switch_mode"', '"stinger_editor_background"',
    '"transition_input_slot"',
):
    assert key in model_cpp, key
assert 'has_explicit_graphic_type' in model_cpp
assert 'TitleGraphicType::Graphic' in model_cpp

for token in (
    'spn_stinger_transition_timecode_', 'chk_stinger_audio_',
    'chk_stinger_alpha_', 'spn_stinger_pre_roll_',
    'spn_stinger_post_roll_', 'cmb_stinger_render_mode_',
    'lbl_stinger_validation_',
):
    assert token in panel_h, token
for token in (
    'add_playback_button(4, "stinger.svg"',
    'TitleGraphicType::Stinger',
    'StingerRenderMode::ProceduralLive',
    'StingerRenderMode::PrerenderedProxy',
    'validate_stinger_title',
):
    assert token in panel_cpp, token

assert 'StingerTransitionPoint' in timeline_h
for token in (
    'OBSTitles.StingerSceneSwitch',
    'stinger_transition_point_seconds(*title_)',
    'set_stinger_transition_point_seconds(*title_, t)',
    'DragMode::StingerTransitionPoint',
):
    assert token in timeline_cpp, token

for token in (
    'OBSTitles.GraphicTypeStinger', 'TitleGraphicType::Stinger',
    'stinger_transition_point = 1.0',
):
    assert token in actions, token
assert 'icon_name = "stinger.svg"' in dock_icons
assert 'obs_icon("stinger.svg"' in library
assert 'TitleGraphicType::Stinger' in library
assert (root / 'data/icons/stinger.svg').is_file()

for token in (
    'OBS_SOURCE_TYPE_TRANSITION',
    'flux_motion_stinger_transition',
    'obs_transition_enable_fixed',
    'obs_transition_video_render_direct',
    'obs_transition_audio_render',
    'transition_start', 'transition_stop',
    'StingerRenderMode::PrerenderedProxy',
    'CacheManager::instance().queueWholeTimeline',
):
    assert token in transition, token
assert 'stinger_transition_register();' in plugin
assert '../../plugins/obs/flux-motion/src/stinger-transition.cpp' in cmake
assert '../../plugins/obs/flux-motion/src/stinger-transition.h' in cmake
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake

for key in (
    'OBSTitles.GraphicTypeStinger=',
    'OBSTitles.StingerSceneSwitch=',
    'OBSTitles.StingerObsTransition=',
):
    assert key in locale, key

assert 'stinger_transition_point_mode' not in model_h
assert 'stinger_transition_point_mode' not in model_cpp
assert 'cmb_stinger_transition_mode_' not in panel_h
assert 'cmb_stinger_transition_mode_' not in panel_cpp
assert 'spn_stinger_transition_frames_' not in panel_cpp
assert 'spn_stinger_transition_percentage_' not in panel_cpp
assert 'QGroupBox(fxm_tr("OBSTitles.StingerSettings")' not in panel_cpp
assert 'set_form_field_visible(spn_stinger_transition_timecode_, point_switch)' in panel_cpp

print('Stinger graphic type contract passed')
