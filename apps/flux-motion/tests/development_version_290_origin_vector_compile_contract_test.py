from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text(encoding="utf-8")


def test_origin_prop_uses_vector_interval_probe():
    assert "animated_vec2_property_changes_during_interval(\n            layer.origin_prop" in runtime


def test_origin_prop_is_not_in_scalar_property_array():
    scalar_start = runtime.index("const AnimatedProperty *properties[] = {", runtime.index("layer_non_transform_animation_changes_during_interval"))
    scalar_end = runtime.index("};", scalar_start)
    assert "&layer.origin_prop" not in runtime[scalar_start:scalar_end]
