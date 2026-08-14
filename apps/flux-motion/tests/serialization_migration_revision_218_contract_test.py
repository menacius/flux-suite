#!/usr/bin/env python3
"""Source contract for Development Version 218 serialization and Timeline context menus."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
title_data = read("src/core/title-data.cpp")
title_header = read("Core/title-data.h")
layer_header = read("Core/layer-model.h")
effect_header = read("src/effects/layer-effects.h")
transition_header = read("src/transitions/layer-transition.h")
timeline_h = read("src/timeline/timeline-widget.h")
timeline_cpp = read("src/timeline/timeline-widget.cpp")
canvas_h = read("src/canvas/canvas-preview.h")
canvas_menu = read("src/canvas/canvas-preview/gpu-frame-rendering.inc")
docks = read("src/editor/title-editor/commands-docks.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION \"(21[8-9]|2[2-9][0-9]|[3-9][0-9]{2,})\"\)', cmake)
assert re.search(r'#define FXM_DEVELOPMENT_VERSION \"(21[8-9]|2[2-9][0-9]|[3-9][0-9]{2,})\"', build)
assert 'kCurrentTitleSchemaVersion = 6' in schema
assert re.search(r'kCurrentDevelopmentVersion = (21[8-9]|2[2-9][0-9]|[3-9][0-9]{2,})', schema)
assert 'case 218:' in schema
assert 'migrate_serialization_audit_218' in schema
assert 'disabled malformed parent bind matrix' in schema
assert 'clean_object_array(layer, "effects", "layer effects")' in schema
assert 'recover_array_member(title, "cameras"' in schema

for source in (title_header, layer_header, effect_header, transition_header):
    assert 'serialization_passthrough_json' in source
assert 'merge_nested_passthrough' in title_data
assert 'merge_surviving_passthrough' in title_data
assert 'future fields' in title_data or 'Unknown/newer fields' in title_header
assert 'duplicateOrMissingIds' in title_data
assert 'danglingOrCyclicLinks' in title_data
assert 'kMaxLoggedMigrationDetails = 8' in title_data
assert 'QSaveFile' in title_data

assert 'layer_context_menu_requested(const QPoint &global_pos)' in timeline_h
assert 'emit layer_context_menu_requested(ev->globalPos())' in timeline_cpp
assert 'show_selected_layers_context_menu(const QPoint &global_pos)' in canvas_h
assert 'context_menu_selection_override_' in canvas_h
assert 'forced_layer_selection' in canvas_menu
context_menu_start = canvas_menu.index('void CanvasPreview::contextMenuEvent')
assert 'forced_layer_selection' not in canvas_menu[:context_menu_start]
assert '&TimelineWidget::layer_context_menu_requested' in docks
assert '&CanvasPreview::show_selected_layers_context_menu' not in docks  # connected through guarded lambda
assert 'canvas_->show_selected_layers_context_menu(global_pos)' in docks

assert 'Title schema 6' in readme
assert re.search(r'# v0.8.11-alpha — Development Version (21[8-9]|2[2-9][0-9]|[3-9][0-9]{2,})', changelog)
assert 'Schema 6' in read('docs/ARCHITECTURE_AND_BUILD.md') or 'Schema 6' in read('docs/CHANGELOG.md')

print('Development Version 218 serialization/migration contract passed')
