#pragma once
#include <cstdint>
#include <functional>
struct obs_source;

void transition_cue_gate_register();
void transition_cue_gate_start(std::function<bool()> bypass = {});
void transition_cue_gate_set_enabled(bool enabled);
void transition_cue_gate_set_duration(uint32_t milliseconds);
void transition_cue_gate_watch(obs_source *transition);
void transition_cue_gate_clear_watches();
void transition_cue_gate_cancel();
void transition_cue_gate_stop();
