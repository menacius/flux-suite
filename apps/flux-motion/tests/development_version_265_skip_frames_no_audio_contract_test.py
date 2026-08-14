#!/usr/bin/env python3
"""Development Version 265 contract: Skip Frames must not depend on an audio layer."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_skip_frames_without_audio_layer_falls_back_to_wall_clock_playback():
    tick = read("src/editor/title-editor/signal-handlers.inc")

    assert "title_has_editor_audio_clock" in tick
    assert "layer && layer->type == LayerType::Audio" in tick
    assert "dt = playback_clock_.isValid() ? playback_clock_.restart() / 1000.0 : 0.0;" in tick
    assert "obs_source_media_get_time(editor_audio_preview_source_)" in tick
    assert "editor_audio_preview_source_ &&\n        title_has_editor_audio_clock(title_)" in tick

    guarded = tick.split("title_has_editor_audio_clock(title_)) {")[1].split("if (cache_settings.cached_frames_only")[0]
    assert "obs_source_media_get_time(editor_audio_preview_source_)" in guarded


def test_play_every_frame_path_is_unchanged():
    prerender = read("src/cache/prerender-dock.cpp")
    preview = read("src/editor/title-editor/editor-audio-preview.inc")

    assert "settings.play_every_frame = cadence_mode_ && cadence_mode_->currentData().toInt() == 1;" in prerender
    assert "editor_audio_play_every_frame_speed_factor" in preview
    assert "timeline_seconds / wall_seconds" in preview


if __name__ == "__main__":
    test_skip_frames_without_audio_layer_falls_back_to_wall_clock_playback()
    test_play_every_frame_path_is_unchanged()
    print("development version 265 skip-frames/no-audio-layer contract passed")
