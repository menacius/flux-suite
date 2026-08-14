#pragma once

#include <string>

struct Layer;

enum class TickerPlaybackMode : int {
    AlwaysPlay = 0,
    PausedUntilCued = 1,
    PausedUntilHotkey = 2,
    CustomPlayback = 3,
};

struct TickerRuntimeSnapshot {
    double time_seconds = 0.0;
    bool paused = true;
};

namespace fxm::ticker_runtime {

TickerRuntimeSnapshot sample(const std::string &title_id, const Layer &layer,
                             bool title_cued);
TickerRuntimeSnapshot status(const std::string &title_id, const Layer &layer,
                             bool title_cued);

void toggle_pause(const std::string &title_id, const Layer &layer,
                  bool title_cued);
void stop(const std::string &title_id, const Layer &layer,
          bool title_cued);
void reset_for_mode_change(const std::string &title_id, const Layer &layer,
                           bool title_cued);

void set_adaptive_pause(const std::string &title_id, bool paused);
void clear_title(const std::string &title_id);
void clear_all();

} // namespace fxm::ticker_runtime
