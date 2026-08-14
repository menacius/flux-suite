#!/usr/bin/env python3
"""Structural audit for Development Version 118 external-data diagnostics logging."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
checks = []

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")

def check(name, condition):
    checks.append((name, bool(condition)))

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
log_h = read("src/core/external-data-log.h")
log_cpp = read("src/core/external-data-log.cpp")
logger = read("src/core/title-logger.cpp")
plugin = read("../../plugins/obs/flux-motion/src/plugin-main.cpp")
manager = read("src/core/external-data.cpp")
provider = read("src/core/external-data-provider.cpp")
settings = read("Editor/external-data-settings-dialog.cpp")
binding = read("Editor/external-data-binding-dialog.cpp")
mapping = read("Editor/external-data-table-mapping-dialog.cpp")
dock_lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
dock_cells = read("src/editor/title-dock/live-text-cache-playlist.inc")
dock_cues = read("src/editor/title-dock/list-selection-cues.inc")
source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
source_transitions = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
test = read("tests/external_data_logging_test.cpp")
transition_test = read("tests/external_data_live_cue_transition_contract_test.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")

check("development version is synchronized as 118",
      'set(OBS_FXM_DEVELOPMENT_VERSION "118")' in cmake and
      '#define FXM_DEVELOPMENT_VERSION "118"' in build and
      'Development Version 118' in readme)
check("standard-only external data logging bridge is built with core",
      'src/core/external-data-log.cpp' in cmake and
      'src/core/external-data-log.h' in cmake and
      'using Sink = std::function' in log_h and
      '#include <Q' not in log_h and '#include <Q' not in log_cpp)
check("logging bridge supports lazy level filtering and failure isolation",
      'write_lazy' in log_h and 'EnabledProbe' in log_h and
      'catch (...)' in log_cpp and 'Logging must never affect provider' in log_cpp)
check("privacy helpers redact locations and fingerprint values",
      'safe_location' in log_cpp and "clean.rfind('@'" in log_cpp and
      "clean.find('?" in log_cpp and 'value_summary' in log_cpp and
      'fingerprint=' in log_cpp and 'fnv1a64' in log_cpp)
check("External Data category is exposed by the existing logger",
      'QStringLiteral("ExternalData")' in logger and
      'QStringLiteral("External data")' in logger)
check("OBS plugin installs and removes the bridge",
      'ExternalDataLog::set_sink' in plugin and
      'FXM_LOG_INFO("ExternalData"' in plugin and
      'ExternalDataLog::clear_sink' in plugin)
check("manager logs updates, tables and render queue flow",
      '"Manager"' in manager and '"TableMapping"' in manager and
      '"RenderQueue"' in manager and 'value_summary' in manager)
check("providers log lifecycle and asynchronous network flow",
      '"Provider"' in provider and '"HttpProvider"' in provider and
      '"WebSocket"' in provider and '"ProviderService"' in provider and
      'safe_location' in provider)
check("authentication secrets are not written into provider log messages",
      'authentication_token=' not in provider and
      'bearer=' not in provider.lower() and
      'tokenValue' not in provider)
check("source and binding settings actions are logged",
      '"SettingsUI"' in settings and 'test connection' in settings and
      '"BindingUI"' in binding and '"TableMappingUI"' in mapping)
check("actual dock table update and cell population paths are logged",
      '"Dock"' in dock_lifecycle and 'table update received' in dock_lifecycle and
      '"DockCell"' in dock_cells and 'displayed=' in dock_cells and
      'ExternalTableManaged' in dock_cells)
check("cue requests and Loop/Pause source commits are logged",
      '"CueControl"' in dock_cues and 'decision=' in dock_cues and
      '"CuePlayback"' in source_transitions and
      'committed pending row after outro' in source_transitions and
      '"CueApply"' in source_runtime)
check("source-side pending row application resolves external table values",
      'apply_live_text_runtime_binding' in source_runtime and
      'effective_live_text_cue_value' in source_runtime and
      'apply_live_cue_layer_value(target, cue_value)' in source_runtime and
      'apply_live_cue_layer_value(target, title->live_text_rows[value_row][col])' not in source_runtime)
check("logging regression test is registered and checks privacy",
      'external_data_logging_test' in cmake and
      'safe_location' in test and 'raw external value is not present' in test and
      'level probe suppresses trace entries' in test)
check("Loop/Pause external cue transition regression is registered",
      'external_data_live_cue_transition_contract_test' in cmake and
      'effective_live_text_cue_value' in transition_test and
      'Loop/Pause transition must not apply empty authored table cells' in transition_test)
check("documentation explains Debug/Trace and privacy behavior",
      'External-data diagnostics logging' in docs and
      'Authentication tokens' in docs and 'fingerprint' in docs and
      'External-data diagnostics' in readme)
check("changelog documents Development 118 logging",
      'Development Version 118' in changelog and
      'External Data' in changelog and 'redact' in changelog)

failed = 0
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
    failed += 0 if passed else 1
print(f"\nDevelopment 118 audit: {len(checks)-failed}/{len(checks)} passed")
sys.exit(1 if failed else 0)
