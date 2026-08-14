from pathlib import Path
root=Path(__file__).resolve().parents[1]
for name in ["Sharpen.fxmeffect","Unsharp Mask.fxmeffect","High Pass.fxmeffect","Clarity.fxmeffect","Bilateral Sharpen.fxmeffect","Real Glare.fxmeffect","Halation.fxmeffect"]:
    assert (root/"data/effect-transitions"/name).exists(), name
panel=(root/"src/effects/effects-panel.cpp").read_text()
assert "QComboBox::currentIndexChanged" in panel
assert "LayerEffectType::Glare" in panel and "LayerEffectType::Halation" in panel
runtime=(root/"src/effects/effect-runtime.cpp").read_text()
assert '"fxm.builtin.glare"' in runtime and 'shaders/glare/glare.effect' in runtime
assert '"fxm.builtin.halation"' in runtime and 'shaders/halation/halation.effect' in runtime
shader=(root/"data/effect-transitions/shaders/noise/noise.effect").read_text()
for uniform in ["profile","noiseOffset","aspect","lacunarity","gain","brightness","contrast","affectAlpha","clampOutput","temporalStability"]:
    assert f"uniform " in shader and uniform in shader
print("Development Version 223 optical effects and Noise binding contract passed")
