from pathlib import Path
p = Path('src/editor/properties-panel/property-synchronization.inc').read_text(encoding='utf-8')
checks = {
    'save preserves regex condition type': 'if (rule.condition_type != "regex")' in p,
    'save preserves inferred regex pattern': 'must not replace the inferred' in p,
    'selected preset stored per rule': 'rule.style_preset_id = cmb_auto_rule_style_->currentData().toString().toStdString();' in p,
    'selected preset cached': 'cache_rule_format(rule);' in p,
    'rule styling invalidated after save': 'invalidate_auto_text_styling();' in p,
}
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
assert all(checks.values())
