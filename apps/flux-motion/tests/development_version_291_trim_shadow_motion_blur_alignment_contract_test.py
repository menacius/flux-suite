from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GPU = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc").read_text(encoding="utf-8")


def test_trim_shadow_temporal_raster_stays_one_to_one():
    assert "has_active_trim_paths" in GPU
    assert "has_legacy_shadow" in GPU
    assert "has_layer_space_shadow_effect" in GPU
    assert "raster_scale = 1.0" in GPU


def test_fix_is_narrowly_scoped_to_trim_and_shadow():
    assert "has_active_trim_paths &&" in GPU
    assert "(has_legacy_shadow || has_layer_space_shadow_effect)" in GPU
