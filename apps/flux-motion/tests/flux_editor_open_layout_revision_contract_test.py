#!/usr/bin/env python3
"""Contract for the Flux Editor opening geometry and active-title controls."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


preview = read("src/canvas/canvas-preview/preview-cache-view.inc")
geometry = read("src/canvas/canvas-preview/geometry-selection.inc")
canvas_header = read("src/canvas/canvas-preview.h")
timeline = read("src/timeline/timeline-widget.cpp")
timeline_header = read("src/timeline/timeline-widget.h")
commands = read("src/editor/title-editor/commands-docks.inc")
toolbar = read("src/editor/title-editor/document-shape-editing.inc")
title_ui = read("src/editor/title-editor/layout-template-tools.inc")

queued_refresh = preview[preview.index("QTimer::singleShot(0, this") :]
assert "if (fit_zoom_active_)" in queued_refresh
assert "fit_canvas(fit_zoom_up_to_100_);" in queued_refresh
assert "if (title_) fit_canvas(true);" in preview
assert "bool fit_zoom_up_to_100 = true;" in canvas_header
assert "bool fit_zoom_up_to_100_ = true;" in canvas_header
assert "std::floor(scale * 100.0)" in geometry

# Rulers reserve equal space on every side. A one-sided reservation moves the
# fitted canvas down and right by half the ruler thickness on every launch.
fit_scale = geometry[geometry.index("double CanvasPreview::fit_scale() const") :
                     geometry.index("double CanvasPreview::view_scale() const")]
centered_origin = geometry[geometry.index("QPointF CanvasPreview::centered_view_origin() const") :
                           geometry.index("QPointF CanvasPreview::view_origin() const")]
assert "ruler_inset * 2.0" in fit_scale
assert centered_origin.count("ruler_inset * 2.0") == 2
assert "ruler_inset + (available_width" in centered_origin
assert "ruler_inset + (available_height" in centered_origin

# The timeline stays fitted while Qt restores docks/splitters, then leaves fit
# mode only after an explicit user zoom or horizontal navigator operation.
assert "bool fit_zoom_active_ = true;" in timeline_header
assert "fit_zoom_active_ = true;" in timeline
resize = timeline[timeline.index("void TimelineWidget::resizeEvent") :]
assert "if (fit_zoom_active_ && title_ && width() > 40)" in resize
assert "fit_zoom_active_ = false;" in timeline
assert "fit_on_next_resize_" not in timeline
assert "fit_on_next_resize_" not in timeline_header

assert "QWidget#editor3DToolbar QToolButton:checked" in commands
assert "gizmo_group->setExclusive(true);" in commands
assert "current->setChecked(true);" in commands

assert "const QString title_text = QString::fromStdString(title_->name);" in title_ui
assert 'add_align_action("align-center-artboard.svg"' not in toolbar

assert "lower_split->setStretchFactor(0, 0);" in commands
assert "lower_split->setStretchFactor(1, 1);" in commands
assert "lower_split->setSizes({layers_->minimumWidth(), 100000});" in commands

print("Flux Editor opening layout/title/toolbar contract: PASS")
