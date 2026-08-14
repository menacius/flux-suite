#!/usr/bin/env python3
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
plugin = read("../../plugins/obs/flux-motion/src/plugin-main.h")
readme = read("README.md")
docs_index = read("docs/README.md")
changelog = read("docs/CHANGELOG.md")
install = read("INSTALL.txt")
vcpkg = json.loads(read("vcpkg.json"))
manifest = json.loads(read("tests/test-suite-manifest.json"))

project_version = re.search(r"project\(flux-motion VERSION (\d+)\.(\d+)\.(\d+)\)", cmake)
assert project_version and tuple(map(int, project_version.groups())) >= (0, 8, 13)
assert 'flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"' in cmake
assert 'flux_extract_semver("${FLUX_MOTION_VERSION_LABEL}" OBS_FXM_VERSION)' in cmake
assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION \"(\d+)\"', cmake).group(1)) >= 395
public_version = re.search(r'#define PLUGIN_VERSION "([^"]+)"', build).group(1)
assert public_version == f"{'.'.join(project_version.groups())}-alpha"
assert int(re.search(r'FXM_DEVELOPMENT_VERSION \"(\d+)\"', build).group(1)) >= 395
assert f'#define PLUGIN_VERSION "{public_version}"' in plugin
assert vcpkg["version-string"] == public_version

readme_version = re.search(r"`v([^`]+)` · `Development Version (\d+)`", readme)
assert readme_version and readme_version.group(1) == public_version and int(readme_version.group(2)) >= 395
assert "Highlights since Development Version 281" in readme
for feature in (
    "Trim Paths",
    "Motion Blur and temporal rendering",
    "3D lighting, materials and shadows",
    "Assets and document import",
    "Live cueing and preview",
):
    assert feature in readme

assert "VALIDATION_HISTORY.md" in docs_index
assert "## Development Version 395 — performance audit and hot-path reduction" in changelog
package_example = re.search(
    r"Flux_Motion_v([^_]+)_development-version-(\d+)_windows-x64\.zip",
    install,
)
assert package_example and package_example.group(1) == public_version
assert int(package_example.group(2)) >= 395
assert manifest["development_version"] >= 395

logger_h = read("src/core/title-logger.h")
logger_cpp = read("src/core/title-logger.cpp")
preferences = read("src/core/title-preferences.cpp")
signals = read("src/editor/title-editor/signal-handlers.inc")
source_render = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
canvas_render = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
shadow_render = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
audit = read("docs/VALIDATION_HISTORY.md")

for macro in ("FXM_LOG_ERROR", "FXM_LOG_WARNING", "FXM_LOG_INFO", "FXM_LOG_DEBUG", "FXM_LOG_TRACE"):
    assert macro in logger_h
assert logger_h.count("::TitleLogger::wouldLog") >= 5
assert logger_h.count("::TitleLogger::logPrepared") >= 5
assert "QFile g_log_file;" in logger_cpp
assert "QByteArray g_pending_log_data;" in logger_cpp
assert "kLogBufferFlushBytes = 64 * 1024" in logger_cpp
assert "g_configuration_initialized" in logger_cpp
assert "TitleLogger::refreshConfiguration();" in preferences
for category in ("SourceTiming", "SourcePresentation", "RenderDiagnostics"):
    match = re.search(r'\{QStringLiteral\("' + category + r'"\).*?QStringLiteral\("[^"]+"\), false\}', logger_cpp, re.S)
    assert match, category
assert "type == QEvent::Move || type == QEvent::Resize" in signals
transition_block = signals[signals.index("const bool main_window_layout_transition"):signals.index("const bool dock_structure_transition")]
assert "LayoutRequest" not in transition_block
assert "log_source_presentation || log_source_flicker" in source_render
assert "trace_render_diagnostics || warn_render_diagnostics" in canvas_render
assert "authored_shadow_map_size / 2u" in shadow_render
assert "Point-light" in audit and "4×" in audit

current_test = "tests/development_version_395_performance_audit_contract_test.py"
found = False

def visit(value):
    global found
    if isinstance(value, list):
        if current_test in value:
            found = True
        for item in value:
            visit(item)
    elif isinstance(value, dict):
        for item in value.values():
            visit(item)

visit(manifest)
assert found
print("Development Version 395 performance-audit contract: PASS")
