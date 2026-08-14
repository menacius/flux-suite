from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src/editor/title-dock/list-selection-cues.inc").read_text(encoding="utf-8")

start = source.index("void TitleDock::populate_list()")
end = source.index("void TitleDock::refresh()", start)
populate = source[start:end]

assert "for (auto &t : TitleDataStore::instance().titles())" in populate
assert "title_list_combined_status_icon(*t," in populate
assert "title_list_combined_status_icon(*title," not in populate

print("Title dock list icon scope contract passed")
