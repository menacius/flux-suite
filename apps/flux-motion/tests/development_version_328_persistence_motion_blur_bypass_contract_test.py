from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_live_gpu_motion_blur_collapses_for_persistent_layers_only():
    source = (
        ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc"
    ).read_text(encoding="utf-8")
    render = source.split("static bool render_gpu_layer_to_target", 1)[1]
    render = render.split("static bool render_gpu_mask_graph_texture", 1)[0]

    assert "const bool persistence_motion_bypass = motion_config &&" in render
    assert "gpu_layer_uses_cue_persistence(title, layer)" in render
    assert "if (!persistence_motion_bypass && motion_config" in render
    assert "stage=motion-blur-persistence-bypass" in render


def test_cache_compatibility_path_matches_live_persistence_semantics():
    source = (
        ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc"
    ).read_text(encoding="utf-8")
    motion = source.split("static bool render_motion_blurred_layer", 1)[1]
    motion = motion.split("static bool render_layer_with_gpu_effects", 1)[0]

    assert "gpu_layer_uses_cue_persistence(title, layer)" in motion
    assert "return false;" in motion


def test_previous_persistence_clock_fix_is_retained():
    runtime = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text(
        encoding="utf-8"
    )
    assert "layer_asset_resolved_time(title, layer, root_time)" in runtime
    assert "const double root_time = gpu_layer_uses_cue_persistence" in runtime
