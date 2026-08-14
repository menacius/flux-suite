from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "src/canvas/canvas-preview.h").read_text(encoding="utf-8")
tools = (root / "src/canvas/canvas-preview/editor-3d-tools.inc").read_text(encoding="utf-8")
render = (root / "src/canvas/canvas-preview/keyboard-wheel-events.inc").read_text(encoding="utf-8")
preview = (root / "src/canvas/canvas-preview/preview-cache-view.inc").read_text(encoding="utf-8")
readme = (root / "README.md").read_text(encoding="utf-8")
changelog = (root / "docs/CHANGELOG.md").read_text(encoding="utf-8")
rendering_doc = (root / "docs/RENDERING_AND_CACHE.md").read_text(encoding="utf-8")

assert "indexed and batched state reads" in readme
assert "Development Version 210" in changelog
assert "Interactive transforms and GPU model authority" in rendering_doc
assert "stale render session" in rendering_doc
assert "interactive_transform_pending_" in header
assert "interactive_settle_pending_" in header
assert "full_gpu_model_refresh_pending_" in header
assert "repeated coordinates" in tools
assert "gizmo_drag_.has_last_view" in tools
assert "interactive_transform_pending_ = false;" in tools
assert "interactive_settle_pending_ = changed;" in tools
assert "full_gpu_model_refresh_pending_ = true;" in tools
assert "const bool transform_only_requested" in render
assert "!full_gpu_model_refresh_pending_" in render
assert "const bool priority_edit_frame" in render
assert "interactive_settle_pending_" in render
assert "full_gpu_model_refresh_pending_ || interactive_settle_pending_" in render
assert "interactive_transform_pending_ = false;" in preview
assert "full_gpu_model_refresh_pending_ = true;" in preview
assert render.count("render_interval_ms_ = std::clamp(cost_ms + 1, 1, 34);") == 1
assert "direct_interaction" in render
print("Development Version 210 performance/cache/rendering contract passed")
