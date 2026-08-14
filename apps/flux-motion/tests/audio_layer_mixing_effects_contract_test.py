from pathlib import Path
root = Path(__file__).resolve().parents[1]
model = (root / 'Core/layer-model.h').read_text(encoding='utf-8')
data = (root / 'src/core/title-data.cpp').read_text(encoding='utf-8')
runtime = (root / '../../plugins/obs/flux-motion/src/title-audio-runtime.cpp').read_text(encoding='utf-8')
for token in ('audio_solo', 'AudioFadeCurve', 'AudioEffectType', 'audio_effects'):
    assert token in model
    assert token in data
for token in ('any_solo', 'HighPass', 'LowPass', 'CompressorLimiter', 'Master peak protection',
              'audio_output_get_sample_rate', 'asset_cache_key', 'waveform', 'smooth_gain_l'):
    assert token in runtime
assert 'obs_source_output_audio' in runtime
assert 'std::tanh' in runtime
assert 'swr_alloc_set_opts2' in runtime
print('audio mixing/effects contract: OK')
