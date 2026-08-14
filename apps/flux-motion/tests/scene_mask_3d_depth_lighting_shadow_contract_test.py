#!/usr/bin/env python3
"""Scene-mask OBS scenes participate in the normal 3D layer pipeline."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SESSION = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc"
).read_text(encoding="utf-8")
LIFECYCLE = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc"
).read_text(encoding="utf-8")
PRESENTATION = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc"
).read_text(encoding="utf-8")
SOURCE = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc"
).read_text(encoding="utf-8")


def test_runtime_scene_replaces_the_scene_mask_layer_raster():
    helper = SESSION.split(
        "static TitleGpuRenderSession::LayerRaster *gpu_session_layer_raster(", 1
    )[1].split("static ResolvedLayerEffect", 1)[0]
    assert "layer.use_as_scene_mask" in helper
    assert "session->runtime_scene_mask_rasters.find(layer.id)" in helper
    assert "return &runtime->second.raster;" in helper

    renderer = PRESENTATION.split(
        "static bool render_gpu_layer_to_target(", 1
    )[1].split("\nstatic ", 1)[0]
    assert "gpu_session_layer_raster(session, layer)" in renderer


def test_runtime_scene_mask_enters_depth_and_shadow_enumeration():
    assert LIFECYCLE.count(
        "gpu_session_layer_raster(session,"
    ) >= 6
    visibility = LIFECYCLE.split(
        "static bool layer_should_render_as_visible_content_for_gpu_session(", 1
    )[1].split("\nstatic ", 1)[0]
    assert "session->runtime_scene_mask_rasters.find(layer.id)" in visibility

    shadow_region = LIFECYCLE.split(
        "static bool render_gpu_shadow_map(", 1
    )[1].split("\nstatic ", 1)[0]
    assert "gpu_session_layer_raster(session, *layer)" in shadow_region


def test_source_uses_one_normal_stack_draw_for_planar_scene_masks():
    prepare = SOURCE.split(
        "static bool prepare_runtime_scene_mask_rasters(", 1
    )[1].split("static void render_scene_masks_gpu(", 1)[0]
    assert "title_gpu_render_session_prepare_auxiliary_layers(" in prepare
    assert "set_runtime_scene_mask_raster(" in prepare
    assert "!cfg->move_with_mask" in prepare

    render = SOURCE.split("static void source_video_render(", 1)[1].split(
        "\nstatic void add_scene_list_items(", 1
    )[0]
    assert "scene_masks_in_layer_pipeline" in render
    integrated = render.split("if (scene_masks_in_layer_pipeline) {", 1)[1]
    assert "title_gpu_render_session_draw(" in integrated
    assert "title_gpu_render_session_draw_over_current_target(" in integrated
    assert "if (!scene_masks_in_layer_pipeline)" in render
    assert "render_scene_masks_gpu(data, *title, data->playhead);" in render


if __name__ == "__main__":
    test_runtime_scene_replaces_the_scene_mask_layer_raster()
    test_runtime_scene_mask_enters_depth_and_shadow_enumeration()
    test_source_uses_one_normal_stack_draw_for_planar_scene_masks()
    print("3D scene-mask depth, lighting and shadow contract: PASS")
