from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

ui = read('src/editor/title-editor/ui-construction.inc')
panels = read('src/editor/title-editor/panels-colors.inc')
build = read('src/core/build-info.h')

import re
version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert version and int(version.group(1)) >= 310
assert 'set_editor_dock_visible(tools_dock_, visible);' in ui
assert 'set_editor_dock_visible(graphic_props_dock_, visible);' in ui
assert 'set_editor_dock_visible(effects_presets_dock_, visible);' in ui
assert 'Qt::NoDockWidgetArea' not in panels[panels.index('void TitleEditor::update_panel_lock_state()'):]
assert 'dock->setMaximumHeight(QWIDGETSIZE_MAX);' in panels
assert 'dock->setFeatures(panels_locked_ ? locked_features : unlocked_features);' in panels
