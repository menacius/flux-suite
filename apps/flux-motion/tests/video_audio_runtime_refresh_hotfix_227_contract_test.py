from pathlib import Path

root = Path(__file__).resolve().parents[1]

source_h = (root / "../../packages/flux-common/Shared/rendering-engine/title-source.h").read_text(encoding="utf-8")
source_runtime = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text(encoding="utf-8")
gpu_tick = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc").read_text(encoding="utf-8")
editor_audio = (root / "src/editor/title-editor/editor-audio-preview.inc").read_text(encoding="utf-8")
source_lifecycle = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc").read_text(encoding="utf-8")
commands = (root / "src/editor/title-editor/commands-docks.inc").read_text(encoding="utf-8")
audio_runtime = (root / "../../plugins/obs/flux-motion/src/title-audio-runtime.cpp").read_text(encoding="utf-8")

assert "title_source_set_editor_title_snapshot" in source_h
assert "editor_title_snapshot" in source_runtime
assert "editor_title_snapshot_for_source(data)" in gpu_tick
assert "title_source_set_editor_title_snapshot(editor_audio_preview_source_, title_)" in editor_audio

# Video layers must be treated as time-varying raster sources. Otherwise the GPU
# layer cache keeps the first still frame until an unrelated edit (mute/unmute,
# visibility, selection) dirties the model.
assert "frame_cache_key_for_layer(" in source_lifecycle and "source_frame_duration()" in source_lifecycle
assert "|video-frame=" not in source_lifecycle
assert "layer.type == LayerType::Video ||" in source_lifecycle

# A decoded frame may arrive between playback ticks; the frameReady signal must
# invalidate the canvas even when transport is currently playing.
assert "if (canvas_)\n                    canvas_->refresh_preview();" in commands
assert "!playing_ && canvas_" not in commands

# Audio visibility is separate from mute/solo. Embedded video audio streams must
# remain audible when their visual/timeline lane is collapsed or when the video
# picture visibility is toggled.
assert "s.hidden = false;" in audio_runtime
assert "spec.hidden = false;" in audio_runtime
assert "s.hidden = !l.visible" not in audio_runtime
assert "spec.hidden = !l.visible" not in audio_runtime
