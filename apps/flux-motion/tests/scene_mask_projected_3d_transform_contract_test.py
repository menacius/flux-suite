#!/usr/bin/env python3
"""3D scene masks map the real OBS scene rectangle through the mask plane."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc"
).read_text(encoding="utf-8")


def test_scene_mask_scene_uses_anchor_stable_3d_projection():
    helper = SOURCE.split(
        "static bool apply_scene_mask_scene_transform_gs(", 1
    )[1].split("static constexpr const char *kRuntimeSceneMaskRasterEffect", 1)[0]
    assert "projected_local_quad_transform(" in helper
    assert "gpu_qtransform_to_gs_matrix(scene_to_canvas)" in helper
    assert "const double source_x = (box_x - local_x) / layout.scale_x;" in helper
    assert "const double source_width = box_width / layout.scale_x;" in helper
    assert "const double source_height = box_height / layout.scale_y;" in helper
    assert "QPointF(box_x + box_width, box_y + box_height)" in helper
    assert "obs_source_get_width(scene)" not in helper


def test_legacy_and_fixed_screen_mappings_remain_available():
    helper = SOURCE.split(
        "static bool apply_scene_mask_scene_transform_gs(", 1
    )[1].split("static constexpr const char *kRuntimeSceneMaskRasterEffect", 1)[0]
    render = SOURCE.split("static void render_scene_masks_gpu(", 1)[1]
    assert "apply_layer_world_transform_gs(title, layer, title_time);" in helper
    assert "apply_scene_mask_scene_transform_gs(" in render
    fixed = render.split("} else {", 1)[1]
    assert "gs_matrix_translate3f" in fixed
    assert "gs_matrix_scale3f" in fixed


def test_runtime_recovers_missing_active_scene_and_auxiliary_mask():
    render = SOURCE.split("static void render_scene_masks_gpu(", 1)[1]
    assert "title_has_valid_scene_mask_cue(data, title)" in render
    assert "activate_scene_mask_scenes(data);" in render
    assert "title_gpu_render_session_prepare_auxiliary_layers(" in render
    assert render.count(
        "title_gpu_render_session_render_auxiliary_layer("
    ) >= 2


if __name__ == "__main__":
    test_scene_mask_scene_uses_anchor_stable_3d_projection()
    test_legacy_and_fixed_screen_mappings_remain_available()
    test_runtime_recovers_missing_active_scene_and_auxiliary_mask()
    print("3D scene mask projected transform contract: PASS")
