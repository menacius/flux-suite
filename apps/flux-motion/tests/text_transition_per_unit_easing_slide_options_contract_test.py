from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
presets = (ROOT / "src/text/text-animator-presets.cpp").read_text(
    encoding="utf-8"
)
model = (ROOT / "src/transitions/layer-transition.h").read_text(
    encoding="utf-8"
)
dialog = (ROOT / "src/transitions/transition-editor-dialog.cpp").read_text(
    encoding="utf-8"
)
evaluator = (ROOT / "src/text/text-animator.cpp").read_text(encoding="utf-8")
compat = (
    ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc"
).read_text(encoding="utf-8")

# The completion track is a linear shared clock. The selected easing remains on
# unit_easing and is evaluated only after each Animate By unit's stagger delay.
selector = presets[
    presets.index("TextSelector progressive_selector") :
    presets.index("void add_transition_properties")
]
assert "selector.unit_easing = easing;" in selector
assert selector.count("key(timeline_start") == 2
assert selector.count("EasingType::Linear") == 2
assert "completion_easing" not in selector
assert "text_selector_ease(phase, selector.unit_easing)" in evaluator

# Slide/Blur Slide expose independent persisted Fade and stationary unit-crop
# options, with crop bounds derived from the selected animator granularity.
assert "bool text_slide_fade = true;" in model
assert "bool text_slide_crop_to_unit_bounds = false;" in model
assert 'new FxmSwitch(QStringLiteral("Fade")' in dialog
for unit in ("Character", "Word", "Sentence"):
    assert f'QStringLiteral("Crop in {unit} Bounds")' in dialog
assert "animator.clip_to_unit_bounds =" in presets
assert "text_animator_unit_bounds(layout, map, animator.granularity" in evaluator
assert "cluster.line_index != target_line" in evaluator
assert "cluster_index, true," in evaluator
assert "extend_clipped_position_to_hide_ink" in evaluator
assert "clip_y1 - ink_y0 + edge_guard" in evaluator
assert "line != current_line" in evaluator
assert "unit.cluster_indices" in evaluator
assert "units[lhs].byte_start < units[rhs].byte_start" in evaluator
assert "state.has_unit_clip_bounds" in compat

print("text transition per-unit easing/slide options contract: PASS")
