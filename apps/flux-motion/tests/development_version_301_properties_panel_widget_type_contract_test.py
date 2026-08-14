from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

def test_extrusion_switch_uses_public_qt_base_type_in_header():
    header = read("src/editor/properties-panel.h")
    popup = read("src/editor/properties-panel/popup-state.inc")
    assert "QCheckBox       *chk_geometry_extrusion_ = nullptr;" in header
    assert "FxmSwitch       *chk_geometry_extrusion_" not in header
    assert "new FxmSwitch" in popup

def test_development_version_301():
    assert '#define FXM_DEVELOPMENT_VERSION "301"' in read("src/core/build-info.h")
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "301")' in read("CMakeLists.txt")
