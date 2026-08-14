#!/usr/bin/env python3
"""Dev345: unified import and editable SVG/PSD rich-text contract."""
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

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 345
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 345
assert manifest["development_version"] >= 345

assert 'addMenu(fxm_tr("OBSTitles.Import"))' not in menu
for token in ['OBSTitles.ImportUnified', '&TitleEditor::import_document']:
    assert token in menu, token
for token in ['void import_document();', 'void import_vector_document(const QString &path);',
              'void import_gimp_document(const QString &path);',
              'void import_photoshop_document(const QString &path);']:
    assert token in header, token

for token in [
    'QImageReader::supportedImageFormats()', 'unified_import_filter',
    '*.fxmp', '*.fxmt', '*.json', '*.svg', '*.psd', '*.xcf',
    'create_image_layer_from_external_source', 'TitleDataStore::instance().import_title',
    'import_vector_document(path)', 'import_photoshop_document(path)',
    'import_gimp_document(path)',
]:
    assert token in source, token

for token in [
    'struct SvgTextRun', 'read_svg_text_node', 'text_runs', 'RichTextRange',
    'RichTextCharAll', 'svg_rich_text_format', 'svg_rich_text_fill',
    'font-family', 'font-size', 'font-weight', 'font-style', 'font-stretch',
    'text-decoration', 'letter-spacing', 'baseline-shift', 'text-anchor',
    'stroke-linejoin', 'stroke_gradient_stops', 'fill_gradient_stops',
    'svg_register_embedded_fonts', '@font-face',
    'psd_text_engine_data', 'EngineData', 'RunLengthArray', 'StyleSheetData',
    'FillColor', 'StrokeColor', 'psd_rich_text',
]:
    assert token in source, token

for key in [
    'OBSTitles.ImportUnified=', 'OBSTitles.ImportAllSupported=',
    'OBSTitles.ImportFxmTitles=', 'OBSTitles.ImportDocuments=',
    'OBSTitles.ImportImages=',
]:
    assert key in locale, key

print("Dev345 unified import and rich-text contract passed")
