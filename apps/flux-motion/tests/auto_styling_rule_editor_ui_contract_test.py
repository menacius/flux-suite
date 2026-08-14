from pathlib import Path

root = Path(__file__).resolve().parents[1]
construction = (root / 'src/editor/properties-panel/construction-gradient-image-signals.inc').read_text(encoding='utf-8')
sync = (root / 'src/editor/properties-panel/property-synchronization.inc').read_text(encoding='utf-8')
popup = (root / 'src/editor/properties-panel/popup-state.inc').read_text(encoding='utf-8')
header = (root / 'src/editor/properties-panel.h').read_text(encoding='utf-8')

checks = {
    'dedicated modal editor': 'auto_rules_dialog_ = new QDialog(this)' in construction,
    'demo area is above splitter': construction.index('Demo / Test') < construction.index('new QSplitter'),
    'dialog rules list exists': 'lst_auto_rules_dialog_ = new QListWidget' in construction,
    'main panel opens editor': 'Open Rule Editor…' in construction,
    'double click opens editor': '&QListWidget::itemDoubleClicked' in sync,
    'lists are synchronized': 'lst_auto_rules_dialog_->setCurrentRow(row)' in sync,
    'descriptive names': 'auto_style_rule_descriptive_name' in popup and 'Paragraph beginning' in popup,
    'rule controls parented to dialog': 'new QGroupBox("Selected Rule", auto_rules_dialog_)' in construction,
    'main panel does not contain selected-rule editor': 'auto_layout->addWidget(auto_rule_editor_box_)' not in construction,
    'dialog members declared': 'QDialog         *auto_rules_dialog_' in header,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
if failed:
    raise SystemExit('Failed: ' + ', '.join(failed))
