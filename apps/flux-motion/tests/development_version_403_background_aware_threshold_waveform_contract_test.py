#!/usr/bin/env python3
"""Development Version 403 background-aware threshold/waveform contract."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


assets = read("Editor/title-assets.h")
stack = read("src/layers/layer-stack-widget.cpp")
timeline = read("src/timeline/timeline-widget.cpp")
readme = read("README.md")
build_info = read("src/core/build-info.h")
cmake = read("CMakeLists.txt")

assert "kDarkForegroundLuminanceThreshold = 0.42" in assets
assert "fxm_background_prefers_dark_foreground" in assets
assert "fxm_background_aware_opposite_foreground" in assets
assert "fxm_semantic_foreground_extremes" in assets

# Layer index uses the row palette directly; no stylesheet should freeze its color.
assert "QFont index_font = idx->font();" in stack
assert "idx->setFont(index_font);" in stack
assert 'color:palette(window-text);font-weight:bold;' not in stack

# The 2D state and 3D state are both refreshed against the actual row background.
assert "kLayerRowDimensionToggleProperty" in stack
assert "refresh_layer_dimension_toggle" in stack
assert "row_widget->effective_background()" in stack
assert 'dimension_toggle->setProperty(kLayerRowDimensionToggleProperty, true);' in stack

# Blend-mode items are text-only.
assert "set_layer_row_combo_icons" not in stack
assert 'set_layer_row_combo_icons(mode, "timeline-modes.svg")' not in stack

# Waveforms deliberately use the opposite semantic polarity from strip text/icons.
assert "const QColor strip_waveform_foreground" in timeline
assert "fxm_background_aware_opposite_foreground(bar_col, pal)" in timeline
assert "layer->audio_muted, strip_waveform_foreground" in timeline
assert "false, strip_waveform_foreground" in timeline
assert "strip_waveform_foreground," in timeline

assert "Development Version 403" in readme
assert int(re.search(r'#define FXM_DEVELOPMENT_VERSION "(\d+)"', build_info).group(1)) >= 403
assert int(re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION "(\d+)"\)', cmake).group(1)) >= 403

print("Development Version 403 background-aware threshold/waveform contract: PASS")
