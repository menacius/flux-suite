#!/usr/bin/env python3
"""Development Version 412: editor meter, compact chrome and timeline navigator."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
meter = read("src/editor/title-editor/editor-audio-preview.inc")
timeline_header = read("src/timeline/timeline-widget.h")
timeline_source = read("src/timeline/timeline-widget.cpp")
navigator = read("src/timeline/timeline-scroll-zoom-bar.h")
docks = read("src/editor/title-editor/commands-docks.inc")
theme = read("Editor/editor-theme.cpp")
dock_lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")

cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert cmake_version and int(cmake_version.group(1)) >= 412
assert build_version and int(build_version.group(1)) >= 412

assert "pollSessionLevelsIfNeeded();" in meter
assert "session_->audio_levels(&left, &right, &source_update_ns)" in meter
assert "20.0f * std::log10" in meter
assert "last_callback_ns_" in meter
assert "levelsChanged(levels, false);" in meter

assert "horizontal_view_changed" in timeline_header
assert "set_horizontal_view(double start, double duration)" in timeline_header
assert "notify_horizontal_view_changed();" in timeline_source
assert "class TimelineScrollZoomBar final" in navigator
assert "DragMode::ResizeLeft" in navigator
assert "DragMode::ResizeRight" in navigator
assert "timeline_->fit_timeline();" in navigator
assert "new TimelineScrollZoomBar(timeline_, timeline_zoom_bar)" in docks
assert "new QSlider(Qt::Horizontal, timeline_zoom_bar)" not in docks
assert "auto *zoom_out = new QPushButton(timeline_zoom_bar)" not in docks
assert "auto *zoom_in = new QPushButton(timeline_zoom_bar)" not in docks

assert "padding: 4px 9px" in theme
assert "width: 7px" in theme
assert "height: 7px" in theme
assert "fxm_apply_satoshi_ui_font(this);" not in dock_lifecycle

print("Development Version 412 editor audio/timeline navigator contract passed")
