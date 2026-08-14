#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "src/editor/title-dock.h").read_text(encoding="utf-8")
life = (root / "src/editor/title-dock/dock-lifecycle.inc").read_text(encoding="utf-8")
collapse = (root / "src/editor/title-dock/collapsible-titles.inc").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build = (root / "src/core/build-info.h").read_text(encoding="utf-8")
locale = (root / "data/locale/en-US.ini").read_text(encoding="utf-8")

checks = {
    "version synchronized": 'set(OBS_FXM_DEVELOPMENT_VERSION "132")' in cmake and '#define FXM_DEVELOPMENT_VERSION "132"' in build,
    "persistent state keys": all(token in collapse for token in ["titlesCollapsed", "titlesExpandedWidth", "titlesExpandedSplitterSize", "dockArea"]),
    "no title model recreation on toggle": "list_->clear" not in collapse and "new TitleListWidget" not in collapse,
    "expanded and compact widgets retained": all(token in header for token in ["titles_expanded_content_", "titles_compact_rail_", "titles_compact_active_title_", "titles_compact_cue_state_", "titles_compact_cache_state_"]),
    "only title pane hidden": "titles_expanded_content_->hide()" in collapse and "text_table_->hide" not in collapse,
    "dock-aware direction": "Qt::RightDockWidgetArea" in collapse and "isFloating()" in collapse,
    "selection drives rail": "update_titles_compact_rail();" in (root / "src/editor/title-dock/list-selection-cues.inc").read_text(encoding="utf-8"),
    "localized UI": all(key in locale for key in ["CollapseTitlesAndGraphics", "ExpandTitlesAndGraphics", "TitlesRailActiveTitleFormat"]),
    "collapse controls wired": "set_titles_collapsed(true)" in life and "set_titles_collapsed(false)" in life,
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
raise SystemExit(1 if failed else 0)
