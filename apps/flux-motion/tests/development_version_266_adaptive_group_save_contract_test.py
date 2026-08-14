#!/usr/bin/env python3
"""Development Version 266 contracts: adaptive editor cadence and full-quality save readback."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_adaptive_resolution_uses_low_framerate_editor_cadence():
    canvas = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")

    assert "adaptive_scale_for_cadence = adaptive_preview_scale()" in canvas
    assert "adaptive_low_framerate_editor" in canvas
    assert "kAdaptiveEditorLowFramerateCadenceMs = 100" in canvas
    assert "std::max(base_editing_cadence_ms, kAdaptiveEditorLowFramerateCadenceMs)" in canvas
    assert "playback_frame_pending_ ? 0 : editing_cadence_ms" in canvas


def test_save_and_color_pick_readback_forces_full_quality_adaptive_frame():
    preview = read("src/canvas/canvas-preview/preview-cache-view.inc")
    save = read("src/editor/title-editor/playback-cache-preferences.inc")

    assert "restore_adaptive_draft" in preview
    assert "frame_image_preview_scale_ < 0.999" in preview
    assert "title_gpu_render_session_set_preview_quality(gpu_render_session_,\n                                                     1.0, false);" in preview
    assert "title_gpu_render_session_readback(gpu_render_session_)" in preview
    assert "title_gpu_render_session_set_preview_quality(gpu_render_session_,\n                                                     restore_scale, true);" in preview
    assert "image.size() != QSize(title_->width, title_->height)" in preview
    assert "Qt::SmoothTransformation" in preview
    assert "canvas_->current_rendered_frame()" in save


if __name__ == "__main__":
    test_adaptive_resolution_uses_low_framerate_editor_cadence()
    test_save_and_color_pick_readback_forces_full_quality_adaptive_frame()
    print("development version 266 adaptive/group-save contract passed")
