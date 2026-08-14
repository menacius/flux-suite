from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
modern = read("Editor/fxm-modern-controls.cpp")
properties = read("src/editor/properties-panel/popup-state.inc")
refresh = read("src/editor/properties-panel/selection-refresh.inc")
shape = read("src/editor/properties-panel/construction-gradient-image-signals.inc")
image = read("src/editor/properties-panel/construction-transform-character.inc")
docks = read("src/editor/title-editor/commands-docks.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 352
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 352

# Rotation Z can no longer be pushed into the legacy column 4 path. In 3D it
# completes X/Y/Z at column 3; in 2D the scalar field spans all three columns.
assert "addWidget(transform_rotation_field_z_, 3, 3);" in refresh
assert "addWidget(transform_rotation_field_z_, 3, 1, 1, 3);" in refresh
rotation_refresh = refresh[refresh.index("if (transform_grid_ && transform_rotation_field_z_)") :]
assert "show_3d ? 4" not in rotation_refresh[:900]

# Scale Lock lives with Scale Stroke / Scale Corners and is never dynamically
# reinserted into the numeric grid.
assert "shape_scale_options_layout->addWidget(chk_scale_lock_);" in properties
assert "shape_scale_options_layout->addWidget(chk_scale_lock_, 1);" not in properties
assert "transform_grid->addWidget(chk_scale_lock_" not in properties
assert "transform_grid_->addWidget(chk_scale_lock_" not in refresh
assert "show_transform_lock || is_authored_shape" in refresh

# Explicit layout minima and input size hints no longer force a wide dock.
for source in (properties, shape, image):
    assert "field->setMinimumWidth(130);" not in source
assert "setColumnMinimumWidth(1, 0)" in properties
assert "setColumnMinimumWidth(1, 0)" in shape
assert "setColumnMinimumWidth(0, 0)" in image
assert "props_->setMinimumWidth(180);" in docks
assert "props_->setMinimumWidth(260);" not in docks
assert 'inner->setProperty("fxmResponsivePropertyFields", true);' in properties
assert "responsive_fields ? QSizePolicy::Ignored" in modern
assert "combo->view()->setMinimumWidth(std::max(0, combo->width()));" in modern

# The whole content tree now receives the Camera field/label pattern while all
# colors still come from QPalette-derived names.
assert 'inner->setObjectName(QStringLiteral("FxmLayerPropertiesContent"));' in properties
assert "QWidget#FxmLayerPropertiesContent QDoubleSpinBox" in properties
assert "background:%4" in properties
assert "control_bg_name" in properties and "highlight_name" in properties

print("Development Version 352 responsive Properties/Rotation/Scale Lock contract: PASS")
