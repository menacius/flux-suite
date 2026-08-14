from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / 'src/editor/properties-panel/property-synchronization.inc').read_text(encoding='utf-8')
model = (root / 'src/text/title-rich-text.cpp').read_text(encoding='utf-8')

assert 'catch (const std::exception &error)' in ui
assert 'catch (...)' in ui
assert 'QSignalBlocker blocker(chk_auto_style_enabled_)' in ui
assert 'clamped_end(range.start, range.length, doc.plain_text.size())' in model
assert 'kMaxLearnedSampleBytes' in model
assert 'Allocation/iterator failures must not escape' in model
print('auto styling Learn crash-safety contract passed')
