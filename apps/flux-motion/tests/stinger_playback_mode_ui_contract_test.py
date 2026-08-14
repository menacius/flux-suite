from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

panel_h = read("src/editor/title-properties-panel.h")
panel_cpp = read("src/editor/title-properties-panel.cpp")
model_h = read("Core/title-data.h")
model_cpp = read("src/core/title-data.cpp")
actions = read("src/editor/title-dock/title-actions.inc")
locale = read("data/locale/en-US.ini")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")

assert 'add_playback_button(4, "stinger.svg"' in panel_cpp
assert 'selection == 4' in panel_cpp
assert 'title_->graphic_type = TitleGraphicType::Stinger' in panel_cpp
assert '? 4' in panel_cpp

assert 'stinger_group_' not in panel_h
assert 'stinger_group_' not in panel_cpp
assert 'OBSTitles.StingerSettings' not in panel_cpp
for field in (
    'chk_stinger_audio_', 'chk_stinger_alpha_',
    'spn_stinger_pre_roll_', 'spn_stinger_post_roll_',
    'cmb_stinger_render_mode_', 'lbl_stinger_validation_',
    'cmb_stinger_switch_mode_',
):
    assert f'set_form_field_visible({field}, is_stinger)' in panel_cpp, field
assert 'cmb_stinger_editor_background_' not in panel_h + panel_cpp
assert 'set_form_field_visible(spn_stinger_transition_timecode_, point_switch)' in panel_cpp

assert 'StingerTransitionPointMode' not in model_h
assert 'stinger_transition_point_mode' not in model_cpp
assert 'cmb_stinger_transition_mode_' not in panel_h
assert 'cmb_stinger_transition_mode_' not in panel_cpp
assert 'spn_stinger_transition_frames_' not in panel_cpp
assert 'spn_stinger_transition_percentage_' not in panel_cpp
assert 'new TimecodeSpinBox(this)' in panel_cpp
assert 'set_stinger_transition_point_seconds(*title_, seconds)' in panel_cpp
assert 'stinger_transition_point_mode' not in actions
for obsolete in (
    'OBSTitles.StingerSettings=', 'OBSTitles.StingerTransitionFormat=',
    'OBSTitles.StingerTransitionFrames=', 'OBSTitles.StingerTransitionTimecode=',
    'OBSTitles.StingerTransitionPercentage=', 'OBSTitles.StingerFramesSuffix=',
):
    assert obsolete not in locale, obsolete

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info
print("Stinger playback-mode UI contract passed")
