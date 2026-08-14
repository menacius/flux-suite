from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
delegate = read("src/editor/title-dock/import-export-helpers.inc")
cues = read("src/editor/title-dock/list-selection-cues.inc")

assert int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 366
assert int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 366
assert manifest["development_version"] >= 366

# List rows no longer impose the former 240 px minimum. Their width follows
# the current viewport and therefore shrinks or expands with the dock.
size_hint_start = delegate.index("QSize sizeHint(")
paint_start = delegate.index("void paint(", size_hint_start)
size_hint = delegate[size_hint_start:paint_start]
assert "std::max(240" not in size_hint
assert 'qobject_cast<const QListView *>(parent())' in size_hint
assert "list_view->viewport()->contentsRect().width()" in size_hint
assert "QSize(std::max(0, available_width), 44)" in size_hint

# List mode must relayout on dock resize and must not expose horizontal scroll
# caused by stale/fixed row geometry. Icon mode keeps its authored card grid.
mode_start = cues.index("void TitleDock::update_template_view_mode()")
mode_end = cues.index("void TitleDock::on_toggle_template_view()", mode_start)
mode = cues[mode_start:mode_end]
list_branch = mode[mode.index("} else {"):]
assert "list_->setResizeMode(QListView::Adjust);" in list_branch
assert "list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);" in list_branch
assert "list_->setResizeMode(QListView::Fixed);" not in list_branch
assert "list_->setGridSize(QSize());" in list_branch
icon_branch = mode[:mode.index("} else {")]
assert "QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight)" in icon_branch

print("Development Version 366 responsive title-list width contract: PASS")
