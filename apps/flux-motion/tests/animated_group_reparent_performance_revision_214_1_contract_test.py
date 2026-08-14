#!/usr/bin/env python3
"""Regression contract for 214.1 plus the 216 no-bake successor."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def read(path: str) -> str: return (ROOT / path).read_text(encoding="utf-8")
window = read("src/editor/title-editor/window-session.inc")
layout = read("src/editor/title-editor/layout-template-tools.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/EDITOR_WORKFLOW.md")
assert "static bool editor_reparent_layer_with_parent_bind" in window
assert "editor_store_parent_bind_matrix" in window
assert "editor_store_parent_bind_transform" in window
assert "keyframesBefore=%9 keyframesAfter=%10" in window
assert layout.count("editor_reparent_layer_with_parent_bind(") >= 4
assert "editor_reparent_layer_with_parent_bind(" in commands
assert "duration * project_fps" not in window
assert "kMaximumBakedSamples = 12000" not in window
assert "editor_capture_world_transform_track_for_parenting(" not in layout
assert "editor_restore_world_transform_track_for_parenting(" not in layout
assert "editor_capture_world_transform_track_for_parenting(" not in commands
assert "static parent-bind matrix" in readme
assert "Development Version 214.1 — Animated Group Reparent Performance Fix" in changelog
assert "does not generate a keyframe for every project frame" in note
print("Development Version 214.1/216 reparent performance contract passed")
