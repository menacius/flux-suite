#include "ticker-runtime.h"
#include "layer-model.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {

bool near(double a, double b, double tolerance = 0.01)
{
    return std::abs(a - b) <= tolerance;
}

void sleep_tick()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

} // namespace

int main()
{
    Layer layer;
    layer.id = "ticker-runtime-contract";
    layer.type = LayerType::Ticker;
    const std::string title_id = "title-runtime-contract";

    layer.ticker_playback_mode = static_cast<int>(TickerPlaybackMode::AlwaysPlay);
    fxm::ticker_runtime::reset_for_mode_change(title_id, layer, false);
    const auto always_a = fxm::ticker_runtime::sample(title_id, layer, false);
    sleep_tick();
    const auto always_b = fxm::ticker_runtime::sample(title_id, layer, false);
    if (always_a.paused || always_b.paused || always_b.time_seconds <= always_a.time_seconds)
        return 1;

    fxm::ticker_runtime::toggle_pause(title_id, layer, false);
    const auto paused_a = fxm::ticker_runtime::sample(title_id, layer, false);
    sleep_tick();
    const auto paused_b = fxm::ticker_runtime::sample(title_id, layer, false);
    if (!paused_a.paused || !paused_b.paused || !near(paused_a.time_seconds, paused_b.time_seconds))
        return 2;

    fxm::ticker_runtime::toggle_pause(title_id, layer, false);
    sleep_tick();
    const auto resumed = fxm::ticker_runtime::sample(title_id, layer, false);
    if (resumed.paused || resumed.time_seconds <= paused_b.time_seconds)
        return 3;

    fxm::ticker_runtime::stop(title_id, layer, false);
    const auto stopped = fxm::ticker_runtime::sample(title_id, layer, false);
    if (!stopped.paused || stopped.time_seconds > 0.001)
        return 4;

    layer.ticker_playback_mode = static_cast<int>(TickerPlaybackMode::PausedUntilCued);
    fxm::ticker_runtime::reset_for_mode_change(title_id, layer, false);
    if (!fxm::ticker_runtime::sample(title_id, layer, false).paused)
        return 5;
    fxm::ticker_runtime::sample(title_id, layer, true);
    sleep_tick();
    const auto cued = fxm::ticker_runtime::sample(title_id, layer, false);
    if (cued.paused || cued.time_seconds <= 0.0)
        return 6;

    layer.ticker_playback_mode = static_cast<int>(TickerPlaybackMode::PausedUntilHotkey);
    fxm::ticker_runtime::reset_for_mode_change(title_id, layer, false);
    if (!fxm::ticker_runtime::sample(title_id, layer, false).paused)
        return 7;
    fxm::ticker_runtime::toggle_pause(title_id, layer, false);
    sleep_tick();
    const auto hotkey_started = fxm::ticker_runtime::sample(title_id, layer, false);
    if (hotkey_started.paused || hotkey_started.time_seconds <= 0.0)
        return 8;

    fxm::ticker_runtime::set_adaptive_pause(title_id, true);
    const auto adaptive_a = fxm::ticker_runtime::sample(title_id, layer, false);
    sleep_tick();
    const auto adaptive_b = fxm::ticker_runtime::sample(title_id, layer, false);
    if (!adaptive_a.paused || !adaptive_b.paused ||
        !near(adaptive_a.time_seconds, adaptive_b.time_seconds))
        return 9;
    fxm::ticker_runtime::set_adaptive_pause(title_id, false);
    sleep_tick();
    const auto adaptive_resumed = fxm::ticker_runtime::sample(title_id, layer, false);
    if (adaptive_resumed.paused || adaptive_resumed.time_seconds <= adaptive_b.time_seconds)
        return 10;

    fxm::ticker_runtime::clear_all();
    std::cout << "ticker runtime contract: PASS\n";
    return 0;
}
