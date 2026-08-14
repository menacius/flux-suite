#!/usr/bin/env python3
"""Development Version 400 layer-type icon/color appearance contract."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


icons = [
    "asset-title.svg",
    "layer-type-adjustment.svg",
    "layer-type-audio.svg",
    "layer-type-camera.svg",
    "layer-type-clock.svg",
    "layer-type-color-solid.svg",
    "layer-type-empty.svg",
    "layer-type-image.svg",
    "layer-type-light.svg",
    "layer-type-shape.svg",
    "layer-type-text.svg",
    "layer-type-ticker.svg",
    "layer-type-video.svg",
]
for name in icons:
    svg = read(f"data/icons/{name}")
    assert "currentColor" in svg, f"{name} is not OBS-theme tintable"
    assert "#4a5565" not in svg.lower(), f"{name} retains a fixed source color"

stack = read("src/layers/layer-stack-widget.cpp")
layout = read("src/editor/title-editor-internal/text-layout-rendering.inc")
preferences_h = read("src/core/title-preferences.h")
preferences_cpp = read("src/core/title-preferences.cpp")
appearance = read("src/editor/title-editor/signal-handlers.inc")
locale = read("data/locale/en-US.ini")

for name in icons:
    assert name in stack or name in layout, f"{name} is not wired into editor UI"

for name in [
    "layer-type-text.svg",
    "layer-type-clock.svg",
    "layer-type-ticker.svg",
    "layer-type-shape.svg",
    "layer-type-image.svg",
    "layer-type-video.svg",
    "layer-type-audio.svg",
    "layer-type-empty.svg",
    "layer-type-adjustment.svg",
    "layer-type-color-solid.svg",
    "layer-type-camera.svg",
    "layer-type-light.svg",
]:
    assert f'"{name}"' in stack, f"add menu is missing {name}"

assert "layer_type_icon(l->type)" in stack
assert "constexpr int kLayerRowIconExtent = 18;" in stack
assert "QSize(kLayerRowIconExtent, kLayerRowIconExtent)" in stack
assert "refresh_themed_menu_icons" in stack
assert "QMenu::aboutToShow" in stack
assert "QEvent::ApplicationPaletteChange" in stack
assert "layer-type-camera.svg" in stack and "layer-type-light.svg" in stack

roles = [
    "VideoLayer",
    "AudioLayer",
    "EmptyLayer",
    "AdjustmentLayer",
    "ColorSolidLayer",
    "CameraLayer",
    "LightLayer",
]
for role in roles:
    assert role in preferences_h
    assert f"TimelineColorRole::{role}" in preferences_cpp
    assert f"TimelineColorRole::{role}" in appearance

layer_cases = {
    "Video": "VideoLayer",
    "Audio": "AudioLayer",
    "Empty": "EmptyLayer",
    "Adjustment": "AdjustmentLayer",
    "ColorSolid": "ColorSolidLayer",
    "Light": "LightLayer",
}
for layer_type, role in layer_cases.items():
    assert f"case LayerType::{layer_type}:" in layout
    assert f"TimelineColorRole::{role}" in layout

for key in [
    "VideoLayers",
    "AudioLayers",
    "EmptyLayers",
    "AdjustmentLayers",
    "ColorSolidLayers",
    "CameraLayers",
    "LightLayers",
]:
    assert f"OBSTitles.{key}" in locale

build_info = read("src/core/build-info.h")
cmake = read("CMakeLists.txt")
import re
build_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build_info)
cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
assert build_match and int(build_match.group(1)) >= 400
assert cmake_match and int(cmake_match.group(1)) >= 400

print("Development Version 400 layer-type icon/color contract: PASS")
