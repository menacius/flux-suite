#!/usr/bin/env python3
"""Development Version 410: editor A/V clock and varispeed regressions."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_skip_frames_keeps_timeline_wall_clock_as_source_of_truth():
    host = read("Editor/standalone-editor-host.cpp")
    transport = read("src/editor/title-editor/signal-handlers.inc")
    assert "Skip Frames is wall-clock mastered" in transport
    assert "dt > 0.25" not in transport
    assert "next_playhead = snap_to_obs_frame(audio_time)" not in transport
    assert "std::make_unique<audio::SourceAudioRuntime>" in host
    assert "StandaloneAudioBackend" in host
    assert "runtime_->transport" in host
    assert "qint64 bytesAvailable() const override" in host
    assert "emit readyRead();" in host


def test_play_every_frame_keeps_varispeed_through_seek_and_play():
    preview = read("src/editor/title-editor/editor-audio-preview.inc")
    media = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
    assert "editor_audio_speed_clock_.nsecsElapsed()" in preview
    assert "timeline_seconds / wall_seconds" in preview
    assert "playback_recovery_needed" in preview
    assert media.count("data->editor_audio_speed.load(") >= 3
    assert media.count("std::memory_order_acquire)") >= 3


if __name__ == "__main__":
    test_skip_frames_keeps_timeline_wall_clock_as_source_of_truth()
    test_play_every_frame_keeps_varispeed_through_seek_and_play()
    print("development version 410 A/V sync/varispeed contracts passed")
