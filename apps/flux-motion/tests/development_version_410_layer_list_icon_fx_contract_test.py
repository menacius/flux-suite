from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STACK = (ROOT / "src/layers/layer-stack-widget.cpp").read_text(encoding="utf-8")


# Row and header icons use the larger common metric while the compact 28 px
# list-row height remains unchanged.
assert "constexpr int kLayerRowIconExtent = 18;" in STACK
assert STACK.count("QSize(kLayerRowIconExtent, kLayerRowIconExtent)") >= 8
assert "constexpr int kLayerTypeWidth = 22;" in STACK
assert "item->setSizeHint(QSize(0, 28));" in STACK
assert "class LayerRowIconButton final : public QToolButton" in STACK
assert "style()->drawComplexControl(QStyle::CC_ToolButton" in STACK
assert "glyph.paint(&painter, icon_rect" in STACK
assert STACK.count("new LayerRowIconButton(row_widget)") >= 7

# FX is present only for a real effect stack. Its local padding/height cannot
# collapse the text under the host stylesheet when it is active.
assert ': (has_external_binding ? QStringLiteral("D") : QString()));' in STACK
assert "fx_indicator->setFixedSize(kLayerFxWidth, 20);" in STACK
assert 'font-size:10px;font-weight:bold;padding:0;' in STACK
assert 'layer_indicator_tip = fxm_tr("OBSTitles.NoEffectsAdded");' in STACK

print("Development Version 410 layer-list icon/FX contract: PASS")
