#!/usr/bin/env python3
"""Source contract for the Linux native canvas and OpenGL shader path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

runtime = (ROOT / "Editor" / "standalone-obs-graphics-runtime.cpp").read_text(
    encoding="utf-8"
)
canvas = (ROOT / "src" / "canvas" / "canvas-preview" /
          "preview-cache-view.inc").read_text(encoding="utf-8")
paint = (ROOT / "src" / "canvas" / "canvas-preview" /
         "keyboard-wheel-events.inc").read_text(encoding="utf-8")
renderer = (ROOT / "Renderer" / "main.cpp").read_text(encoding="utf-8")
shader = (ROOT.parents[1] / "packages" / "flux-common" / "Shared" /
          "rendering-engine" / "title-source" /
          "gpu-effects-transitions.inc").read_text(encoding="utf-8")
shader_queue = (ROOT.parents[1] / "packages" / "flux-common" / "Shared" /
                "rendering-engine" / "title-source" /
                "gpu-masks-groups-cache.inc").read_text(encoding="utf-8")

assert "obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL)" in runtime
assert "obs_set_nix_platform(OBS_NIX_PLATFORM_WAYLAND)" in runtime
assert "obs_set_nix_platform_display(native->display())" in runtime
assert "constexpr std::uint32_t kBootstrapExtent = 4" in runtime

assert "canvas_native_presentation_supported()" in canvas
assert "!force_present_pending_ && !playback_present_pending_" in canvas
assert "return QWidget::paintEngine();" in canvas
assert "native_presentation_supported" in paint

assert "shader_compile_failed_ids" in shader_queue
assert "if (queued)\n        mark_shader_compile_pending" in shader_queue
assert "\n    clip(" not in shader
assert "discard;" in shader
assert "(int)(" not in shader
assert "(float)" not in shader

assert "published_requested_frame" in renderer
assert "diagnostics.has_published_frame" in renderer
assert "!diagnostics.last_draw_deferred" in renderer
assert "!compiling" in renderer

print("Linux OpenGL canvas runtime contract passed")
