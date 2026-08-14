from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
properties = read("src/editor/properties-panel/popup-state.inc")
window = read("src/editor/title-editor/window-session.inc")
effects_h = read("src/effects/effects-panel.h")
effects_cpp = read("src/effects/effects-panel.cpp")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 354
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 354

# Transform and Orientation finish at the fixed keyframe-controls column. No
# empty elastic column remains after the controls to make them look centered.
transform = properties[properties.index("transform_grid_ = new QGridLayout()") :
                       properties.index("row_shape_scale_options_ = new QWidget")]
assert transform.count("setColumnStretch(5, 0);") == 2
assert "setColumnStretch(6, 1);" not in transform
assert "row, 5, Qt::AlignRight | Qt::AlignVCenter" in transform
assert "2, 5, Qt::AlignRight | Qt::AlignVCenter" in transform
assert "0, 5, Qt::AlignRight | Qt::AlignVCenter" in transform

# Dock tab bars and every editor-owned QTabWidget use the upper edge.
assert "setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);" in window
assert "for (QTabWidget *tabs : findChildren<QTabWidget *>())" in window
assert "tabs->setTabPosition(QTabWidget::North);" in window

# The complete Effect Stack enable/disable control is the shared theme-aware
# toggle switch, not a checkable visibility icon toolbutton.
assert "class FxmSwitch;" in effects_h
assert "FxmSwitch *btn_stack_enabled_ = nullptr;" in effects_h
assert 'btn_stack_enabled_ = new FxmSwitch(tr("Effect Stack"), button_bar);' in effects_cpp
assert "btn_stack_enabled_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);" in effects_cpp
assert "button_layout->addWidget(btn_stack_enabled_);" in effects_cpp
assert "connect(btn_stack_enabled_, &QCheckBox::toggled" in effects_cpp
assert 'btn_stack_enabled_ = add_button("visibility.svg"' not in effects_cpp
assert "btn_stack_enabled_->setCheckable(true);" not in effects_cpp

print("Development Version 354 right keyframes/top tabs/Effect Stack switch contract: PASS")
