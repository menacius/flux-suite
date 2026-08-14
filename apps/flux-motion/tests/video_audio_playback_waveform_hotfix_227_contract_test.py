"""Development Version 231 hotfix contract for Video playback and waveform decoupling."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

audio_h = read("../../plugins/obs/flux-motion/src/title-audio-runtime.h")
audio_cpp = read("../../plugins/obs/flux-motion/src/title-audio-runtime.cpp")
video_cpp = read("Core/title-video-runtime.cpp")
model = read("Core/layer-model.h")
timeline = read("src/timeline/timeline-widget.cpp")
footer = read("src/editor/title-editor/editor-audio-preview.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
serialization = read("src/core/title-data.cpp")

assert "audio_waveform_progress_percent" in model
assert "audio_waveform_generating" in model
assert "audio_waveform_progress_label" in model
assert "publish_waveform_status" in audio_h and "build_waveform_for_clip" in audio_h
assert "Publish playable PCM immediately" in audio_cpp
assert "publish_decoded(generation, decode_epoch, decoded);" in audio_cpp
assert "build_waveform_for_clip(clip, decode_epoch);" in audio_cpp
assert "pending_waveforms_.clear();\n        for (const auto &clip : decoded)" not in audio_cpp
assert "status.progress_percent" in audio_cpp and "status.label" in audio_cpp
assert "audio_waveform_progress_percent" in serialization
assert "audio_waveform_generating" in serialization
assert "audio_waveform_progress_label" in serialization
assert "Keep presenting the last decoded frame" in video_cpp
assert "result.image = entry->frame;" in video_cpp
assert "l->group_collapsed = true" in commands
assert "linked_audio_stream_layers" in timeline
assert "draw_audio_waveform_lane(streams" in timeline
assert "Waveform %1% — %2" in footer
assert "timeline_->update()" in footer
print("Development Version 231 video/audio waveform hotfix contract passed")
