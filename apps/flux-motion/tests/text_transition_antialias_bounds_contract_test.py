from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
gpu_routing = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc"
).read_text(encoding="utf-8")
compatibility = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc"
).read_text(encoding="utf-8")
layer_raster = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc"
).read_text(encoding="utf-8")

routing = gpu_routing[
    gpu_routing.index("static bool layer_can_use_gpu_text_raster") :
    gpu_routing.index("static bool prepare_gpu_text_raster")
]

# Timeline transitions are the managed subset of text animators. They must use
# the exact isolated-unit raster rather than the visibly aliased SDF path.
assert "animator.enabled && animator.transition_managed" in routing
assert "if (has_managed_text_transition)\n        return false;" in routing

# Preserve the existing crop contract: bounds come from actually painted alpha
# and retain their transparent transform gutter. No advance/glyph-bound growth
# is introduced to hide aliasing.
assert "transition_image_alpha_bounds(unit_image)" in compatibility
assert "constexpr int kTextAnimatorResamplingGutter = 4;" in compatibility
assert "exact_text_transition_unit_render_bounds(" in compatibility
assert "apply_unified_text_animator_raster(" in layer_raster

print("text transition antialias/bounds contract: PASS")
