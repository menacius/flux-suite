from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
construction = (
    ROOT / "src/editor/properties-panel/popup-state.inc"
).read_text(encoding="utf-8")
refresh = (
    ROOT / "src/editor/properties-panel/selection-refresh.inc"
).read_text(encoding="utf-8")

# Falloff Start is derived and has no layout row. Since mk_dspin() initially
# parents controls to the panel, it must be hidden to avoid appearing at (0, 0).
falloff_start = construction[
    construction.index("spn_light_falloff_start_ = mk_dspin") :
    construction.index("spn_light_falloff_distance_ = mk_dspin")
]
assert "spn_light_falloff_start_->hide();" in falloff_start

# Stroke Width is a real Appearance property and belongs in the Stroke row.
assert (
    "btn_appearance_stroke_color_, spn_appearance_stroke_width_,\n"
    "                       btn_appearance_stroke_label_"
) in construction
assert "spn_appearance_stroke_width_->setVisible(supports_outline);" in refresh

print("properties panel hidden falloff-start contract: PASS")
