"""Development Version 409 unified GPU and independent render contract."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
main = read("Editor/main.cpp")
runtime = read("Editor/standalone-obs-graphics-runtime.cpp")
launcher = read("../../plugins/obs/flux-motion/src/obs-editor-host.cpp")
header = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
timing = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
canvas = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
video = read("Core/title-video-runtime.cpp")

development_match = re.search(
    r'set\(OBS_FXM_DEVELOPMENT_VERSION "(\d+)"\)', cmake
)
assert development_match and int(development_match.group(1)) >= 409

# The standalone target owns libobs and the shared production compositor; the
# old software session must not be part of this executable.
assert "${OBS_FXM_OBS_PLUGIN_RENDERING_SOURCES}" in cmake
assert "${OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES}" in cmake
assert "FXM_CANVAS_WITH_OBS=1" in cmake
assert "OBS::libobs" in cmake and "OBS::obs-frontend-api" in cmake
editor_target = cmake[cmake.index("add_executable(flux-motion-editor"):]
assert "Editor/software-title-render-session.cpp" not in editor_target
assert "obs_reset_video(&video)" in runtime
assert 'QStringLiteral("libobs-d3d11.dll")' in runtime
assert "obs_title_preview_renderer()" in main

# Smoke coverage performs actual GPU readbacks at two physical resolutions.
assert "render_extent(320, 180, 1.0 / 24.0" in main
assert "render_extent(640, 360, 1.0 / 60.0" in main
assert "title_gpu_render_session_readback(session)" in main

# Logical title coordinates and physical output extent are separate session
# inputs, including resolution-aware raster density and temporal sampling.
for token in (
    "struct TitleGpuRenderRequest",
    "output_width",
    "output_height",
    "sample_duration_seconds",
    "title_gpu_render_session_set_render_request",
):
    assert token in header
assert "requested_output_width" in session
assert "requested_output_height" in session
assert "gpu_session_render_scale_x" in session
assert "gpu_session_render_scale_y" in session
assert "gpu_session_raster_scale" in session
assert "ScopedSourceFrameDuration" in timing
assert "obs_get_video_info(&ovi)" not in timing
assert "session->sample_duration_seconds" in playback

# Editor/title cadence and OBS live cadence are explicit adapters to the same
# compositor, rather than hidden globals inside rendering code.
assert "render_title->frame_rate" in canvas
assert "obs_output_sample_duration()" in source

# Equal title timestamps resolve video by continuous media time/media FPS. The
# project frame number remains diagnostic only and is absent from cache identity.
assert "media_sample_time * request.media_frame_rate" in video
assert "video-frame-map=media:" in video
assert "video-frame-map=timeline:" not in video
assert "request.timeline_frame_number) * request.media_frame_rate" not in video

# Detached Editor launches can resolve libobs' transitive runtime dependencies.
assert 'environment.insert(\n        QStringLiteral("PATH")' in launcher
assert 'QStringLiteral("--obs-bin-root")' in launcher

print("Development Version 409 unified GPU independent render contract passed")
