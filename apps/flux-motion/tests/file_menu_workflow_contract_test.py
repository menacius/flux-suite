#!/usr/bin/env python3
"""Context-aware File menu, project persistence, and recent-files contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


header = read("src/editor/title-editor.h")
menu = read("src/editor/title-editor/panels-colors.inc")
workflow = read("src/editor/title-editor/playback-cache-preferences.inc")
events = read("src/editor/title-editor/editor-events.inc")
recent = read("Editor/recent-projects.h")
welcome = read("Editor/standalone-welcome-screen.cpp")
main = read("Editor/main.cpp")
model = read("Core/title-data.h")
serialization = read("src/core/title-data.cpp")
assets = read("Editor/asset-library.cpp")
imports = read("src/editor/title-editor/import-documents.inc")
locale = read("data/locale/en-US.ini")

# One host-capability model and one command dispatcher own both contexts.
for token in (
    "struct EditorExecutionContext",
    "EditorHostKind::Standalone",
    "EditorHostKind::ObsPlugin",
    "supports_new_open",
    "supports_recent_projects",
    "supports_save_as",
    "supports_media_export",
    "close_returns_to_host",
    "enum class FileCommand",
):
    assert token in header, token
for token in (
    "build_file_menu",
    "register_file_command",
    "execute_file_command",
    "refresh_file_commands",
    "FileCommand::NewProject",
    "FileCommand::OpenProject",
    "FileCommand::Import",
    "FileCommand::SaveAsAsset",
    "FileCommand::SaveInLibrary",
    "FileCommand::ProjectSettings",
):
    assert token in menu, token
assert 'fxm_tr("OBSTitles.ImportEllipsis")' in menu
assert "case FileCommand::Import: import_document(); break;" in menu
assert 'OBSTitles.ImportEllipsis="Import…"' in locale

# The restored File > Import command uses the unified document importer, which
# keeps layered PSD/XCF and project formats alongside SVG and raster images.
for token in (
    "unified_import_filter()",
    'suffix == QStringLiteral("svg")',
    'suffix == QStringLiteral("psd")',
    'suffix == QStringLiteral("xcf")',
    'suffix == QStringLiteral("fxmp")',
    "import_photoshop_document(path)",
    "import_gimp_document(path)",
):
    assert token in imports, token
assert "EditorExecutionContext::obsPlugin(" in main
assert "EditorExecutionContext::standalone()" in main

# Standalone Save is path-aware and first Save delegates to Save As.
assert "current_project_path_.isEmpty()" in workflow
assert "return save_title_as();" in workflow
assert "QFileDialog::getSaveFileName" in workflow
assert "save_project_to_path(current_project_path_" in workflow
assert "TitleDataStore::instance().export_title" in workflow
assert "QMessageBox::Save | QMessageBox::Discard" in read(
    "src/editor/title-editor/layout-template-tools.inc"
)
assert "emit editor_closed();" in events

# Recents persist full paths, timestamps, and thumbnails and prune missing files.
for token in (
    'QStringLiteral("path")',
    'QStringLiteral("lastOpened")',
    'QStringLiteral("thumbnail")',
    "!info.exists() || !info.isFile()",
    "Clear Recent Projects",
):
    assert token in recent or token in read("data/locale/en-US.ini"), token
assert "RecentProjects::entries()" in welcome
assert "item->setToolTip(project->full_path" in welcome
assert "itemDoubleClicked" in welcome

# Asset tags round-trip and participate in browser discovery immediately.
assert "asset_tags" in model
assert serialization.count('"asset_tags"') >= 3
assert "stored->asset_tags.push_back" in workflow
assert "title->asset_tags" in assets
assert "on_change" in assets and "reload()" in assets

print("Context-aware File menu workflow contract passed")
