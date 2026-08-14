from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
modern = read("Editor/fxm-modern-controls.cpp")
properties = read("src/editor/properties-panel/popup-state.inc")
shape = read("src/editor/properties-panel/construction-gradient-image-signals.inc")
image = read("src/editor/properties-panel/construction-transform-character.inc")
color_popup = read("src/editor/properties-panel/construction-type-live-shape.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 351
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 351

# Carets are visibly smaller without changing the shared diamond metric.
assert "constexpr int kKeyframeButtonExtent = 18;" in modern
assert "constexpr int kKeyframeCaretWidth = 10;" in modern
assert "constexpr int kKeyframeCaretHeight = 14;" in modern
assert "button->setFixedSize(kKeyframeCaretWidth, kKeyframeCaretHeight);" in modern

# Every normal field grows like the Camera inspector instead of retaining a
# fixed-width legacy textbox.
normalizer = modern[modern.index("void fxm_apply_transform_panel_widget_style") :]
for widget_branch in (
    "qobject_cast<QAbstractSpinBox *>(widget)",
    "qobject_cast<QComboBox *>(widget)",
    "qobject_cast<QLineEdit *>(widget)",
):
    branch = normalizer[normalizer.index(widget_branch):]
    assert "setMaximumWidth(QWIDGETSIZE_MAX);" in branch[:1200]
assert "QSizePolicy::Expanding, QSizePolicy::Fixed" in modern

# The Properties panel uses the same exact content/form inset and spacing as
# the 3D Camera form.
for token in (
    "constexpr int kPanelContentLeft = 5;",
    "constexpr int kPanelContentTop = 4;",
    "constexpr int kPanelContentRight = 5;",
    "constexpr int kPanelContentBottom = 5;",
    "form->setContentsMargins(5, 4, 5, 5);",
    "form->setHorizontalSpacing(4);",
    "form->setVerticalSpacing(2);",
):
    assert token in modern

# XYZ/WH labels sit outside filled QPalette-backed spinboxes, matching the
# camera rows rather than the former dark composite boxes.
assert "spin->setStyleSheet(control_style);" in properties
assert "OBSTitlesTransformNumericField{background:transparent;border:none;}" in properties
assert "field_layout->addWidget(spin, 1);" in properties
assert properties.count("setColumnMinimumWidth(1, 0)") >= 1
assert "text->setAlignment(Qt::AlignRight | Qt::AlignVCenter);" in properties

for source in (shape, image):
    assert "OBSTitlesShapeNumericField{background:transparent;border:none;}" in source
    assert "field->setMinimumWidth(0);" in source
    assert "field_layout->addWidget(spin, 1);" in source

# All ordinary Properties accents remain theme-derived.
properties_sources = "\n".join((properties, shape, image, color_popup))
assert "#f0a000" not in properties_sources
assert "#ff5c5c" not in properties_sources
assert "palette(highlight)" in properties_sources
assert "highlight_name" in properties_sources

print("Development Version 351 camera-style Properties/carets contract: PASS")
