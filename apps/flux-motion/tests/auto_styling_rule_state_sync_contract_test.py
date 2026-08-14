from pathlib import Path
p = Path('src/editor/properties-panel/property-synchronization.inc').read_text(encoding='utf-8')
checks = {
    'embedded learned style option': 'Embedded / learned style' in p,
    'missing preset visibility': 'Missing preset' in p,
    'per-rule preset immediate handler': 'rule.style_preset_id = selected_id' in p,
    'preserve selected rule': 'previous_rule_row' in p and 'restored_rule_row' in p,
    'signal blocked rule loading': 'block_style(cmb_auto_rule_style_)' in p and 'block_generalization' in p,
    'selected rule only': 'auto_style_rules[static_cast<size_t>(row)]' in p,
}
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
assert all(checks.values())
