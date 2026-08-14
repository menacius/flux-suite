from pathlib import Path
root = Path(__file__).resolve().parents[1]
dock = (root / "src/editor/title-dock/dock-lifecycle.inc").read_text(encoding="utf-8")
effects = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc").read_text(encoding="utf-8")
assert "cue->setEnabled(title_has_bound_obs_source(title->id));" in dock
assert "QTimer::singleShot(75" in dock
assert "max_rich_text_stroke_width(layer, t)" in effects
assert "const ShadowRenderParams shadow = evaluated_shadow_params(layer, t);" in effects
assert "bounds.translated(shadow.dx, shadow.dy)" in effects
assert "shadow.long_length" in effects
print("dock source binding and effect bounds contract: ok")
