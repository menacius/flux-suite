#!/usr/bin/env python3
"""Dev343: no new file-scope module may split the editor's chained .inc scopes."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
editor = (ROOT / "src/editor/title-editor.cpp").read_text(encoding="utf-8")
manifest = json.loads((ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 343
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 343
assert manifest["development_version"] >= 343

chain = [
    "window-session.inc",
    "ui-construction.inc",
    "panels-colors.inc",
    "commands-docks.inc",
    "document-shape-editing.inc",
    "playback-cache-preferences.inc",
    "layout-template-tools.inc",
    "editor-audio-preview.inc",
    "signal-handlers.inc",
    "editor-events.inc",
    "import-documents.inc",
]
positions = [editor.index(f'#include "title-editor/{name}"') for name in chain]
assert positions == sorted(positions)
assert editor.rstrip().endswith('#include "title-editor/import-documents.inc"')

expanded = editor
for name in chain:
    expanded = expanded.replace(
        f'#include "title-editor/{name}"',
        (ROOT / "src/editor/title-editor" / name).read_text(encoding="utf-8"),
    )
marker = expanded.index("namespace fxm_document_import {")
depth = 0
state = "code"
quote = ""
i = 0
while i < marker:
    c = expanded[i]
    following = expanded[i + 1] if i + 1 < marker else ""
    if state == "line":
        if c == "\n": state = "code"
    elif state == "block":
        if c == "*" and following == "/": state = "code"; i += 1
    elif state == "string":
        if c == "\\": i += 1
        elif c == quote: state = "code"
    elif c == "/" and following == "/": state = "line"; i += 1
    elif c == "/" and following == "*": state = "block"; i += 1
    elif c in ('"', "'"): state = "string"; quote = c
    elif c == "{": depth += 1
    elif c == "}": depth -= 1
    i += 1
assert depth == 0, f"importer namespace begins at brace depth {depth}, not file scope"

commands = (ROOT / "src/editor/title-editor/commands-docks.inc").read_text(encoding="utf-8")
assert commands.lstrip().startswith("act_canvas_border_visible_")
assert "view_menu" in commands and "register_editor_shortcut" in commands

print("Dev343 complete editor include-chain contract passed")
