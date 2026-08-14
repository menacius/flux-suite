"""Development Version 231 Video visual effects, overlay and playback performance contract."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
readme = (root / "README.md").read_text(encoding="utf-8")
canvas = (root / "src/canvas/canvas-preview/keyboard-wheel-events.inc").read_text(encoding="utf-8")
editor_events = (root / "src/editor/title-editor/editor-events.inc").read_text(encoding="utf-8")
video_runtime_h = (root / "Core/title-video-runtime.h").read_text(encoding="utf-8")
video_runtime_cpp = (root / "Core/title-video-runtime.cpp").read_text(encoding="utf-8")
compat = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc").read_text(encoding="utf-8")
gpu = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc").read_text(encoding="utf-8")
playback = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc").read_text(encoding="utf-8")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert "Development Version 243" in readme

# Video is image-like for rendering, but it must never be treated as an empty
# still-image placeholder. The hatch/diagonal overlay is only for image layers
# whose image_path is empty.
placeholder = canvas[canvas.index("void CanvasPreview::draw_empty_image_placeholders"):]
assert "layer->type == LayerType::Video" in placeholder
assert "!layer->image_path.empty()" in placeholder

# Visual presets must apply to the Video owner row itself. Video has audio child
# streams, but it is not an audio-only container for preset eligibility.
apply_effect = editor_events[editor_events.index("void TitleEditor::apply_effect_preset_to_layer"):editor_events.index("void TitleEditor::apply_transition_preset_to_layer")]
assert "layer_type_can_have_children(layer->type) && layer->type != LayerType::Video" in apply_effect
assert "effects_panel_->add_effect_from_preset_file(file_path)" in apply_effect

# The runtime returns decoded-frame metadata, not just pixels. Cache keys must be
# driven by the published decoded frame so the GPU does not re-upload the same
# stale frame under a different requested playhead frame every tick.
assert "struct VideoFrame" in video_runtime_h
assert "frame_cache_key_for_layer" in video_runtime_h
assert "requested_frame_number" in video_runtime_cpp
assert "decoded_frame_number" in video_runtime_cpp
assert "video-frame-map=timeline:" in video_runtime_cpp
assert "media-requested:" in video_runtime_cpp
assert ".frame_cache_key_for_layer(" in playback
assert "source_frame_duration()" in playback
assert "|video-frame=" not in playback

# Compatibility and GPU image paths both consume VideoFrame.image, and the direct
# GPU image fast path can use Video frames when no visual effect requires the
# full compatibility surface.
assert "fxm::video::VideoFrame frame" in compat
assert "frame.image" in compat
assert "layer.type == LayerType::Video" in gpu
assert "layer.video_source.empty()" in gpu
assert "FrameRuntime::instance().frame_for_layer(" in gpu
assert "raster = frame.image" in gpu

print("Development Version 231 Video visual effects/performance contract passed")
