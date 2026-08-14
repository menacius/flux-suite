from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text()
build = (root / "src/core/build-info.h").read_text()
preferences = (root / "src/editor/title-editor/signal-handlers.inc").read_text()
source_properties = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc").read_text()

cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert cmake_version and int(cmake_version.group(1)) >= 329
assert build_version and int(build_version.group(1)) >= 329

# The user-facing reset exists only in Preferences > Advanced, never in the
# OBS source property list.
assert 'advanced_layout->addWidget(danger_box)' in preferences
assert 'Reset All Flux Motion Settings' in preferences
assert 'FxmDangerResetButton' in preferences
assert 'background:#b42318' in preferences
assert 'reset_all_fxm_settings' not in source_properties
assert 'obs_properties_add_button' not in source_properties[source_properties.index('source_get_properties'):source_properties.index('source_get_defaults')]

# It is deliberately difficult to trigger accidentally.
assert 'QMessageBox::warning' in preferences
assert 'QMessageBox::critical' in preferences
assert 'QMessageBox::Reset | QMessageBox::Cancel' in preferences
assert 'QMessageBox::Yes | QMessageBox::No' in preferences

# Settings and Windows registry state are cleared explicitly.
assert 'QSettings::NativeFormat' in preferences
assert 'QSettings::IniFormat' in preferences
assert 'HKEY_CURRENT_USER\\\\Software\\\\FluxMotion' in preferences
for application in ('Dock', 'Color', 'EditorPanels'):
    assert f'QStringLiteral("{application}")' in preferences

# Plugin-owned files are removed without recursively deleting generic roots.
assert 'obs_module_config_path("")' in preferences
for owned_path in ('style-presets', 'palettes', 'Effects', 'frame-cache',
                   'video-proxies', 'video-optical-flow'):
    assert f'QStringLiteral("{owned_path}")' in preferences
assert 'absolute == app_data || absolute == cache' in preferences
assert 'fxm_reset_remove_logs(log_directory)' in preferences
