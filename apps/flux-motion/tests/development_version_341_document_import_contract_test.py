#!/usr/bin/env python3
"""Dev341: vector, GIMP and Photoshop document import contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
menu = (ROOT / "src/editor/title-editor/panels-colors.inc").read_text(encoding="utf-8")
header = (ROOT / "src/editor/title-editor.h").read_text(encoding="utf-8")
source = (ROOT / "src/editor/title-editor/import-documents.inc").read_text(encoding="utf-8")
locale = (ROOT / "data/locale/en-US.ini").read_text(encoding="utf-8")
manifest = json.loads((ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 341
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 341
assert manifest["development_version"] >= 341

for token in [
    'OBSTitles.ImportUnified', '&TitleEditor::import_document',
]:
    assert token in menu, token

for declaration in [
    'void import_document();',
    'void import_vector_document();', 'void import_gimp_document();',
    'void import_photoshop_document();',
]:
    assert declaration in header, declaration

for token in [
    'class SvgPathReader', 'QPainterPath', 'painter_path_points',
    'shapes.size() > 1', 'LayerType::Shape', 'LayerType::Group',
    'read_psd', 'decode_psd_channel', 'read_xcf', 'decode_xcf_rle',
    'ImportMergedBitmap', 'ImportSeparateLayers',
    'LayerType::Text', 'LayerType::ColorSolid', 'LayerType::Shape',
    'LayerType::Adjustment', 'LayerType::Image', 'psd_vector_mask',
    'source.visible', 'source.opacity', 'source.blend', 'source.parent',
    'QStandardPaths::AppDataLocation', 'QSaveFile',
]:
    assert token in source, token

for key in [
    'OBSTitles.Import="Import"', 'OBSTitles.ImportVector="Vector..."',
    'OBSTitles.ImportGimp="GIMP..."', 'OBSTitles.ImportPhotoshop="Photoshop..."',
    'OBSTitles.ImportMergedBitmap=', 'OBSTitles.ImportSeparateLayers=',
]:
    assert key in locale, key

print("Dev341 document import contract passed")
