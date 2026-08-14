#!/usr/bin/env python3
"""Development Version 413: compact navigator placement and painted keyframes."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
docks = read("src/editor/title-editor/commands-docks.inc")
navigator = read("src/timeline/timeline-scroll-zoom-bar.h")
layers = read("src/layers/layer-stack-widget.cpp")
dock_lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 413
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 413

assert "\n    zoom_layout->addStretch(1);" not in docks
assert "zoom_layout->addWidget(timeline_scroll_zoom, 2);" in docks
assert "setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);" in navigator
assert "setMaximumWidth(720);" not in navigator

assert "class KeyframeDiamondButton final" in layers
assert "setActiveKeyframe(bool active)" in layers
assert "painter.drawPath(diamond);" in layers
assert "new KeyframeDiamondButton(prop_widget)" in layers
assert 'QStringLiteral("◆")' not in layers
assert 'QStringLiteral("◇")' not in layers
assert "setFixedSize(22, 22);" in layers

assert "fxm_apply_satoshi_ui_font(this);" not in dock_lifecycle

print("Development Version 413 timeline/keyframe indicator contract passed")
