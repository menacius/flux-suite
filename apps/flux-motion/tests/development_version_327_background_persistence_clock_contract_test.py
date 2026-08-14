from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_asset_descendants_receive_the_persistent_root_clock():
    runtime = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text(
        encoding="utf-8"
    )
    cache = (
        ROOT / "src/cache/cache-manager/visual-hash-keying.inc"
    ).read_text(encoding="utf-8")

    persistence = runtime.split("static bool gpu_layer_uses_cue_persistence", 1)[1]
    persistence = persistence.split("static double gpu_layer_render_time", 1)[0]
    render_time = runtime.split("static double gpu_layer_render_time", 1)[1]
    render_time = render_time.split("static double gpu_layer_transition_time", 1)[0]

    assert "!layer.asset_owner_id.empty()" not in persistence
    assert "const double root_time = gpu_layer_uses_cue_persistence" in render_time
    assert "layer_asset_resolved_time(title, layer, root_time)" in render_time
    transition_time = runtime.split("static double gpu_layer_transition_time", 1)[1]
    transition_time = transition_time.split("static bool gpu_layer_chain_visible", 1)[0]
    assert "gpu_layer_uses_cue_persistence(title, layer)" in transition_time
    assert "layer_asset_resolved_time(title, layer, root_time)" in transition_time
    assert "!layer.asset_owner_id.empty()" not in cache.split(
        "static bool cache_layer_uses_cue_persistence", 1
    )[1].split("static double cache_layer_sample_time", 1)[0]


def test_gpu_transform_uses_the_same_resolved_clock_as_raster_and_transition():
    source = (
        ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc"
    ).read_text(encoding="utf-8")
    draw = source.split("static bool draw_gpu_layer_texture", 1)[1]
    draw = draw.split("static bool render_gpu_layer_to_target", 1)[0]

    assert "const double resolved_time = gpu_layer_render_time" in draw
    assert "title, layer, resolved_time, texture_quad" in draw
    assert "camera_render_state(\n                title, resolved_time" in draw
    assert "layer_world_matrix(title, layer, resolved_time)" in draw
    assert "apply_layer_world_transform_gs(title, layer, resolved_time, 0)" in draw
    assert "persistenceLayers=%19 persistenceAssetLayers=%20" in (
        ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc"
    ).read_text(encoding="utf-8")
