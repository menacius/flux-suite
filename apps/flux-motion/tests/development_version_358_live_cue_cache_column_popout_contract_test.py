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
population = read("src/editor/title-dock/live-text-cache-playlist.inc")
locale = read("data/locale/en-US.ini")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 358
assert build_version >= 358
assert manifest["development_version"] >= 358

assert "void enforce_live_text_cache_column_visibility();" in header
assert "void open_live_text_columns_window();" in header
assert "QDialog      *live_text_columns_dialog_ = nullptr;" in header
assert "QWidget      *live_text_table_host_ = nullptr;" in header

assert "void TitleDock::enforce_live_text_cache_column_visibility()" in selection
assert "text_table_->setColumnHidden(" in selection
assert "kLiveCacheColumn, !CacheManager::instance().cacheEnabled()" in selection
restore = selection.split("bool TitleDock::restore_live_text_header_state()", 1)[1]
restore = restore.split("bool TitleDock::has_checked_live_text_rows()", 1)[0]
assert "enforce_live_text_cache_column_visibility();" in restore
assert population.count("enforce_live_text_cache_column_visibility();") >= 4

assert "text_table_ = new LiveTextCueTable(live_text_table_host_);" in lifecycle
assert "text_table_->setParent(dialog);" in selection
assert "text_table_->setParent(live_text_table_host_);" in selection
assert "live_text_table_host_layout_->insertWidget(0, text_table_, 1);" in selection
assert "update_live_text_columns_window_title();" in population
assert "new LiveTextCueTable(dialog)" not in selection
assert "new LiveTextCueTable(dialog)" not in lifecycle

for key in (
    "OBSTitles.OpenLiveTextColumnsWindow",
    "OBSTitles.LiveTextColumnsWindow",
    "OBSTitles.LiveTextColumnsDetached",
    "OBSTitles.ShowLiveTextColumnsWindow",
):
    assert key in locale

print("development version 358 cache column and Live Text pop-out contract passed")
