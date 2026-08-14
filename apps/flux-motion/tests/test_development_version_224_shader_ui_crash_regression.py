from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def test_gpu_text_shader_avoids_reserved_point_identifier():
    text = (ROOT / "src/rendering/title-gpu-text-renderer.cpp").read_text(encoding="utf-8")
    assert "float gradientPosition(float2 point" not in text
    assert "float4 gradientColor(float2 point" not in text
    assert "float gradientPosition(float2 samplePoint" in text

def test_noise_effect_has_obs_compatible_entry_points():
    asset = (ROOT / "data/effect-transitions/shaders/noise/noise.effect").read_text(encoding="utf-8")
    embedded = (ROOT / "src/rendering/title-effect-registry.cpp").read_text(encoding="utf-8")
    for text in (asset, embedded):
        assert "VertDataOut VSDefault(VertDataIn v_in)" in text
        assert "vertex_shader = VSDefault(v_in);" in text
        assert "pixel_shader = PSNoise(v_in);" in text

def test_effect_popup_style_resolves_all_placeholders_on_qt68():
    text = (ROOT / "src/editor/properties-panel/popup-state.inc").read_text(encoding="utf-8")
    assert ".arg(panel_text_name, section_bg_name, border_name, subtle_text_name)" not in text
    assert ".arg(panel_text_name)" in text
    assert ".arg(subtle_text_name);" in text


def test_gpu_text_effect_lifetime_is_serialized_and_invalid_compile_is_destroyed():
    text = (ROOT / "src/rendering/title-gpu-text-renderer.cpp").read_text(encoding="utf-8")
    assert "std::mutex effect_mutex;" in text
    assert "std::lock_guard<std::mutex> effect_lock(impl_->effect_mutex);" in text
    assert '!gs_effect_get_technique(impl_->effect, "Draw")' in text
    assert "gs_effect_destroy(impl_->effect);" in text
    assert "impl_->effect = nullptr;" in text
