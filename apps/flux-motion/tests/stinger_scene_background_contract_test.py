from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

model_h = read("Core/title-data.h")
model_cpp = read("src/core/title-data.cpp")
panel_h = read("src/editor/title-properties-panel.h")
panel_cpp = read("src/editor/title-properties-panel.cpp")
commands = read("src/editor/title-editor/commands-docks.inc")
canvas = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
raster = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")

assert "CanvasTransparency = 2" in model_h
assert "FollowSwitchPoint = 3" in model_h
assert "StingerEditorBackground::FollowSwitchPoint" in model_h
assert "v168-v169 stored a static Scene A or Scene B choice" in model_cpp
assert "cmb_stinger_editor_background_" not in panel_h + panel_cpp
assert 'add_checkerboard_action("Scene A/B", 6)' in commands
assert "stinger_scene_background_action->setVisible(point_switch)" in commands
assert "title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint" in commands
assert "StingerEditorBackground::CanvasTransparency" in commands
assert "StingerEditorBackground::FollowSwitchPoint" in commands
assert "playhead_ + 1e-9 >=" in canvas
assert "stinger_transition_point_seconds(*title_)" in canvas
assert 'scene_b ? QStringLiteral("B")' in canvas
assert 'scene_b ? "B" : "A"' in raster
assert "transition_input_textures[layer.transition_input_slot]" in gpu
assert "apply_gpu_layer_effect_stack" in gpu
print("Stinger Scene A/B canvas and layer-surface contract passed")
