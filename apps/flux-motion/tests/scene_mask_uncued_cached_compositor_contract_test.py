#!/usr/bin/env python3
"""Uncued scene masks stay on the reusable final-frame compositor path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc"
).read_text(encoding="utf-8")


def test_invalid_cue_clears_a_previous_runtime_mask_once():
    clear = SOURCE.split(
        "static void clear_runtime_scene_mask_rasters(", 1
    )[1].split("static bool set_runtime_scene_mask_raster(", 1)[0]
    assert "runtime_scene_mask_rasters.empty()" in clear
    assert clear.find("runtime_scene_mask_rasters.empty()") < clear.find(
        "session->frame_dirty = true"
    )

    prepare = SOURCE.split(
        "static bool prepare_runtime_scene_mask_rasters(", 1
    )[1].split("static void render_scene_masks_gpu(", 1)[0]
    invalid = prepare.split(
        "if (!title_has_valid_scene_mask_cue(data, title))", 1
    )[1].split("if (data->active_scene_mask_scenes.empty()", 1)[0]
    assert "clear_runtime_scene_mask_rasters(data->gpu_render_session);" in invalid
    assert "return false;" in invalid


def test_uncued_render_does_not_enter_the_partial_layer_range_path():
    render = SOURCE.split("static void source_video_render(", 1)[1].split(
        "static void add_scene_list_items", 1
    )[0]
    assert "const bool scene_mask_cue_valid" in render
    assert "title_has_valid_scene_mask_cue(data, *title)" in render
    assert "scene_mask_cue_valid &&" in render
    loop = render.split("std::size_t first_scene_mask", 1)[1].split(
        "if (scene_masks_in_layer_pipeline)", 1
    )[0]
    assert "scene_mask_cue_valid &&" in loop
    assert "draw_gpu_layer_range_over_current_target" in render
    assert "title_gpu_render_session_draw(data->gpu_render_session" in render


if __name__ == "__main__":
    test_invalid_cue_clears_a_previous_runtime_mask_once()
    test_uncued_render_does_not_enter_the_partial_layer_range_path()
    print("scene mask uncued cached compositor contract: PASS")
