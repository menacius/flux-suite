from pathlib import Path
pointer = Path('src/canvas/canvas-preview/pointer-events.inc').read_text()
release = Path('src/canvas/canvas-preview/gpu-frame-rendering.inc').read_text()
required_pointer = [
    'if (alt_duplicate_active_)',
    'drag_layer_states_ is the',
    'active_drag_ids',
]
required_release = [
    'Keep the duplicated objects selected at the end of the gesture',
    'emit layers_selected(selected_layer_ids_)',
]
missing = [x for x in required_pointer if x not in pointer] + [x for x in required_release if x not in release]
if missing:
    raise SystemExit('Missing authoritative Alt+drag clone markers: ' + ', '.join(missing))
print('Authoritative Alt+drag clone contract passed')
