#include "layer-model.h"
#include <cassert>
#include <cmath>

int main()
{
    Layer audio;
    audio.type = LayerType::Audio;
    audio.audio_source = "missing.wav";
    audio.in_time = 1.25;
    audio.out_time = 4.75;
    audio.audio_in_point = 0.5;
    audio.audio_out_point = 4.0;
    audio.audio_volume = 0.8f;
    audio.audio_pan = -0.25f;
    audio.audio_muted = true;
    audio.audio_fade_in = 0.2;
    audio.audio_fade_out = 0.4;
    audio.audio_loop = true;
    audio.audio_independent = false;
    audio.audio_waveform = {-0.5f, 0.75f};

    assert(layer_type_is_audio(audio.type));
    assert(!layer_type_is_container(audio.type));
    assert(std::fabs(audio.out_time - 4.75) < 0.0001);
    assert(audio.audio_waveform.size() == 2);
    return 0;
}
