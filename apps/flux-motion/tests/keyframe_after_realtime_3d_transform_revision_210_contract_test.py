from pathlib import Path

root = Path(__file__).resolve().parents[1]
tools = (root / "src/canvas/canvas-preview/editor-3d-tools.inc").read_text(encoding="utf-8")
render = (root / "src/canvas/canvas-preview/keyboard-wheel-events.inc").read_text(encoding="utf-8")
preview = (root / "src/canvas/canvas-preview/preview-cache-view.inc").read_text(encoding="utf-8")

finish_start = tools.index("bool CanvasPreview::finish_3d_gizmo_drag")
finish_end = tools.index("void CanvasPreview::draw_editor_3d_view_overlay", finish_start)
finish = tools[finish_start:finish_end]

# Release must revoke transform-only state before publishing the model change.
assert finish.index("interactive_transform_pending_ = false;") < finish.index("emit layer_geometry_changed();")
assert finish.index("full_gpu_model_refresh_pending_ = true;") < finish.index("emit layer_geometry_changed();")
assert "interactive_settle_pending_ = changed;" in finish
assert "dirty_ = true;" in finish

refresh_start = preview.index("void CanvasPreview::refresh_preview()")
refresh_end = preview.index("void CanvasPreview::clear_rendered_frame()", refresh_start)
refresh = preview[refresh_start:refresh_end]
assert refresh.index("interactive_transform_pending_ = false;") < refresh.index("gpu_model_dirty_ = true;")
assert refresh.index("full_gpu_model_refresh_pending_ = true;") < refresh.index("gpu_model_dirty_ = true;")

# A pending keyframe/model refresh blocks the transform-only session shortcut.
assert "const bool transform_only_update = transform_only_requested &&" in render
assert "!full_gpu_model_refresh_pending_;" in render
# Settle gets realtime cadence but is deliberately absent from transform_only_requested.
transform_block = render[render.index("const bool transform_only_requested"):render.index("bool submitted_cached_frame")]
assert "interactive_settle_pending_" not in transform_block
assert "interactive_settle_pending_" in render[render.index("const bool priority_edit_frame"):]
print("Development Version 210 keyframe-after-transform regression contract passed")
