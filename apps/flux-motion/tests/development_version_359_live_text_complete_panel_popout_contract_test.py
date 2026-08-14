from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
header = read("src/editor/title-dock.h")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
selection = read("src/editor/title-dock/list-selection-cues.inc")
locale = read("data/locale/en-US.ini")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 359
assert build_version >= 359
assert manifest["development_version"] >= 359

for member in (
    "QWidget      *live_text_section_ = nullptr;",
    "QVBoxLayout  *live_text_section_layout_ = nullptr;",
    "QWidget      *live_text_header_widget_ = nullptr;",
    "QToolBar     *live_text_toolbar_ = nullptr;",
):
    assert member in header

assert "live_text_header_widget_ = new QWidget(live_section);" in lifecycle
assert "live_text_toolbar_ = make_obs_dock_toolbar(live_section);" in lifecycle
assert "live_layout->addWidget(live_text_header_widget_);" in lifecycle
assert "live_layout->addWidget(live_text_toolbar_)" in lifecycle

for widget in (
    "live_text_header_widget_",
    "text_table_",
    "live_text_toolbar_",
):
    assert f"{widget}->setParent(dialog);" in selection

assert "live_text_header_widget_->setParent(live_text_section_);" in selection
assert "text_table_->setParent(live_text_table_host_);" in selection
assert "live_text_toolbar_->setParent(live_text_section_);" in selection
assert "live_text_section_layout_->insertWidget(0, live_text_header_widget_);" in selection
assert "live_text_section_layout_->insertWidget(2, live_text_toolbar_);" in selection

assert 'OBSTitles.OpenLiveTextColumnsWindow="Open Live Text Cues in window"' in locale
assert "complete Live Text Cues panel" in locale

print("development version 359 complete Live Text panel pop-out contract passed")
