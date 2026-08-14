#!/usr/bin/env python3
"""Development Version 417: shared multi-title projects and public naming."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


model = read("Core/title-data.h")
serialization = read("src/core/title-data.cpp")
editor = read("src/editor/title-editor/playback-cache-preferences.inc")
project_panel = read("Editor/project-panel.cpp")
loading = read("Editor/project-loading-dialog.cpp")
editor_theme = read("Editor/editor-theme.cpp")
obs_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
obs_properties = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
cmake = read("CMakeLists.txt")
windows_build = read("build-windows.ps1")
locale = read("data/locale/en-US.ini")

assert "struct TitleProject" in model
assert "CurrentSchemaVersion = 1" in model
assert "std::vector<std::shared_ptr<Title>> titles" in model
assert 'root["format"] = "flux-motion-project"' in serialization
assert 'root["titles"]' in serialization
assert "QSaveFile file" in serialization
assert "root.is_array()" in serialization
assert 'root.contains("title")' in serialization
assert "fxm::packed_title::has_packed_signature" in serialization
for stage in (
    "Reading project",
    "Loading assets",
    "Initializing timelines",
    "Building caches",
    "Preparing previews",
):
    assert stage in serialization

for operation in (
    "create_project_title",
    "duplicate_project_titles",
    "rename_project_title",
    "delete_project_titles",
    "import_project_titles",
    "export_project_titles",
    "reorder_project_titles",
):
    assert operation in editor
assert "ExtendedSelection" in project_panel
assert "InternalMove" in project_panel
assert "itemDoubleClicked" in project_panel
assert "Search project" in project_panel
assert "QStyle::SP_" not in project_panel
for icon in (
    "add.svg",
    "duplicate.svg",
    "rename.svg",
    "delete.svg",
    "import.svg",
    "export.svg",
    "add-to-scene.svg",
):
    assert f'"{icon}"' in project_panel
assert "fxm_icon(icon_name)" in project_panel
assert "PaletteChange" in project_panel
assert "FluxMotionProjectBrowser" in project_panel
assert "FluxMotionDockToolbar" in project_panel
assert "QDockWidget { border: 1px solid #474747; border-radius: 4px; }" in editor_theme
assert "QToolBar#FluxMotionDockToolbar" in editor_theme
assert "Project loading was cancelled" in serialization
assert "QEventLoop::AllEvents" in loading

assert "PROP_PROJECT_PATH" in obs_properties
assert "populate_title_property" in obs_properties
assert "project_modified_msecs" in obs_runtime
assert "reload_source_project" in obs_runtime
assert "source_title_snapshot" in obs_runtime
assert "selected_exists" in obs_runtime

assert 'OUTPUT_NAME "Flux Motion"' in cmake
assert '"Flux Motion Plugin for OBS"' in windows_build
assert '"Flux Motion"' in windows_build
assert 'OBSTitles.EditorWindowTitle="Flux Motion"' in locale
assert "Flux Motion Editor" not in locale
legacy_obs_distribution = '(Join-Path $PackageOutputDir "Flux Motion OBS Plugin")'
assert legacy_obs_distribution in windows_build
assert windows_build.count("Flux Motion OBS Plugin") == 1

print("Development Version 417 multi-title project contract passed")
