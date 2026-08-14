#!/usr/bin/env python3
"""Dev342: the importer must begin at true file scope in the editor TU."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
editor = (ROOT / "src/editor/title-editor.cpp").read_text(encoding="utf-8")
ui = (ROOT / "src/editor/title-editor/ui-construction.inc").read_text(encoding="utf-8")
panels = (ROOT / "src/editor/title-editor/panels-colors.inc").read_text(encoding="utf-8")
importer = (ROOT / "src/editor/title-editor/import-documents.inc").read_text(encoding="utf-8")
manifest = json.loads((ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 342
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 342
assert manifest["development_version"] >= 342

ui_include = editor.index('#include "title-editor/ui-construction.inc"')
panels_include = editor.index('#include "title-editor/panels-colors.inc"')
import_include = editor.index('#include "title-editor/import-documents.inc"')
commands_include = editor.index('#include "title-editor/commands-docks.inc"')
events_include = editor.index('#include "title-editor/editor-events.inc"')
assert ui_include < panels_include < commands_include < events_include < import_include

# These two modules intentionally form one contiguous method implementation.
assert not ui.rstrip().endswith("}")
assert panels.lstrip().startswith("act_timeline_visible_")
assert 'namespace fxm_document_import {' in importer
assert 'constexpr double kPi' in importer
assert 'local.shear(std::tan(n[0] * kPi / 180.0), 0.0)' in importer
assert 'local.shear(0.0, std::tan(n[0] * kPi / 180.0))' in importer

print("Dev342 document importer MSVC file-scope contract passed")
