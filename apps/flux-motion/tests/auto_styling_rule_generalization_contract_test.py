from pathlib import Path
root = Path(__file__).resolve().parents[1]
h = (root / "src/text/title-rich-text.h").read_text(encoding="utf-8")
cpp = (root / "src/text/title-rich-text.cpp").read_text(encoding="utf-8")
ui = (root / "src/editor/properties-panel/construction-gradient-image-signals.inc").read_text(encoding="utf-8")
sync = (root / "src/editor/properties-panel/property-synchronization.inc").read_text(encoding="utf-8")
for token in ["generalization_mode", "prevent_duplicates", "allow_multiple_cases"]: assert token in h
assert "rich_text_merge_auto_style_rules" in cpp
assert "Smart merge equivalent examples" in ui
assert "Skip exact duplicate rules" in ui
assert "Apply one rule to every matching case" in ui
assert "rich_text_merge_auto_style_rules" in sync
print("auto styling rule generalization contract passed")
