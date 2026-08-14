#!/usr/bin/env python3
"""Dev335: edge-on X/Y rotation and immediate Undo/Redo property rebinding."""

from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
gizmo = read("src/canvas/canvas-preview/editor-3d-tools.inc")
canvas_h = read("src/canvas/canvas-preview.h")
history = read("src/editor/title-editor/layout-template-tools.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 335
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 335
assert manifest["development_version"] >= 335

# Edge-on rings must retain a projection-independent first drag. Dev336
# supersedes the Dev335 geometric fallback with direct screen-space scrubbing.
assert "rotation_scrub_axis" in canvas_h
assert "kScrubActivationPixels" in gizmo
assert "kDegreesPerPixel" in gizmo

# Snapshot restore replaces Layer instances even when their stable IDs and the
# current selection are unchanged. Properties and Effects must explicitly bind
# to those restored objects before the event returns.
restore_start = history.index("void TitleEditor::restore_undo_snapshot")
restore_end = history.index("void TitleEditor::update_undo_redo_actions", restore_start)
restore = history[restore_start:restore_end]
assert "restore_title_authoring_snapshot(*title_, *snapshot);" in restore
assert "synchronize_layer_selection({sel_layer_id_});" in restore
assert "update_layer_panels(" in restore
assert "title_->find_layer(sel_layer_id_)" in restore
assert restore.index("restore_title_authoring_snapshot") < restore.index(
    "update_layer_panels("
)

test_name = (
    "tests/development_version_335_rotation_first_drag_undo_properties_contract_test.py"
)
for area in ("editor_gui", "rendering_2d_3d", "platform_build"):
    assert test_name in manifest["areas"][area]["python"]

print("Dev335 first-drag rotation/Undo properties contract passed")
