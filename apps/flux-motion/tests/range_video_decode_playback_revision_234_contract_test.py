from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
schema = read('Core/title-serialization-schema.h')
manifest = read('tests/test-suite-manifest.json')
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')
popup = read('src/editor/properties-panel/popup-state.inc')
props_sync = read('src/editor/properties-panel/property-synchronization.inc')
timeline_h = read('src/timeline/timeline-widget.h')
timeline_cpp = read('src/timeline/timeline-widget.cpp')
commands = read('src/editor/title-editor/commands-docks.inc')
video_runtime = read('Core/title-video-runtime.cpp')
playback = read('src/editor/title-editor/layout-template-tools.inc')
effects_panel = read('src/effects/effects-panel.cpp')

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 234:' in schema
assert '"development_version": 243' in manifest
assert 'Development Version 243' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

video_section = popup[popup.index('/* ── Video Layer ── */'):popup.index('/* ── Audio Layer ── */')]
assert 'add_form_row(video_form, QStringLiteral("Range"), video_range_row);' in video_section
assert 'add_form_row(video_form, QStringLiteral("Source")' not in video_section
assert 'add_form_row(video_form, QStringLiteral("Preview")' not in video_section
assert 'add_form_row(video_form, QStringLiteral("Feedback")' not in video_section
assert 'Set In' not in video_section and 'Set Out' not in video_section
assert 'spn_video_in_->setPrefix(QStringLiteral("In "))' in video_section
assert 'spn_video_out_->setPrefix(QStringLiteral("Out "))' in video_section
assert 'QHBoxLayout(video_range_row)' in video_section
assert 'lbl_video_range_preview_->setText' not in props_sync
video_sync_section = props_sync[props_sync.index('if (is_video) {'):props_sync.index('if (is_audio) {')]
assert 'range length follows strip' not in video_sync_section

assert 'void layer_timing_changed(bool commit_undo);' in timeline_h
assert 'emit layer_timing_changed(false);' in timeline_cpp
assert 'emit layer_timing_changed(true);' in timeline_cpp
assert 'connect(timeline_, &TimelineWidget::layer_timing_changed' in commands
assert 'props_->set_layer(selected, playhead_)' in commands

assert 'kMaxCachedDecodedFrames = 240' in video_runtime
assert 'nearest_cached_frame_at_or_before_locked' in video_runtime
assert 'decoded_frame_number <= requested_frame' in video_runtime
assert 'future decoded frame' in video_runtime

assert 'Being after a' in playback and 'Pause/Loop zone is a valid current-playhead start' in playback
assert 'playhead_ >= std::clamp(title_->pause_time' not in playback
assert 'title_->playback_mode == 1' in playback and 'loop_start' in playback

for name in [
    'Grain.fxmeffect',
    'Film Distortion.fxmeffect',
    'Analog Distortion.fxmeffect',
    'Digital Distortion.fxmeffect',
]:
    assert (ROOT / 'data/effect-transitions' / name).exists(), name
for removed in [
    'Animated Noise Drift.fxmeffect',
    'Glare Sweep.fxmeffect',
    'Ripple Loop.fxmeffect',
    'Wave Warp Loop.fxmeffect',
    'Chromatic Pulse.fxmeffect',
    'Soft Bloom Highlight.fxmeffect',
    'Cinematic Halation Warm.fxmeffect',
    'Micro Contrast Clarity.fxmeffect',
]:
    assert not (ROOT / 'data/effect-transitions' / removed).exists(), removed

assert 's->setFixedHeight(20)' in effects_panel
assert 'c->setFixedHeight(20)' in effects_panel
assert 'font-size:10px' in effects_panel
for needle in [
    'edt_audio_source_->setFixedHeight(20)',
    'cmb_audio_fade_curve_->setFixedHeight(20)',
    'cmb_audio_playback_->setFixedHeight(20)',
    'cmb_asset_playback_->setFixedHeight(20)',
    'spn_asset_offset_->setFixedHeight(20)',
    'spn_asset_pause_duration_->setFixedHeight(20)',
    'spn_asset_loop_count_->setFixedHeight(20)',
    'spn_appearance_stroke_width_->setFixedHeight(20)',
    'spn_appearance_opacity_->setFixedHeight(20)',
]:
    assert needle in popup, needle

print('Development Version 243 range inspector, video decode, playback and effect animation preset contract: PASS')
