from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
header = read("src/editor/title-editor.h")
internal = read("src/editor/title-editor-internal/widget-property-helpers.inc")
ui = read("src/editor/title-editor/ui-construction.inc")
panels = read("src/editor/title-editor/panels-colors.inc")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "311")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "311"' in build
assert 'constexpr int kEditorLayoutVersion = 7;' in internal
assert 'void set_editor_dock_visible(QDockWidget *dock, bool visible);' in header
for dock in (
    'tools_dock_', 'graphic_props_dock_', 'three_d_scene_dock_',
    'layer_props_dock_', 'effects_dock_', 'effects_presets_dock_',
    'styles_dock_', 'timeline_dock_', 'prerender_dock_',
    'editor_audio_dock_'
):
    assert f'set_editor_dock_visible({dock}, visible);' in ui + panels
assert 'dockWidgetArea(dock) == Qt::NoDockWidgetArea' in panels
assert 'QGuiApplication::screens()' in panels
assert 'resizeDocks({dock}' in panels
assert 'QTimer::singleShot(0, this' in panels
assert 'dock->setFloating(false);' in panels
