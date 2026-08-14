#include "ticker-runtime.h"

#include "layer-model.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace fxm::ticker_runtime {
namespace {

using Clock = std::chrono::steady_clock;

struct RuntimeState {
    std::string title_id;
    int playback_mode = static_cast<int>(TickerPlaybackMode::AlwaysPlay);
    double elapsed_seconds = 0.0;
    Clock::time_point anchor = Clock::now();
    bool initialized = false;
    bool was_effectively_paused = true;
    bool manual_paused = false;
    bool cue_gate_released = true;
    bool hotkey_gate_released = true;
    bool last_title_cued = false;
};

std::mutex g_mutex;
std::unordered_map<std::string, RuntimeState> g_states;
std::unordered_map<std::string, bool> g_adaptive_paused_titles;

int normalized_mode(const Layer &layer)
{
    return std::clamp(layer.ticker_playback_mode,
                      static_cast<int>(TickerPlaybackMode::AlwaysPlay),
                      static_cast<int>(TickerPlaybackMode::CustomPlayback));
}

bool adaptive_paused(const std::string &title_id)
{
    const auto it = g_adaptive_paused_titles.find(title_id);
    return it != g_adaptive_paused_titles.end() && it->second;
}

bool mode_paused(const RuntimeState &state)
{
    switch (static_cast<TickerPlaybackMode>(state.playback_mode)) {
    case TickerPlaybackMode::PausedUntilCued:
        return !state.cue_gate_released;
    case TickerPlaybackMode::PausedUntilHotkey:
        return !state.hotkey_gate_released;
    case TickerPlaybackMode::CustomPlayback:
    case TickerPlaybackMode::AlwaysPlay:
    default:
        return false;
    }
}

bool effectively_paused(const RuntimeState &state)
{
    return state.manual_paused || mode_paused(state) ||
           adaptive_paused(state.title_id);
}

void accumulate_until(RuntimeState &state, Clock::time_point now)
{
    if (!state.initialized || state.was_effectively_paused)
        return;
    state.elapsed_seconds +=
        std::chrono::duration<double>(now - state.anchor).count();
    state.anchor = now;
}

void apply_effective_transition(RuntimeState &state, Clock::time_point now)
{
    const bool paused = effectively_paused(state);
    if (!state.initialized) {
        state.initialized = true;
        state.anchor = now;
        state.was_effectively_paused = paused;
        return;
    }

    if (!state.was_effectively_paused && paused)
        accumulate_until(state, now);
    else if (state.was_effectively_paused && !paused)
        state.anchor = now;

    state.was_effectively_paused = paused;
}

void initialize_mode(RuntimeState &state, int mode, Clock::time_point now,
                     bool reset_elapsed)
{
    if (reset_elapsed)
        state.elapsed_seconds = 0.0;
    state.playback_mode = mode;
    state.manual_paused = false;
    state.cue_gate_released =
        mode != static_cast<int>(TickerPlaybackMode::PausedUntilCued);
    state.hotkey_gate_released =
        mode != static_cast<int>(TickerPlaybackMode::PausedUntilHotkey);
    state.anchor = now;
    state.initialized = true;
    state.was_effectively_paused = effectively_paused(state);
}

RuntimeState &state_for(const std::string &title_id, const Layer &layer,
                        bool title_cued, Clock::time_point now)
{
    RuntimeState &state = g_states[layer.id];
    const int mode = normalized_mode(layer);
    const bool first_use = !state.initialized;
    const bool has_title_context = !title_id.empty();
    const bool title_changed = has_title_context && !first_use &&
                               !state.title_id.empty() && state.title_id != title_id;

    const bool was_title_cued = state.last_title_cued;
    if (has_title_context)
        state.title_id = title_id;

    if (first_use || title_changed)
        initialize_mode(state, mode, now, true);
    else if (state.playback_mode != mode) {
        accumulate_until(state, now);
        initialize_mode(state, mode, now, true);
    }

    if (has_title_context)
        state.last_title_cued = title_cued;

    if (state.playback_mode ==
            static_cast<int>(TickerPlaybackMode::PausedUntilCued) &&
        state.last_title_cued)
        state.cue_gate_released = true;

    apply_effective_transition(state, now);
    return state;
}

TickerRuntimeSnapshot snapshot_for(RuntimeState &state, Clock::time_point now)
{
    apply_effective_transition(state, now);
    TickerRuntimeSnapshot result;
    result.paused = state.was_effectively_paused;
    result.time_seconds = state.elapsed_seconds;
    if (!result.paused)
        result.time_seconds +=
            std::chrono::duration<double>(now - state.anchor).count();
    result.time_seconds = std::max(0.0, result.time_seconds);
    return result;
}

} // namespace

TickerRuntimeSnapshot sample(const std::string &title_id, const Layer &layer,
                             bool title_cued)
{
    if (layer.type != LayerType::Ticker)
        return {};
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState &state = state_for(title_id, layer, title_cued, now);
    return snapshot_for(state, now);
}

TickerRuntimeSnapshot status(const std::string &title_id, const Layer &layer,
                             bool title_cued)
{
    return sample(title_id, layer, title_cued);
}

void toggle_pause(const std::string &title_id, const Layer &layer,
                  bool title_cued)
{
    if (layer.type != LayerType::Ticker)
        return;
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState &state = state_for(title_id, layer, title_cued, now);
    accumulate_until(state, now);

    if (state.playback_mode ==
            static_cast<int>(TickerPlaybackMode::PausedUntilCued) &&
        !state.cue_gate_released) {
        state.cue_gate_released = true;
        state.manual_paused = false;
    } else if (state.playback_mode ==
                   static_cast<int>(TickerPlaybackMode::PausedUntilHotkey) &&
               !state.hotkey_gate_released) {
        state.hotkey_gate_released = true;
        state.manual_paused = false;
    } else {
        state.manual_paused = !state.manual_paused;
    }

    state.anchor = now;
    state.was_effectively_paused = effectively_paused(state);
}

void stop(const std::string &title_id, const Layer &layer,
          bool title_cued)
{
    if (layer.type != LayerType::Ticker)
        return;
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState &state = state_for(title_id, layer, title_cued, now);
    state.elapsed_seconds = 0.0;
    state.manual_paused = true;
    if (state.playback_mode ==
        static_cast<int>(TickerPlaybackMode::PausedUntilHotkey))
        state.hotkey_gate_released = true;
    state.anchor = now;
    state.was_effectively_paused = true;
}

void reset_for_mode_change(const std::string &title_id, const Layer &layer,
                           bool title_cued)
{
    if (layer.type != LayerType::Ticker)
        return;
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState &state = g_states[layer.id];
    state.title_id = title_id;
    state.last_title_cued = title_cued;
    initialize_mode(state, normalized_mode(layer), now, true);
    if (state.playback_mode ==
            static_cast<int>(TickerPlaybackMode::PausedUntilCued) &&
        title_cued) {
        state.cue_gate_released = true;
        state.was_effectively_paused = effectively_paused(state);
    }
}

void set_adaptive_pause(const std::string &title_id, bool paused)
{
    if (title_id.empty())
        return;

    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool previous = adaptive_paused(title_id);
    if (previous == paused)
        return;

    for (auto &[layer_id, state] : g_states) {
        (void)layer_id;
        if (state.title_id == title_id)
            accumulate_until(state, now);
    }

    if (paused)
        g_adaptive_paused_titles[title_id] = true;
    else
        g_adaptive_paused_titles.erase(title_id);

    for (auto &[layer_id, state] : g_states) {
        (void)layer_id;
        if (state.title_id != title_id)
            continue;
        state.anchor = now;
        state.was_effectively_paused = effectively_paused(state);
    }
}

void clear_title(const std::string &title_id)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_states.begin(); it != g_states.end();) {
        if (it->second.title_id == title_id)
            it = g_states.erase(it);
        else
            ++it;
    }
    g_adaptive_paused_titles.erase(title_id);
}

void clear_all()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_states.clear();
    g_adaptive_paused_titles.clear();
}

} // namespace fxm::ticker_runtime
