#!/usr/bin/env python3
"""OBS-hosted titles inherit and lock the OBS project document format."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


host = read("../../plugins/obs/flux-motion/src/obs-editor-host.cpp")
main = read("Editor/main.cpp")
runtime = read("Editor/standalone-obs-graphics-runtime.cpp")
editor_header = read("src/editor/title-editor.h")
workflow = read("src/editor/title-editor/playback-cache-preferences.inc")
panel_header = read("src/editor/title-properties-panel.h")
panel = read("src/editor/title-properties-panel.cpp")
construction = read("src/editor/title-editor/commands-docks.inc")

# Capture the base canvas and exact rational cadence in the OBS process.
for token in (
    "obs_get_video_info(&host_video)",
    "host_video.base_width",
    "host_video.base_height",
    "host_video.fps_num",
    "host_video.fps_den",
    'QStringLiteral("--host-width")',
    'QStringLiteral("--host-height")',
    'QStringLiteral("--host-fps-num")',
    'QStringLiteral("--host-fps-den")',
):
    assert token in host, token

# Parse the values before initializing the private libobs runtime/editor.
for token in (
    "host_format_supplied",
    "host_frame_rate",
    "StandaloneFrameRateProvider frame_rate(",
    "StandaloneObsGraphicsRuntime>(",
    "EditorExecutionContext::obsPlugin(",
):
    assert token in main, token
assert "video.base_width" in runtime and "video.fps_num" in runtime

# Hosted documents use those values, and the provider rejects title overrides.
for token in (
    "host_canvas_width",
    "host_canvas_height",
    "host_frame_rate",
):
    assert token in editor_header, token
assert "title_->width = std::clamp(" in workflow
assert "title_->height = std::clamp(" in workflow
assert "title_->frame_rate = std::clamp(" in workflow
assert "accept_document_rate_" in read("Editor/standalone-editor-host.h")

# Both fields and their QFormLayout labels disappear in OBS mode.
assert "set_document_format_editable(bool editable)" in panel_header
assert "form->labelForField(field)" in panel
assert "set_row_visible(resolution_row_)" in panel
assert "set_row_visible(spn_frame_rate_)" in panel
assert "EditorHostKind::ObsPlugin" in construction

print("OBS-hosted document format contract passed")
