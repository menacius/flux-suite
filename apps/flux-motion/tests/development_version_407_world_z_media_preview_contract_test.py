"""Development Version 407 world-Z and standalone A/V preview contract."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


transform = read("src/rendering/layer-transform-3d.cpp")
software = read("Editor/software-title-render-session.cpp")
host = read("Editor/standalone-editor-host.cpp")
interface = read("../../packages/flux-common/Shared/editor-host-interfaces.h")
preferences_h = read("src/core/title-preferences.h")
preferences_cpp = read("src/core/title-preferences.cpp")
preferences_ui = read("src/editor/title-editor/signal-handlers.inc")
cmake = read("CMakeLists.txt")

# Automatic Z is evaluated across all compatible siblings for a camera, not
# reset whenever an unrelated authored row interrupts the layer list.
assert "std::map<std::string, CameraDepthSet> by_camera" in transform
assert "evaluate(title, *left, title_time).camera_depth" in transform
assert "set.positions.push_back(slot)" in transform
assert "farthest surface first" in transform

# Standalone Video uses the same asynchronous FFmpeg-backed frame runtime as
# editor/live output and bypasses the transform-neutral raster cache while a
# decoded frame can be published for an unchanged timestamp.
assert "fxm::video::FrameRuntime::instance().frame_for_layer" in software
assert "fxm::video::VideoDecodeClient::Editor" in software
assert "if (layer.type == LayerType::Video)" in software
assert "return software_layer_raster(layer, bounds, local_time, title_time);" in software

# The standalone host must return a concrete audio preview session and route
# it through Qt's selected physical output device.
assert "class StandaloneAudioPreviewSession" in host
assert "std::make_unique<StandaloneAudioPreviewSession>(title_id)" in host
assert "std::make_unique<QMediaPlayer>()" in host
assert "std::make_unique<QAudioOutput>()" in host
assert "selected_preview_audio_device()" in host
assert "setActiveAudioTrack" in host
assert "evaluate_video_time_remap" in host

# Device discovery remains host-facing while the persisted choice belongs to
# shared Editor preferences.
assert "AudioOutputDeviceInfo" in interface
assert "audio_output_devices() const" in interface
assert "preview_audio_device_id()" in preferences_h
assert "set_preview_audio_device_id" in preferences_cpp
assert "preview_audio_device" in preferences_ui
assert "release_editor_audio_preview();" in preferences_ui

# Build and development deployments include both the Qt Multimedia library and
# its Windows media-service plugin.
assert "COMPONENTS Multimedia REQUIRED" in cmake
assert "OBS_FXM_QT_MULTIMEDIA_TARGET" in cmake
assert 'plugins/multimedia' in cmake

print("Development Version 407 world-Z and standalone media-preview contract passed")
