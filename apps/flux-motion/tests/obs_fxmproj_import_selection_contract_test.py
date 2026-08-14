from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
helpers = (ROOT / "src/editor/title-dock/import-export-helpers.inc").read_text(
    encoding="utf-8"
)
actions = (ROOT / "src/editor/title-dock/title-actions.inc").read_text(
    encoding="utf-8"
)
dock_ui = (ROOT / "src/editor/title-dock/dock-ui.inc").read_text(
    encoding="utf-8"
)
locale = (ROOT / "data/locale/en-US.ini").read_text(encoding="utf-8")


assert 'suffix == QStringLiteral("fxmproj")' in helpers
assert 'suffix == QStringLiteral("fxmproj")' in dock_ui
assert 'QFileDialog::getOpenFileNames' in actions
assert 'suffix == QStringLiteral("fxmproj")' in actions
assert 'TitleDataStore::instance().read_project' in actions
assert 'select_project_titles_for_import' in actions
assert 'TitleDataStore::instance().append_project(selection)' in actions
assert 'fxmProjectImportSelectionDialog' in actions
assert 'Qt::ItemIsUserCheckable' in actions
assert 'OBSTitles.ImportProjectSelectionTitle=' in locale
assert 'OBSTitles.ImportFileFilter=' in locale
assert '*.fxmproj' in locale

print("OBS FXM project import selection contract passed")
