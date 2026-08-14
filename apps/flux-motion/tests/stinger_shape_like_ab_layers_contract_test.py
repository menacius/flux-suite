from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding="utf-8")

layer_model = read("Core/layer-model.h")
core = read("src/core/title-data.cpp")
canvas_model = read("src/canvas/canvas-preview/preview-cache-view.inc")
canvas_geometry = read("src/canvas/canvas-preview/geometry-selection.inc")
properties = read("src/editor/properties-panel/selection-refresh.inc")
property_actions = read("src/editor/properties-panel/auto-style-and-property-actions.inc")
session_update = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
mask_graph = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
timeline = read("src/timeline/timeline-widget.cpp")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")

assert "layer_type_uses_shape_geometry" in layer_model
assert "type == LayerType::TransitionInput" in layer_model
assert "return layer_type_uses_shape_geometry(layer.type);" in canvas_model
assert "const bool is_shape_geometry = layer_type_uses_shape_geometry" in properties
assert "const bool show_transform_size = is_shape_geometry" in properties
assert "const bool show_transform_scale = !is_shape_geometry" in properties
assert "layer_type_uses_shape_geometry(layer_->type)" in property_actions

assert "layer.lock_aspect_ratio = false" in core
assert "layer.shape_type = ShapeType::Rectangle" in core
assert "layer.size.static_value = {canvas_width, canvas_height}" in core
assert "layer.scale.static_value = {1.0, 1.0, 1.0}" in core
assert "layer.in_time = 0.0" in core
assert "layer.out_time = std::max(0.001, title.duration)" in core

assert "!session->transition_input_preview_enabled" in session_update
assert "Do not manufacture a CPU" in session_update
assert "entry.logical_width = current_box_width" in session_update
assert "|transition-input-preview=1" in session_update

assert "draw_logical_width = draw_base_box_width" in gpu
assert "draw_logical_height = draw_base_box_height" in gpu
assert "input != entry.texture" in gpu
assert "entry.layer_box_rect = QRectF(0.0, 0.0, draw_logical_width" in gpu
assert "runtime_transition_input" in mask_graph
assert "|runtime-input=" in mask_graph

assert "layer_is_editor_visible" in canvas_geometry
assert "StingerSwitchMode::ManualSceneAnimation" in canvas_geometry
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info

print("Shape-like Stinger Scene A/B layer contract passed")
