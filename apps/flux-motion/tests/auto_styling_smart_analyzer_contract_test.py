from pathlib import Path

root = Path(__file__).resolve().parents[1]
rich = (root / 'src/text/title-rich-text.cpp').read_text(encoding='utf-8')
ui = (root / 'src/editor/properties-panel/construction-gradient-image-signals.inc').read_text(encoding='utf-8')
wiring = (root / 'src/editor/properties-panel/property-synchronization.inc').read_text(encoding='utf-8')

for token in ('ascii_looks_like_time', 'ascii_looks_like_date', 'ascii_looks_like_email',
              'ascii_looks_like_url', 'generic_pattern_for_sample', 'structural_separator_before'):
    assert token in rich
assert 'rule.cached_format = range.format' in rich
assert 'rule.cached_mask = range.mask' in rich
assert 'Clear All Rules' in ui
assert 'Clear All Auto Styling Rules' in wiring
assert 'QMessageBox::question' in wiring
print('smart auto styling analyzer and clear-all contract passed')
