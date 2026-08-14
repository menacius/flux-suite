from pathlib import Path
root = Path(__file__).resolve().parents[1]
cmake=(root/'CMakeLists.txt').read_text()
build=(root/'src/core/build-info.h').read_text()
helpers=(root/'src/editor/title-editor-internal/widget-property-helpers.inc').read_text()
layout=(root/'src/editor/title-editor/panels-colors.inc').read_text()
props=(root/'../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc').read_text()
assert 'OBS_FXM_DEVELOPMENT_VERSION "312"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "312"' in build
assert 'kEditorLayoutVersion = 8' in helpers
assert 'splitDockWidget(tools_dock_, editor_audio_dock_, Qt::Horizontal)' in layout
assert 'splitDockWidget(editor_audio_dock_, utility_anchor, Qt::Horizontal)' in layout
assert 'addDockWidget(Qt::RightDockWidgetArea, layer_props_dock_)' in layout
assert 'addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_)' in layout
assert 'Reset All Flux Motion Settings' in props
assert 'source_reset_all_settings_clicked' in props
assert 'removeRecursively' in props
