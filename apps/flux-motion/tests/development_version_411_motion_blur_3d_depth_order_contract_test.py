#!/usr/bin/env python3
"""Development 411: Motion Blur must not change a 3D plane's Z order."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/rendering/layer-transform-3d.cpp").read_text(
    encoding="utf-8"
)
HEADER = (ROOT / "src/rendering/layer-transform-3d.h").read_text(
    encoding="utf-8"
)


def test_motion_blur_is_not_a_depth_sort_barrier():
    helper = SOURCE.split(
        "bool effect_stack_has_active_non_motion_blur_space", 1
    )[1].split("bool simple_planar_3d_candidate", 1)[0]
    assert "effect.type != LayerEffectType::MotionBlur" in helper
    depth_sort = SOURCE.split("bool depth_sort_candidate", 1)[1].split(
        "std::string effective_camera_id", 1
    )[0]
    assert "effect_stack_has_active_non_motion_blur_space(" in depth_sort
    assert "LayerEffectSpace::PostTransform" in depth_sort
    assert "effect_stack_has_active_space(\n               layer, t, LayerEffectSpace::PostTransform)" not in depth_sort
    assert "Motion-blurred planes retain their" in HEADER


if __name__ == "__main__":
    test_motion_blur_is_not_a_depth_sort_barrier()
    print("Development 411 motion-blur 3D depth-order contract: PASS")
