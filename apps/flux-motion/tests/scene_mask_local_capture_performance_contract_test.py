#!/usr/bin/env python3
"""Runtime scene masks capture only the source pixels consumed by the mask."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc"
).read_text(encoding="utf-8")


def test_capture_plan_uses_the_local_mask_raster_when_it_is_cheaper():
    plan = SOURCE.split("struct SceneMaskCapturePlan", 1)[1].split(
        "static bool apply_scene_mask_scene_transform_gs", 1
    )[0]
    assert "gpu_scene_mask_raster_id(layer.id)" in plan
    assert "source.pending_image.width()" in plan
    assert "source.pending_image.height()" in plan
    assert "cropped_pixels >= full_pixels" in plan
    assert "source.origin.x() - scene_local_x" in plan
    assert "logical_width / layout.scale_x" in plan
    assert "plan.cropped = true" in plan


def test_move_with_mask_renders_the_scene_into_the_reduced_target():
    prepare = SOURCE.split(
        "static bool prepare_runtime_scene_mask_rasters(", 1
    )[1].split("static void render_scene_masks_gpu(", 1)[0]
    assert "const SceneMaskCapturePlan capture = scene_mask_capture_plan(" in prepare
    assert (
        "target, capture.target_width, capture.target_height, clear" in prepare
    )
    assert "capture.target_width) /" in prepare
    assert "capture.scene_span_width" in prepare
    assert "-capture.scene_origin_x * capture_scale_x" in prepare
    assert prepare.find("gs_matrix_scale3f") < prepare.find(
        "obs_source_video_render(scene)"
    )
    assert "scene_width, scene_height, capture" in prepare


def test_mask_shader_remaps_cropped_capture_and_rejects_outside_scene():
    shader = SOURCE.split(
        'static constexpr const char *kRuntimeSceneMaskRasterEffect = R"(', 1
    )[1].split(')";', 1)[0]
    assert "uniform float2 sceneBounds;" in shader
    assert "uniform float2 sceneCaptureOrigin;" in shader
    assert "uniform float2 sceneCaptureSize;" in shader
    assert "(scenePx - sceneCaptureOrigin)" in shader
    assert "scenePx.x > sceneBounds.x" in shader
    assert "sceneUv.x > 1.0" in shader


if __name__ == "__main__":
    test_capture_plan_uses_the_local_mask_raster_when_it_is_cheaper()
    test_move_with_mask_renders_the_scene_into_the_reduced_target()
    test_mask_shader_remaps_cropped_capture_and_rejects_outside_scene()
    print("scene mask local capture performance contract: PASS")
