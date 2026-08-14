"""Development Version 408 preview FPS, adaptive render, and title format contract."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
model = read("Core/title-data.h")
serialization = read("src/core/title-data.cpp")
properties_h = read("src/editor/title-properties-panel.h")
properties_cpp = read("src/editor/title-properties-panel.cpp")
editor_connections = read("src/editor/title-editor/commands-docks.inc")
editor_open = read("src/editor/title-editor/playback-cache-preferences.inc")
frame_rate_h = read("../../packages/flux-common/Shared/frame-rate-provider.h")
standalone_host = read("Editor/standalone-editor-host.h")
canvas_paint = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
adaptive = read("src/canvas/canvas-preview/geometry-selection.inc")
software = read("Editor/software-title-render-session.cpp")

assert "set(OBS_FXM_DEVELOPMENT_VERSION" in cmake

# The authored format belongs to the title and survives save/load.
assert "double      frame_rate  = 30.0" in model
assert 'jt["frame_rate"] = t.frame_rate;' in serialization
assert 'json_double(jt, "frame_rate", 30.0)' in serialization
assert "spn_width_" in properties_h and "spn_height_" in properties_h
assert "spn_frame_rate_" in properties_h
assert 'fxm_tr("OBSTitles.ResolutionLabel")' in properties_cpp
assert 'fxm_tr("OBSTitles.FrameRateLabel")' in properties_cpp
assert "emit document_format_changed(true);" in properties_cpp
assert "emit document_format_changed(false);" in properties_cpp

# Standalone consumers share the opened title's cadence without overriding the
# OBS host provider, whose default document-rate setter intentionally does none.
assert "set_document_frame_rate(double) const noexcept" in frame_rate_h
assert "std::atomic<double> frame_rate_{30.0}" in standalone_host
assert "fxm::set_document_frame_rate(title_->frame_rate);" in editor_open
assert "reset_playback_timer_cadence();" in editor_connections

# A software canvas paint is an actual present and participates in the same FPS
# diagnostic window as the GPU swap-chain path.
assert "const bool playback_present = transport_playback_active_" in canvas_paint
assert "record_live_playback_present();" in canvas_paint

# Reduced playback quality must shrink the render target and layer shading work,
# not render a full title and only scale the completed image afterward.
assert "!transport_playback_active_" in adaptive
assert "qRound(title.width * preview_scale)" in software
assert "qRound(raster.image.width() * preview_scale)" in software
assert "render_software_title(\n        title, time, &session->raster_cache, transform_only_update,\n        session->scale);" in software

print("Development Version 408 preview FPS and title format contract passed")
