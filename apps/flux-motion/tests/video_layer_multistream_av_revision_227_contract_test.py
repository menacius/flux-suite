#!/usr/bin/env python3
"""Development Version 231 Video layer and multistream A/V synchronization contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
read = lambda rel: (ROOT / rel).read_text(encoding="utf-8")

model = read("Core/layer-model.h")
data = read("src/core/title-data.cpp")
schema = read("Core/title-serialization-schema.h")
cmake = read("CMakeLists.txt")
video_h = read("Core/title-video-runtime.h")
video_cpp = read("Core/title-video-runtime.cpp")
audio_cpp = read("../../plugins/obs/flux-motion/src/title-audio-runtime.cpp")
raster = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
props = read("src/editor/properties-panel/auto-style-and-property-actions.inc")
timeline = read("src/timeline/timeline-widget.cpp")
layers = read("src/layers/layer-stack-widget.cpp")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
clone = read("src/editor/title-editor/layout-template-tools.inc")
canvas_pointer = read("src/canvas/canvas-preview/pointer-events.inc")
window = read("src/editor/title-editor/window-session.inc")
cache = read("src/cache/title-cache-policy.h")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert re.search(r"Video\s*=\s*12", model)
assert "layer_type_is_image_like" in model and "LayerType::Video" in model
for field in (
    "linked_media_layer_id", "linked_media_stream", "media_stream_label",
    "video_source", "video_stream_index", "video_in_point", "video_out_point",
    "video_loop", "video_media_duration", "video_frame_rate",
    "video_pixel_width", "video_pixel_height", "video_has_alpha",
):
    assert field in model and f'j["{field}"]' in data
assert "kCurrentDevelopmentVersion = 243" in schema and "case 228:" in schema
assert "title_has_layer_type(title, 12)" in schema

assert "Core/title-video-runtime.cpp" in cmake
assert "libswscale" in cmake and "FXM_HAVE_FFMPEG=1" in cmake
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert "std::vector<AudioStreamInfo> audio_streams" in video_h
assert "FrameRuntime : public QObject" in video_h and "frameReady" in video_h
assert "AVMEDIA_TYPE_VIDEO" in video_cpp and "AVMEDIA_TYPE_AUDIO" in video_cpp
assert "for (unsigned i = 0; i < format->nb_streams; ++i)" in video_cpp
assert "frame_request_for_layer" in video_cpp and "timeline_frame_number" in video_cpp
assert "requested_generation" in video_cpp and "decoded_generation" in video_cpp
assert "requested_frame_number" in video_cpp and "decoded_frame_number" in video_cpp
assert "media-decoded:" in video_cpp and "media-pending:" in video_cpp

assert "render_layer_video" in raster
assert "FrameRuntime::instance().frame_for_layer(" in raster or "FrameRuntime::instance().frame_for_layer(\n        layer, title_time)" in raster
assert "render_layer_image_pixels(cr, title, layer, title_time, frame.image, identity)" in raster
assert "case LayerType::Video:" in raster
assert "LayerType::Video" in cache

assert "for (const auto &stream : video_media.audio_streams)" in commands
assert "audio->parent_id = l->id" in commands
assert "audio->linked_media_layer_id = l->id" in commands
assert "audio->audio_stream_index = stream.stream_index" in commands
assert "audio->audio_sample_rate = stream.sample_rate" in commands
assert "audio->audio_channels = stream.channels" in commands
assert "synchronize_video_audio_streams(*title_, l->id)" in commands
assert "for (const auto &stream : media.audio_streams)" in props
assert "Video replacement must always pass through probe_media()" in props

assert "effective_audio_media" in audio_cpp
assert "result.path = video->video_source" in audio_cpp
assert "result.timeline_in = video->in_time" in audio_cpp
assert "result.media_in = video->video_in_point" in audio_cpp
assert "spec.path = media.path" in audio_cpp and "spec.loop = media.loop" in audio_cpp
assert "synchronize_video_audio_streams(*title)" not in audio_cpp
assert "pending_waveforms_" in audio_cpp and "layer->audio_waveform" in audio_cpp

assert "if (layer->linked_media_stream) return;" in timeline
assert timeline.count("synchronize_video_audio_streams(*title_, layer->id)") >= 3
assert "audio_waveform" in timeline
assert "parent->type == LayerType::Video" in hierarchy
assert "layer->linked_media_stream" in hierarchy
assert "if (!l->linked_media_stream)" in layers
assert 'make_toggle("visibility-normal.svg", "no-visibility.svg", l->visible' in layers
assert 'make_toggle("sound.svg", "sound-mute.svg", !l->audio_muted' in layers
assert "layer.type == LayerType::Video) return {true, true}" in layers

assert "layer->linked_media_stream" in window
assert "linked_video_stream" in window
assert "media_owner_clone" in clone
assert "clone->linked_media_layer_id = media_owner_clone->second" in clone
assert clone.count("synchronize_video_audio_streams(*title_)") >= 2
assert "linked_video_stream" in canvas_pointer
assert "media_owner_clone" in canvas_pointer
assert "synchronize_video_audio_streams(*title_)" in canvas_pointer
assert "protected_layer->linked_media_stream" in data
assert "removing_video" in data and "l->linked_media_layer_id == lid" in data

assert manifest["development_version"] == 243
assert "tests/video_layer_multistream_av_revision_227_contract_test.py" in manifest["areas"]["audio_transport"]["python"]
assert "Development Version 243" in readme
assert changelog.startswith("# v0.8.11-alpha — Development Version 243")
print("Development Version 231 Video layer and multistream A/V contract passed")
