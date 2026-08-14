from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

model_h = read("Core/title-data.h")
model_cpp = read("src/core/title-data.cpp")
layer_h = read("Core/layer-model.h")
panel_cpp = read("src/editor/title-properties-panel.cpp")
panel_h = read("src/editor/title-properties-panel.h")
commands = read("src/editor/title-editor/commands-docks.inc")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
canvas = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
editor_ops = read("src/editor/title-editor/layout-template-tools.inc")
layer_stack = read("src/layers/layer-stack-widget.cpp")
cache_policy = read("src/cache/title-cache-policy.h")
cache_hash = read("src/cache/cache-manager/disk-cache-storage.inc")
raster = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
session_update = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
title_source_h = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
gpu_session_model = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
mask_lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
transition = read("../../plugins/obs/flux-motion/src/stinger-transition.cpp")
editor_lifecycle = read("src/editor/title-editor/playback-cache-preferences.inc")
locale = read("data/locale/en-US.ini")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")

for token in (
    "enum class StingerSwitchMode", "SwitchAtPoint", "ManualSceneAnimation",
    "enum class StingerEditorBackground", "SceneA", "SceneB",
    "stinger_switch_mode", "stinger_editor_background",
    "ensure_stinger_transition_input_layers",
):
    assert token in model_h, token

for token in (
    "TransitionInput = 11", "transition_input_slot",
    "stinger_transition_input_layer_is_protected",
    'layer.name = slot == 0 ? "Scene A" : "Scene B"',
    'jt["stinger_switch_mode"]', 'jt["stinger_editor_background"]',
    'j["transition_input_slot"]',
):
    assert token in layer_h + model_cpp, token

for token in (
    "cmb_stinger_switch_mode_",
    "OBSTitles.StingerSwitchAtPoint", "OBSTitles.StingerManualSceneAnimation",
    "StingerEditorBackground::FollowSwitchPoint",
    "ensure_stinger_transition_input_layers(*title_)",
):
    assert token in panel_cpp, token
assert "cmb_stinger_editor_background_" not in panel_cpp + panel_h

assert "stinger_structure_changed" in panel_h + panel_cpp + commands
assert "stinger_editor_preview_changed" in panel_h + panel_cpp + commands
assert "show_transition_inputs" in hierarchy
assert "ManualSceneAnimation" in hierarchy
assert "stinger_transition_point_seconds(*title_)" in canvas
assert "StingerEditorBackground::CanvasTransparency" in canvas
assert 'add_checkerboard_action("Scene A/B", 6)' in commands
assert "stinger_scene_background_action->setVisible(point_switch)" in commands
assert "StingerEditorBackground::FollowSwitchPoint" in commands
assert "StingerEditorBackground::CanvasTransparency" in commands
assert "CanvasPaintPass::Underlay" in canvas
assert "stinger_transition_input_layer_is_protected" in editor_ops
assert "ensure_stinger_transition_input_layers(*title_)" in editor_lifecycle
assert "LayerType::TransitionInput" in cache_policy
assert "add((int)title.stinger_switch_mode)" in cache_hash
assert "add(layer->transition_input_slot)" in cache_hash
assert "render_transition_input_placeholder" in raster
assert "input != entry.texture" in gpu
assert "draw_logical_width = draw_base_box_width" in gpu
assert "apply_gpu_layer_effect_stack(" in gpu
assert "session, layer, resolved_time, source_texture" in gpu
assert "transition_input_textures[layer.transition_input_slot]" in gpu
assert "title_gpu_render_session_draw_transition_inputs" in session
assert "output_width, output_height, true" in session
assert "layer.type != LayerType::TransitionInput" in session_update
assert "title_gpu_render_session_set_transition_input_preview" in title_source_h + session_update + canvas
assert "transition_input_preview_enabled" in gpu_session_model + gpu
assert "Normal\n         * OBS title sources keep these layers transparent" in canvas
assert "transition_input_generation" in session + gpu + mask_lifecycle
assert "|runtime-input=" in mask_lifecycle
assert "obs_transition_video_render(data->source" in transition
assert "manual_transition_render_callback" in transition
assert "title_gpu_render_session_draw_transition_inputs" in transition
assert "point <= 1.0e-9" in model_cpp
assert "OBSTitles.StingerSwitchMode=" in locale
assert "OBSTitles.StingerTransitionInputProtected=" in locale
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info

print("Stinger switch modes and manual Scene A/B contract passed")
