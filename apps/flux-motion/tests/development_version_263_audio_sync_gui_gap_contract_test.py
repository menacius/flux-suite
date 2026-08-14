#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_editor_audio_meter_has_no_bottom_elastic_spacer():
    preview = read("src/editor/title-editor/editor-audio-preview.inc")
    assert "layout->addWidget(editor_audio_meter_, 1, Qt::AlignHCenter);" in preview
    meter_section = preview.split("editor_audio_meter_ = new FxmEditorAudioMeter(panel);")[1].split("editor_audio_meter_timer_ = new QTimer(panel);")[0]
    assert "addStretch" not in meter_section


def test_playback_cache_cadence_dropdown_defaults_to_skip_frames():
    prerender_h = read("src/cache/prerender-dock.h")
    prerender = read("src/cache/prerender-dock.cpp")
    locale = read("data/locale/en-US.ini")
    assert "QComboBox *cadence_mode_" in prerender_h
    assert "kPrerenderCadenceModeKey" in prerender
    assert 'cadence_mode_->addItem(fxm_tr("OBSTitles.SkipFrames"), 0);' in prerender
    assert 'cadence_mode_->addItem(fxm_tr("OBSTitles.PlayEveryFrame"), 1);' in prerender
    assert "kPrerenderCadenceModeKey), 0" in prerender
    assert "settings.play_every_frame = cadence_mode_ && cadence_mode_->currentData().toInt() == 1;" in prerender
    assert "setValue(QString::fromUtf8(kPrerenderCadenceModeKey), cadence_mode_->currentIndex())" in prerender
    assert 'OBSTitles.EditorPlaybackCadence="A/V sync"' in locale
    assert 'OBSTitles.PlayEveryFrame="Play Every Frame"' in locale


def test_play_every_frame_keeps_editor_audio_audible_and_updates_dock_immediately():
    preview = read("src/editor/title-editor/editor-audio-preview.inc")
    cache_h = read("src/cache/cache-manager.h")
    cache = read("src/cache/cache-manager/worker-publication.inc")
    assert "playbackSettingsChanged" in cache_h
    assert "emit playbackSettingsChanged();" in cache
    assert "CacheManager::instance().playbackSettings().play_every_frame" in preview
    assert "editor_audio_monitor_enabled_" in preview
    assert "Play Every Frame varispeeds audio to visual cadence" in preview
    assert "editor_audio_meter_->setSource(editor_audio_preview_source_)" in preview
    assert "editor_audio_meter_->tick();" in preview
    assert "obs_source_media_play_pause(editor_audio_preview_source_, !playing_)" in preview
    assert "editor_audio_play_every_frame_speed_factor" in preview


def test_skip_frames_uses_live_editor_audio_clock_for_visual_sync():
    tick = read("src/editor/title-editor/signal-handlers.inc")
    runtime = read("../../plugins/obs/flux-motion/src/title-audio-runtime.cpp")
    assert "obs_source_media_get_time(editor_audio_preview_source_)" in tick
    assert "obs_source_media_get_state(editor_audio_preview_source_)" in tick
    assert "last_audio_level_update_ns > 0" in tick
    assert "kAudioClockTrustWindowSeconds" in tick
    assert "timeline_ms_for_transport_cursor" in runtime
    assert "preserve_editor_audio_clock" in runtime
    assert "reported_time_ms_.store" in runtime


def test_playback_cache_panel_has_no_bottom_elastic_spacer():
    prerender = read("src/cache/prerender-dock.cpp")
    tail = prerender.split("diagnostics_ = new QLabel(cache_section_);")[1].split("for (auto *combo")[0]
    assert "root->addStretch" not in tail


if __name__ == "__main__":
    test_editor_audio_meter_has_no_bottom_elastic_spacer()
    test_playback_cache_cadence_dropdown_defaults_to_skip_frames()
    test_play_every_frame_keeps_editor_audio_audible_and_updates_dock_immediately()
    test_skip_frames_uses_live_editor_audio_clock_for_visual_sync()
    test_playback_cache_panel_has_no_bottom_elastic_spacer()
    print("development version 263/264 audio sync/gui gap contracts passed")
