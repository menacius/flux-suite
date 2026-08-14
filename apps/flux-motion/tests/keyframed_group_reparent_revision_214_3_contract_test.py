#!/usr/bin/env python3
"""Regression contract for Development Version 214.3 keyframed grouping."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


window = read("src/editor/title-editor/window-session.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
types = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/EDITOR_WORKFLOW.md")

# Development Version 216 supersedes sampled fallback with a static bind.
assert "parent_bind_enabled" in window
assert "editor_store_parent_bind_matrix" in window
assert "editor_store_parent_bind_transform" in window
assert "destination_inverse * source_basis" in window
assert "source_basis * destination_inverse" in window
assert "keyframesBefore=%9 keyframesAfter=%10" in window
assert "editor_capture_world_transform_track_for_parenting(" not in read("src/editor/title-editor/layout-template-tools.inc")
assert "editor_restore_world_transform_track_for_parenting(" not in read("src/editor/title-editor/layout-template-tools.inc")

# Slow group logs identify likely keyframe explosions directly.
assert "slow_frames" in types
assert "max_child_transform_keyframes" in types
assert "slowFrames=%15" in session
assert "maxChildTransformKeys=%16" in session
assert 'maxChildName=\\"%18\\"' in session

assert "keyframe count" in readme or "keyframe-safe" in readme
assert "Development Version 214.3 — Keyframed Group Reparent Fix" in changelog
assert "keyframe count unchanged" in note
assert "It is serialized" in note

print("Development Version 214.3 keyframed group reparent contract passed")
