"""Playback pipeline timing and adaptive-resolution regression contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


geometry = read("src/canvas/canvas-preview/geometry-selection.inc")
preview = read("src/canvas/canvas-preview/preview-cache-view.inc")
header = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
diagnostics = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")
frame = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
status = read("src/editor/title-editor/editor-audio-preview.inc")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")

assert "transport_playback_active_ ? adaptive_auto_scale_ : 0.5" in geometry
assert "update_adaptive_playback_quality(" in preview
assert "effective_ms > frame_budget_ms * 1.10" in preview
assert "adaptive_over_budget_frames_ >= 4" in preview
assert "adaptive_under_budget_frames_ >= 90" in preview
assert "static constexpr double kScales[] = {1.0, 0.75, 0.5, 0.375, 0.25}" in preview
assert "const double adaptive_scale = adaptive_preview_scale();" in frame
assert "destination_composite\n                ? 1.0" not in frame

for token in (
    "requested_output_width",
    "requested_output_height",
    "render_target_width",
    "render_target_height",
    "preview_quality_scale",
):
    assert token in header
    assert token in diagnostics
for token in (
    "graphicsWaitMs=",
    "renderSubmitMs=",
    "previewUploadMs=",
    "presentWaitMs=",
    "readbackMs=0",
    "totalMs=",
):
    assert token in canvas
assert "Render:" in status
assert "Total frame:" in status
assert "actual target:" in status

assert "draft_shadow_scale" in gpu
assert "session->preview_quality_scale" in gpu
assert "full_quality_face_size" in gpu
assert "full_quality_face_size) *" in gpu

assert "if (!playback_frame)\n        invalidate_canvas_overlay_caches();" in preview
assert "invalidate_canvas_overlay_caches();\n        if (restore_full_quality)" in preview

print("Playback pipeline performance v0.8.19 contract passed")
