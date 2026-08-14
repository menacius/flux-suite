#!/usr/bin/env python3
"""3D scene-mask editor placeholders bypass material/depth suppression."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc"
).read_text(encoding="utf-8")


def test_3d_scene_mask_placeholder_uses_projected_raster_path():
    helper = SOURCE.split(
        "static bool hardware_depth_candidate_for_gpu_session(", 1
    )[1].split("static std::string gpu_mask_texture_key(", 1)[0]
    assert "session->scene_mask_placeholder_preview_enabled" in helper
    assert "layer.use_as_scene_mask" in helper
    assert "return false;" in helper
    assert SOURCE.count("hardware_depth_candidate_for_gpu_session(") == 5


if __name__ == "__main__":
    test_3d_scene_mask_placeholder_uses_projected_raster_path()
    print("3D scene-mask editor placeholder contract: PASS")
