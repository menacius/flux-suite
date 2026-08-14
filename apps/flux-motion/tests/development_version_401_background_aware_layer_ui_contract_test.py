#!/usr/bin/env python3
"""Development Version 401 background-aware layer UI contract."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


assets = read("Editor/title-assets.h")
layout = read("src/editor/title-editor-internal/text-layout-rendering.inc")
stack = read("src/layers/layer-stack-widget.cpp")
timeline = read("src/timeline/timeline-widget.cpp")
build_info = read("src/core/build-info.h")
cmake = read("CMakeLists.txt")

for symbol in [
    "fxm_relative_luminance",
    "fxm_contrast_ratio",
    "fxm_composite_over",
    "fxm_background_aware_foreground",
    "fxm_background_aware_muted_foreground",
]:
    assert symbol in assets, f"missing shared contrast primitive: {symbol}"

assert "palette.color(QPalette::Active, QPalette::WindowText)" in assets
assert "palette.color(QPalette::Active, QPalette::HighlightedText)" in assets
assert "palette.color(QPalette::Active, QPalette::Shadow)" in assets
assert "fxm_semantic_foreground_extremes" in assets
assert "fxm_background_prefers_dark_foreground" in assets

assert "layer_type_icon(LayerType type, const QColor &foreground)" in layout
assert "static QHash<QString, QIcon> cache" in layout
assert "fxm_icon(file_name, foreground)" in layout

for symbol in [
    "LayerRowWidget",
    "effective_background()",
    "fxm_composite_over(tint, base_background_)",
    "refresh_layer_row_contrast_widgets",
    "set_layer_row_button_icon",
    "refresh_layer_dimension_toggle",
    "row_widget->refresh_contrast()",
    "color:palette(text)",
    "color:palette(button-text)",
]:
    assert symbol in stack, f"layer list is missing {symbol}"

assert "layer_type_icon(l->type, type_foreground)" in stack
assert "fxm_background_aware_foreground(" in stack and "effective_layer_color, pal" in stack
assert "QEvent::ApplicationPaletteChange" in stack
assert "if (auto *color_row = dynamic_cast<LayerRowWidget *>(row_widget))" in stack

for symbol in [
    "const QColor strip_foreground",
    "fxm_background_aware_foreground(bar_col, pal)",
    "fxm_background_aware_muted_foreground(bar_col, pal)",
    "layer_type_icon(layer->type, strip_foreground)",
    "p.drawPixmap(icon_rect, strip_icon.pixmap(icon_size))",
    "strip_waveform_foreground",
    "const QColor transition_background",
    "fxm_composite_over(transition_fill, bar_col)",
    "fxm_background_aware_foreground(transition_background, pal)",
]:
    assert symbol in timeline, f"timeline strip is missing {symbol}"

import re
build_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build_info)
cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
assert build_match and int(build_match.group(1)) >= 401
assert cmake_match and int(cmake_match.group(1)) >= 401

print("Development Version 401 background-aware layer UI contract: PASS")
