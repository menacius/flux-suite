from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


canvas_h = read("src/canvas/canvas-preview.h")
preview = read("src/canvas/canvas-preview/preview-cache-view.inc")
paint = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
properties = read("src/editor/properties-panel/property-synchronization.inc")
construction = read("src/editor/properties-panel/construction-transform-character.inc")
controls = read("Editor/fxm-modern-controls.cpp")

# Every playhead change republishes the editor-only texture. This covers
# animated light properties and other helpers outside the selected layer.
playhead = preview[preview.index("void CanvasPreview::set_playhead") :]
playhead = playhead[: playhead.index("void CanvasPreview::set_display_refresh_rate")]
assert "playhead_ = t;" in playhead
assert "invalidate_canvas_overlay_caches();" in playhead

# The master helper switch defaults on, owns a Canvas API and is positioned
# between the independent Safe control and Adaptive Rendering.
assert "bool overlay_graphics_visible_ = true;" in canvas_h
assert "void set_overlay_graphics_visible(bool visible);" in canvas_h
assert "if (!overlay_graphics_visible_)\n        return;" in paint
assert "if (overlay_graphics_visible_ && inline_text_editor_ &&" in paint
safe_add = commands.index("canvas_zoom_layout->addWidget(safe_guides);")
overlay_add = commands.index("canvas_zoom_layout->addWidget(overlay_graphics);")
adaptive_add = commands.index("canvas_zoom_layout->addWidget(adaptive_rendering);")
assert safe_add < overlay_add < adaptive_add
assert "overlay_graphics->setChecked(canvas_->overlay_graphics_visible());" in commands

# No selection means no inspector content—not merely reset values in visible
# sections—and the constructor starts in the same empty state.
assert "content->setVisible(static_cast<bool>(layer_));" in properties
assert "setWidget(inner);\n    inner->setVisible(false);" in construction

# Styled combo boxes receive a theme-aware painted caret in both shared field
# styling paths, avoiding host-style-dependent missing arrows.
assert "class FxmComboCaretOverlay final : public QWidget" in controls
assert controls.count("ensure_combo_caret(combo);") >= 2
assert "QPalette::Disabled" in controls

print("Development Version 410 editor overlay controls contract: PASS")
