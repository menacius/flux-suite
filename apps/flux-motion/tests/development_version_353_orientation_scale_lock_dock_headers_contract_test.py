from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
properties = read("src/editor/properties-panel/popup-state.inc")
refresh = read("src/editor/properties-panel/selection-refresh.inc")
panels = read("src/editor/title-editor/panels-colors.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 353
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 353

# Orientation uses the same responsive six-column geometry as Position,
# Scale, Anchor and Rotation; the former independent stretching HBox is gone.
orientation = properties[properties.index("transform_orientation_row_ = new QWidget") :
                         properties.index("row_shape_scale_options_ = new QWidget")]
assert "new QGridLayout(transform_orientation_row_)" in orientation
for token in (
    "setColumnMinimumWidth(0, 82)",
    "setColumnMinimumWidth(1, 0)",
    "setColumnMinimumWidth(2, 0)",
    "setColumnMinimumWidth(3, 0)",
    "addWidget(field_orientation_x, 0, 1)",
    "addWidget(field_orientation_y, 0, 2)",
    "addWidget(field_orientation_z, 0, 3)",
    "0, 5, Qt::AlignRight | Qt::AlignVCenter",
):
    assert token in orientation
assert "new QHBoxLayout(transform_orientation_row_)" not in orientation
assert "orientation_layout->addStretch" not in orientation

# Lock Scale keeps only its natural switch width, leaves spare space after all
# switches, and stays in the options row for both 2D Size and 3D Scale modes.
assert "chk_scale_lock_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);" in properties
assert "chk_transform_size_lock_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);" in properties
assert "shape_scale_options_layout->addWidget(chk_scale_lock_);" in properties
assert "shape_scale_options_layout->addWidget(chk_transform_size_lock_);" in properties
assert "shape_scale_options_layout->addWidget(chk_scale_lock_, 1);" not in properties
assert "shape_scale_options_layout->addStretch(1);" in properties
assert "transform_grid->addWidget(chk_transform_size_lock_" not in properties
assert 'chk_transform_size_lock_->setText(QStringLiteral("Lock Scale"));' in properties
assert refresh.count("const bool show_transform_lock =") == 1
assert "set_visible(chk_scale_lock_, show_transform_scale);" in refresh
assert "set_visible(chk_transform_size_lock_, show_transform_size);" in refresh
assert "chk_scale_lock_->setVisible(show_scale);" in refresh
assert "chk_transform_size_lock_->setVisible(show_size);" in refresh

# Locked docks expose no close feature. Tabbed docks plus the standalone narrow
# Sidebar and Editor Audio docks replace their complete headers with a zero-height
# title bar, then restore Qt's normal header when panels are unlocked.
lock_state = panels[panels.index("void TitleEditor::update_panel_lock_state()") :]
assert "locked_features = QDockWidget::NoDockWidgetFeatures;" in lock_state
assert "QDockWidget::DockWidgetClosable |" in lock_state
assert "dock == tools_dock_ || dock == editor_audio_dock_" in lock_state
assert "!tabifiedDockWidgets(dock).isEmpty()" in lock_state
assert 'setProperty("fxmLockedDockHeaderPlaceholder", true);' in lock_state
assert "placeholder->setFixedHeight(0);" in lock_state
assert "dock->setTitleBarWidget(placeholder);" in lock_state
assert "dock->setTitleBarWidget(nullptr);" in lock_state

print("Development Version 353 Orientation/Scale Lock/dock-header contract: PASS")
