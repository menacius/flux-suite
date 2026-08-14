#!/usr/bin/env python3
"""Regression contract for scene-mask backdrop effects in Development Version 220."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc").read_text(encoding="utf-8")
changelog = (ROOT / "docs/CHANGELOG.md").read_text(encoding="utf-8")

helper_start = source.index("static bool apply_scene_mask_backdrop_effects_over_current_target(")
helper_end = source.index("static void render_scene_masks_gpu(", helper_start)
helper = source[helper_start:helper_end]
scene_start = helper_end
scene_end = source.index("static void source_video_render", scene_start)
scene = source[scene_start:scene_end]

# Scene-mask layers must classify enabled affect-behind effects explicitly.
assert "scene_mask_layer_has_backdrop_effects" in source
assert "effect.affect_layers_behind" in source
assert "eval_effect_enabled(effect, local_time)" in source

# The current destination containing lower ordinary layers and lower scene
# masks must be snapshotted and mapped into title-local coordinates.
assert "gs_texture_t *current_target = gs_get_render_target();" in helper
assert "ensure_external_background_snapshot" in helper
assert "gs_copy_texture(session->external_background_snapshot, current_target);" in helper
assert "ExternalBackgroundMapping::CurrentTransform" in helper

# Each backdrop effect is evaluated independently, uses the adjustment-only
# effect path, and is bounded by the untouched scene-mask silhouette.
assert "adjustment_layer.effects.push_back(backdrop_effect);" in helper
assert "apply_gpu_layer_effect_stack(" in helper
assert "nullptr, true, frame, true);" in helper
assert "mix_gpu_adjustment_layer(" in helper
assert "frame, adjusted, coverage, target, true" in helper

# Replace the already-composited destination only after a pass actually
# changed it. A shader failure must remain fail-open for the scene foreground.
assert "bool changed = false;" in helper
assert "return !changed || present_gpu_session_texture_locked(" in helper
assert "apply_scene_mask_backdrop_effects_over_current_target(" in scene
call_pos = scene.index("apply_scene_mask_backdrop_effects_over_current_target(")
render_pos = scene.index("obs_source_video_render(scene);")
assert call_pos < render_pos

assert "Scene-mask backdrop compositing correction" in changelog
print("Development Version 220 scene-mask backdrop compositing regression contract passed")
