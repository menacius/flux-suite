from pathlib import Path
root = Path(__file__).resolve().parents[1]
runtime = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text()
playback = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc").read_text()
assert "manual_uncue_floor" in runtime
assert "data->playhead < data->manual_uncue_floor" in playback
assert "gpu_layer_transition_time" in runtime
transition_clock = runtime.split("static double gpu_layer_transition_time", 1)[1].split("static bool gpu_layer_chain_visible", 1)[0]
assert "gpu_layer_uses_cue_persistence(title, layer)" in transition_clock
assert "cue_persistence_hold_time(title)" in transition_clock
assert "return title_time;" not in transition_clock
for rel in [
    "../../packages/flux-common/Shared/rendering-engine/title-source/scene-masks-properties.inc",
    "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc",
    "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc",
]:
    assert "gpu_layer_transition_time(title, layer, title_time)" in (root / rel).read_text()
print("manual uncue and persistence transition contract: OK")
