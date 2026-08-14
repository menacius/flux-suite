from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
stack = read("src/layers/layer-stack-widget.cpp")
empty_tools = read("src/canvas/canvas-preview/editor-3d-tools.inc")
locale = read("data/locale/en-US.ini")
manifest = json.loads(read("tests/test-suite-manifest.json"))

cmake_version = int(re.search(
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_version >= 368
assert build_version >= 368
assert manifest["development_version"] >= 368
assert (
    "tests/development_version_368_layer_list_alignment_drag_empty_size_contract_test.py"
    in manifest["areas"]["editor_gui"]["python"]
)

# A permanent grip column begins both the header and every main layer row. The
# grip initiates the list's existing InternalMove drag instead of duplicating
# the hierarchy/reordering logic.
for token in (
    "constexpr int kLayerDragHandleWidth = 20;",
    "class LayerListWidget final : public QListWidget",
    "class LayerRowDragHandle final : public QToolButton",
    "void start_row_drag(QListWidgetItem *item)",
    "startDrag(Qt::MoveAction);",
    'setObjectName(QStringLiteral("layerRowDragHandle"))',
    'setAccessibleName(fxm_tr("OBSTitles.DragLayerTooltip"))',
    'setToolTip(fxm_tr("OBSTitles.DragLayerTooltip"))',
    "add_header(\"\", kLayerDragHandleWidth);",
    "auto *drag_handle = new LayerRowDragHandle(",
    "hl->addWidget(drag_handle);",
):
    assert token in stack
assert 'OBSTitles.DragLayerTooltip="Drag between layers to reorder;' in locale

# Header and row authoring order agree: Parent follows both matte controls and
# immediately precedes the dimension switch. Optional controls remain inside
# fixed-width cells, so Audio/Light/Empty rows cannot shift later columns.
header_tokens = (
    'add_header(fxm_tr("OBSTitles.ModeHeader")',
    'add_header(fxm_tr("OBSTitles.MatteSourceHeader")',
    'add_header_icon("matte-alpha.svg"',
    'add_header_icon("matte-normal.svg"',
    'add_header(fxm_tr("OBSTitles.ParentHeader")',
    'add_header(QStringLiteral("2D/3D")',
)
header_positions = [stack.index(token) for token in header_tokens]
assert header_positions == sorted(header_positions)

row_tokens = (
    "QComboBox *mode = new CompactLayerComboBox",
    "QComboBox *matte = new CompactLayerComboBox",
    "QToolButton *matte_type = new LayerRowIconButton",
    "QToolButton *matte_invert = new LayerRowIconButton",
    "QComboBox *parent = new CompactLayerComboBox",
    "const bool supports_3d =",
)
row_positions = [stack.index(token) for token in row_tokens]
assert row_positions == sorted(row_positions)
for token in (
    "add_fixed_control_column(mode, kLayerModeWidth",
    "add_fixed_control_column(matte, kLayerMaskWidth",
    "add_fixed_control_column(matte_type, kLayerMatteControlWidth",
    "add_fixed_control_column(matte_invert, kLayerMatteControlWidth",
    "add_fixed_control_column(parent, kLayerParentWidth",
):
    assert token in stack

# 2D Empty objects have a 56 px full cross span and a larger center mark; the
# 3D representation intentionally retains its original 18 px axes.
assert "const double axis_pixels = is_3d ? 18.0 : 28.0;" in empty_tools
assert "const double center_size = is_3d ? 5.0 : 7.0;" in empty_tools

print("Development Version 368 layer-list fixes contract: PASS")
