#!/usr/bin/env python3
"""Independent OBS preferences, Editor discovery, version and Suite packaging."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = ROOT.parents[1]
PLUGIN_ROOT = REPOSITORY_ROOT / "plugins/obs/flux-motion"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
plugin_main = (PLUGIN_ROOT / "src/plugin-main.cpp").read_text(encoding="utf-8")
preferences = (PLUGIN_ROOT / "src/obs-plugin-preferences.cpp").read_text(encoding="utf-8")
editor_host = (PLUGIN_ROOT / "src/obs-editor-host.cpp").read_text(encoding="utf-8")
editor_main = read("Editor/main.cpp")
windows_build = read("build-windows.ps1")
editor_preferences = read("src/editor/title-editor/signal-handlers.inc")
motion_version = (ROOT / "VERSION.txt").read_text(encoding="utf-8").strip()
plugin_version = (PLUGIN_ROOT / "VERSION.txt").read_text(encoding="utf-8").strip()

assert "project(flux-motion VERSION 0.8.19)" in cmake
assert 'flux_read_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt"' in cmake
assert 'flux_read_version("${OBS_FXM_OBS_PLUGIN_ROOT}/VERSION.txt"' in cmake
assert motion_version == "2026 - v0.8.19-2-alpha"
assert plugin_version == "2026 - v0.8.19-2-alpha"
assert '#define FXM_VERSION_LABEL "2026 - v0.8.19-2-alpha"' in build_info
assert "show_plugin_preferences(main)" in plugin_main
assert "open_editor_preferences(&error)" not in plugin_main
assert "Cache / Prerendering" in preferences
assert "FluxMotionEditorExecutable" in preferences
assert "FluxSuite" in preferences and "Flux Motion" in preferences
assert 'category.group == QStringLiteral("OBS source")' in editor_preferences
assert "OBS source diagnostics are configured only in the OBS plugin preferences" in editor_preferences
assert "Shared plugin, editor, rendering, cache and media categories" in preferences
assert "resolved_editor_executable()" in editor_host
assert "installation/executable" in editor_main

# The OBS target must neither depend on nor stage the standalone Editor.
assert "add_dependencies(flux-motion-obs-plugin flux-motion-editor)" not in cmake
assert "Staging standalone Editor beside the OBS dock plugin" not in cmake
assert "install(TARGETS flux-motion-obs-plugin flux-motion-editor" not in cmake

assert "out\\dist" in windows_build
assert "$RepositoryRoot" in windows_build
assert '"Flux Motion Plugin for OBS"' in windows_build
assert '"Flux Motion"' in windows_build
assert '"Flux Motion.exe"' in windows_build
assert '"flux-motion-renderer.exe"' in windows_build
assert '$ReleaseLabel = (Get-Content -Raw -LiteralPath $VersionFilePath).Trim()' in windows_build

print("Flux Suite distribution contract passed")
