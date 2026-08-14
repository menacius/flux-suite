from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = (root / "src/core/title-data.cpp").read_text(encoding="utf-8")
model = (root / "Core/layer-model.h").read_text(encoding="utf-8")
timeline = (root / "src/timeline/timeline-widget.cpp").read_text(encoding="utf-8")
ops = (root / "src/editor/title-editor/layout-template-tools.inc").read_text(encoding="utf-8")
stack = (root / "src/layers/layer-stack-widget.cpp").read_text(encoding="utf-8")
commands = (root / "src/editor/title-editor/commands-docks.inc").read_text(encoding="utf-8")
canvas_pointer = (root / "src/canvas/canvas-preview/pointer-events.inc").read_text(encoding="utf-8")
properties = (root / "src/editor/properties-panel/selection-refresh.inc").read_text(encoding="utf-8")
gpu = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build_info = (root / "src/core/build-info.h").read_text(encoding="utf-8")

assert "transition_input_required" in model
assert "layer.transition_input_required" in core
assert 'j["transition_input_required"]' in core
assert "It must never reset" in core
ensure_body = core.split("void ensure_stinger_transition_input_layers", 1)[1].split("StingerValidationResult", 1)[0]
assert "input_layer->in_time =" not in ensure_body
assert "input_layer->out_time =" not in ensure_body
assert "parent_id.clear" not in ensure_body
assert "input_layer->transition_input_required = true" in ensure_body

# Required A/B must not be special-cased out of trim, move or transition editing.
for forbidden in [
    "fixed_transition_input",
    "stinger_transition_input_layer_is_protected(*rows[row].layer)",
    "stinger_transition_input_layer_is_protected(*layer)) continue",
]:
    assert forbidden not in timeline
assert "begin_layer_strip_drag(layer->id, DragMode::TrimIn" in timeline
assert "begin_layer_strip_drag(layer->id, DragMode::TrimOut" in timeline
assert "begin_layer_strip_drag(layer->id, DragMode::Layer" in timeline
assert "transition_edge_target_at_pos" in timeline

# Ordinary layer operations are available; only deletion filters required inputs.
assert "clone->transition_input_required = false" in ops
assert "clone->transition_input_required = false" in canvas_pointer
assert "group_selected_layers" in ops
assert "stinger_transition_input_layer_is_protected(*layer) ||" not in ops
assert "return layer && stinger_transition_input_layer_is_protected(*layer);" in ops
assert "name->setReadOnly(l->locked);" in stack
assert "if (stinger_transition_input_layer_is_protected(*layer))" not in commands
assert "structural_selection_count" in stack
assert "group_layers->setEnabled(structural_selection_count >= 2);" in stack
assert "is_transition_input ||" in properties
assert "layer.type == LayerType::TransitionInput" in gpu

# First creation is canvas-sized and full-duration.
assert "layer.in_time = 0.0;" in core
assert "layer.out_time = std::max(0.001, title.duration);" in core
assert "layer.size.static_value = {canvas_width, canvas_height};" in core
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info

print("Full manual Stinger Scene A/B visual-layer contract passed")
