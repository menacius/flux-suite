#!/usr/bin/env python3
"""Source contract for Development Version 216."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def read(path: str) -> str: return (ROOT / path).read_text(encoding="utf-8")
cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
model = read("Core/layer-model.h")
data = read("src/core/title-data.cpp")
schema = read("Core/title-serialization-schema.h")
window = read("src/editor/title-editor/window-session.inc")
layout = read("src/editor/title-editor/layout-template-tools.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
transform = read("src/rendering/layer-transform-3d.cpp")
canvas = read("src/canvas/canvas-preview/geometry-selection.inc")
compositor = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
hash_source = read("src/cache/cache-manager/disk-cache-storage.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")
gpu_key = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
doc = read("docs/EDITOR_WORKFLOW.md")
changelog = read("docs/CHANGELOG.md")
readme = read("README.md")
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema
assert 'bool parent_bind_enabled = false' in model
assert 'std::array<double, 16> parent_bind_matrix' in model
assert 'j["parent_bind_enabled"]' in data and 'j["parent_bind_matrix"]' in data
assert 'parent_bind_matrix.size()' in data and 'std::isfinite(item.get<double>())' in data
assert 'editor_store_parent_bind_matrix' in window
assert 'editor_store_parent_bind_transform' in window
assert 'static bool editor_reparent_layer_with_parent_bind(' in window
bind_function = window[window.index('static bool editor_reparent_layer_with_parent_bind('):window.index('static std::vector<double> editor_reparent_sample_times(')]
for forbidden in ('add_or_replace_keyframe(', 'set_animated_value(', '.keyframes.clear()', 'editor_offset_layer_position_track'):
    assert forbidden not in bind_function
assert 'editor_layer_transform_keyframe_count(layer)' in bind_function
assert 'destination_inverse * source_basis' in window
assert 'source_basis * destination_inverse' in window
assert 'keyframesBefore=%9 keyframesAfter=%10' in window
assert layout.count('editor_reparent_layer_with_parent_bind(') >= 4
assert 'bindReparents=' in layout
assert 'sampledReparents=' not in layout
assert 'editor_capture_world_transform_track_for_parenting(' not in layout
assert 'editor_restore_world_transform_track_for_parenting(' not in layout
assert 'editor_capture_world_transform_track_for_parenting(' not in commands
assert 'parent_basis * parent_bind_matrix(layer)' in transform
assert 'parent_bind_uses_3d(layer)' in transform
assert 'editor_layer_parent_bind_transform(layer) * parent_basis' in canvas
assert 'layer_parent_bind_qt(layer) * parent_basis' in compositor
assert 'add(layer->parent_bind_enabled)' in hash_source
assert 'v39-parent-bind-matrix-3d-pipeline-completion' in cache_abi
assert 'gpu-effects-v17-keying-matte' in gpu_key
# Existing 3D composition contracts remain present.
assert 'hardware_depth_candidate' in transform
assert 'LayerEffectSpace::PostTransform' in transform
assert 'LayerEffectSpace::ScreenSpace' in transform
assert 'ordered_group_children' in transform
assert 'Keyframe-safe hierarchy' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')
assert 'parent-bind matrix' in doc
print('Development Version 216 3D pipeline completion contract passed')
