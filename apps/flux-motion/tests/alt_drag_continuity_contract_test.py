from pathlib import Path

source = Path('src/canvas/canvas-preview/pointer-events.inc').read_text(encoding='utf-8')
required = [
    'alt_duplicate_active_ = duplicate_selected_layers_for_drag()',
    'if (alt_duplicate_active_)',
    'for (const auto &state : drag_layer_states_)',
    'title_->find_layer(state.id)',
]
missing = [item for item in required if item not in source]
if missing:
    raise SystemExit('Missing Alt+drag continuity contract markers: ' + ', '.join(missing))
print('Alt+drag continuity contract passed')
