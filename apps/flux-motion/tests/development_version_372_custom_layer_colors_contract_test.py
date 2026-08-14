from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
model = read("Core/layer-model.h")
data = read("src/core/title-data.cpp")
colors = read("src/editor/title-editor-internal/text-layout-rendering.inc")
stack_h = read("src/layers/layer-stack-widget.h")
stack = read("src/layers/layer-stack-widget.cpp")
connections = read("src/editor/title-editor/commands-docks.inc")
locale = read("data/locale/en-US.ini")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 372
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 372
assert manifest["development_version"] >= 372
assert (
    "tests/development_version_372_custom_layer_colors_contract_test.py"
    in manifest["areas"]["editor_gui"]["python"]
)

# The custom color is authored per layer, survives save/load/copy/undo through
# the canonical Layer model and is excluded from raster cache identity.
for token in (
    "bool        custom_ui_color_enabled = false;",
    "uint32_t    custom_ui_color = 0xFF4C6EF5u;",
):
    assert token in model
for token in (
    'j["custom_ui_color_enabled"] = l.custom_ui_color_enabled;',
    'j["custom_ui_color"] = l.custom_ui_color;',
    'j, "custom_ui_color_enabled", false',
    'j, "custom_ui_color", (uint32_t)0xFF4C6EF5u',
    '"group_collapsed", "custom_ui_color_enabled", "custom_ui_color",',
):
    assert token in data
assert "if (layer.custom_ui_color_enabled)" in colors
assert "QColor::fromRgba(" in colors
assert "layer.custom_ui_color" in colors
assert "return layer_type_color(layer.type);" in colors
assert "set_timeline_color" not in stack
assert "set_timeline_color" not in connections

# Clicking the restored 370-style type icon opens a 4x4 palette of exactly 16
# swatches, plus the Custom Color dialog and a safe return-to-default action.
assert "class LayerColorIcon final : public QLabel" in stack
assert "static const std::array<QColor, 16> &layer_ui_color_palette()" in stack
assert "grid->addWidget(swatch, index / 4, index % 4);" in stack
assert 'swatch->setFixedSize(24, 24);' in stack
assert 'menu->addSection(fxm_tr("OBSTitles.LayerColorPalette"));' in stack
assert 'fxm_tr("OBSTitles.CustomLayerColor")' in stack
assert "QColorDialog::getColor(" in stack
assert "QColorDialog::DontUseNativeDialog" in stack
assert 'fxm_tr("OBSTitles.DefaultLayerColor")' in stack
assert "emit layer_ui_color_changed(" in stack
# The popup is parented outside the colored type icon and explicitly uses the
# application/OBS menu palette, including the embedded swatch-grid surface.
for token in (
    "auto *menu = new QMenu(list_);",
    "const QPalette menu_palette = qApp->palette();",
    '"QMenu{color:%1;background:%2;border:1px solid %3;}"',
    '"QWidget#layerColorPaletteWidget{background:%2;color:%1;}"',
    "menu_palette.color(QPalette::Base)",
):
    assert token in stack
palette_body = stack[
    stack.index("static const std::array<QColor, 16> &layer_ui_color_palette()"):
    stack.index("static QString layer_ui_color_swatch_style")
]
assert len(re.findall(r'QColor\(QStringLiteral\("#[0-9a-f]{6}"\)\)', palette_body)) == 16

# Restore the exact Dev370 visual treatment for the type chip and checked FX.
for token in (
    '"background:%1;border:1px solid %2;color:%3;font-weight:bold;"',
    '"QToolButton:checked{background:%3;color:%4;}"',
):
    assert token in stack

# The signal mutates only the addressed Layer instance, persists through the
# ordinary title/graphic edit/undo path and refreshes every color consumer.
assert "void layer_ui_color_changed(" in stack_h
for token in (
    "&LayerStack::layer_ui_color_changed",
    "layer->custom_ui_color_enabled = enabled;",
    "layer->custom_ui_color = opaque_argb;",
    "force_next_title_visual_update();",
    "on_title_modified(true);",
    "QTimer::singleShot(0, layers_",
    "layers_->refresh();",
    "timeline_->set_title(title_);",
    "canvas_->update();",
):
    assert token in connections

for key in (
    "OBSTitles.LayerColorTooltip=",
    "OBSTitles.LayerColorPalette=",
    "OBSTitles.LayerColorSwatchAccessibleFormat=",
    "OBSTitles.DefaultLayerColor=",
    "OBSTitles.CustomLayerColor=",
    "OBSTitles.CustomLayerColorDialog=",
):
    assert key in locale

print("Development Version 372 custom layer colors contract: PASS")
