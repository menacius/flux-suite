#include "transition-cue-gate.h"
#include <obs-module.h>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace {
constexpr const char *kHoldId = "flux_motion_outro_hold";
using Source = std::shared_ptr<obs_source_t>;
Source adopt(obs_source_t *source) { return Source(source, obs_source_release); }
Source retain(obs_source_t *source) { return adopt(obs_source_get_ref(source)); }

struct CueState { bool supported = false, active = false, pending = false; double remaining = 0; };
CueState cue_state(obs_source_t *source)
{
    calldata_t params;
    calldata_init(&params);
    CueState state;
    state.supported = proc_handler_call(obs_source_get_proc_handler(source), "flux_motion_get_cue_state", &params);
    if (state.supported) {
        state.active = calldata_bool(&params, "active");
        state.pending = calldata_bool(&params, "pending");
        state.remaining = calldata_float(&params, "remaining_seconds");
    }
    calldata_free(&params);
    return state;
}
bool uncue_before_transition_enabled(obs_source_t *source)
{
    auto *settings = obs_source_get_settings(source);
    // Public OBS source property; absent keys are deliberately opt-out.
    const bool enabled = obs_data_get_bool(settings, "uncue_before_scene_transition");
    obs_data_release(settings);
    return enabled;
}
void uncue(obs_source_t *source)
{
    calldata_t params;
    calldata_init(&params);
    proc_handler_call(obs_source_get_proc_handler(source), "flux_motion_uncue", &params);
    calldata_free(&params);
}

// Traverse visible scene items, including groups and nested scenes, once per source.
struct SceneSources {
    std::unordered_set<obs_source_t *> visited;
    std::vector<Source> graphics;
    void collect(obs_source_t *source)
    {
        if (!source || !visited.insert(source).second) return;
        if (cue_state(source).supported) graphics.push_back(retain(source));
        auto *scene = obs_scene_from_source(source);
        if (!scene) scene = obs_group_from_source(source);
        if (scene) obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
            if (obs_sceneitem_visible(item))
                static_cast<SceneSources *>(param)->collect(obs_sceneitem_get_source(item));
            return true;
        }, this);
    }
};

struct Gate {
    std::recursive_mutex mutex;
    std::atomic<bool> enabled {false};
    std::atomic<uint32_t> default_duration {300};
    QTimer timer;
    Source hold, transition, from, to;
    std::vector<Source> leaving;
    std::mutex watches_mutex;
    std::vector<Source> watched;
    std::function<bool()> bypass;
    bool internal = false;
    uint32_t duration = 300;
    uint64_t sample_time = 0;
    float sample_progress = 0;
    bool duration_measured = false;
    std::chrono::steady_clock::time_point started, deadline;

    Gate(std::function<bool()> skip) : bypass(std::move(skip))
    {
        hold = adopt(obs_source_create_private(kHoldId, "Flux Motion outro hold", nullptr));
        signal_handler_connect(obs_get_signal_handler(), "source_transition_start", on_start, this);
        signal_handler_connect(obs_get_signal_handler(), "channel_change", on_output_changed, this);
        auto output = adopt(obs_get_output_source(0));
        watch(output.get());
        if (hold) signal_handler_connect(obs_source_get_signal_handler(hold.get()), "transition_start", on_start, this);
        QObject::connect(&timer, &QTimer::timeout, [&] { poll(); });
        timer.start(10);
    }
    ~Gate()
    {
        timer.stop();
        signal_handler_disconnect(obs_get_signal_handler(), "source_transition_start", on_start, this);
        signal_handler_disconnect(obs_get_signal_handler(), "channel_change", on_output_changed, this);
        if (hold) signal_handler_disconnect(obs_source_get_signal_handler(hold.get()), "transition_start", on_start, this);
        clear_watches();
        cancel();
    }
    void clear_watches()
    {
        std::lock_guard<std::mutex> lock(watches_mutex);
        for (const auto &source : watched)
            signal_handler_disconnect(obs_source_get_signal_handler(source.get()), "transition_start", on_start, this);
        watched.clear();
    }
    void watch(obs_source_t *source)
    {
        std::lock_guard<std::mutex> lock(watches_mutex);
        if (!source || source == hold.get() ||
            obs_source_get_type(source) != OBS_SOURCE_TYPE_TRANSITION) return;
        for (const auto &existing : watched) if (existing.get() == source) return;
        watched.push_back(retain(source));
        // OBS frontend transitions are private: they emit only local signals.
        signal_handler_connect(obs_source_get_signal_handler(source), "transition_start", on_start, this);
    }
    static void on_output_changed(void *priv, calldata_t *params)
    {
        if (calldata_int(params, "channel") != 0) return;
        auto *self = static_cast<Gate *>(priv);
        auto *source = static_cast<obs_source_t *>(calldata_ptr(params, "source"));
        // channel_change runs under the OBS view lock: only attach the local
        // signal here. Calling obs_get_output_source would deadlock.
        if (source != self->hold.get()) self->watch(source);
    }
    static void on_start(void *priv, calldata_t *params)
    {
        static_cast<Gate *>(priv)->begin(static_cast<obs_source_t *>(calldata_ptr(params, "source")));
    }
    void begin(obs_source_t *requested)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (internal || !enabled || !hold || !requested) return;
        auto output = adopt(obs_get_output_source(0));
        if (output.get() != requested || (bypass && bypass())) return;
        if (transition) {
            if (requested == hold.get()) {
                to = adopt(obs_transition_get_source(requested, OBS_TRANSITION_SOURCE_B));
                // Keep A active while clearing a replacement request on the hold.
                obs_source_inc_active(from.get());
                obs_source_inc_showing(from.get());
                obs_transition_clear(hold.get());
                obs_transition_set(hold.get(), from.get());
                obs_source_dec_showing(from.get());
                obs_source_dec_active(from.get());
            }
            return;
        }
        auto a = adopt(obs_transition_get_source(requested, OBS_TRANSITION_SOURCE_A));
        auto b = adopt(obs_transition_get_source(requested, OBS_TRANSITION_SOURCE_B));
        if (!a || a == b) return;
        SceneSources outgoing;
        outgoing.collect(a.get());
        double remaining = 0;
        for (const auto &source : outgoing.graphics) {
            const auto state = cue_state(source.get());
            if ((state.active || state.pending) && uncue_before_transition_enabled(source.get())) {
                leaving.push_back(source);
                remaining = std::max(remaining, state.remaining);
            }
        }
        if (leaving.empty()) return;
        transition = retain(requested); from = std::move(a); to = std::move(b);
        duration = default_duration;
        sample_time = 0; sample_progress = 0;
        duration_measured = obs_transition_fixed(requested);
        started = std::chrono::steady_clock::now();
        deadline = started + std::chrono::milliseconds(static_cast<int64_t>((remaining + 3) * 1000));
        // Transfer the active output before clearing B. This keeps outgoing
        // graphics alive and restarts incoming scene masks only at the real cut.
        obs_transition_set(hold.get(), from.get());
        obs_set_output_source(0, hold.get());
        obs_transition_clear(transition.get()); // No premature transition_stop signal.
        blog(LOG_INFO, "[Flux Motion] Waiting for %zu graphic outro(s) before scene transition.", leaving.size());
        for (const auto &source : leaving) uncue(source.get());
    }
    void release()
    {
        obs_transition_clear(hold.get());
        transition.reset(); from.reset(); to.reset(); leaving.clear();
    }
    void cancel()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (!transition) return;
        auto output = adopt(obs_get_output_source(0));
        if (output == hold) {
            obs_transition_set(transition.get(), from.get());
            obs_set_output_source(0, transition.get());
        }
        release();
    }
    void poll()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (!transition) return;
        auto output = adopt(obs_get_output_source(0));
        if (output != hold) { release(); return; }
        // libobs retains the original AUTO clock after clear. Recover the exact
        // requested duration (including Quick Transitions/scene overrides).
        const uint64_t before = obs_get_video_frame_time();
        const float progress = obs_transition_get_time(transition.get());
        const uint64_t after = obs_get_video_frame_time();
        if (!duration_measured && before == after && progress > 0 && progress < 1) {
            if (sample_time && before > sample_time && progress > sample_progress) {
                duration = static_cast<uint32_t>(std::llround(
                    (before - sample_time) / 1e6 / (progress - sample_progress)));
                duration_measured = true;
            }
            sample_time = before; sample_progress = progress;
        }
        bool busy = false;
        for (const auto &source : leaving) {
            const auto state = cue_state(source.get());
            busy |= state.active || state.pending;
        }
        const auto now = std::chrono::steady_clock::now();
        if (enabled && busy && now < deadline) return;
        // An immediately completed outro may precede the first clock sample.
        // Allow two video frames to recover a Quick Transition's duration.
        if (enabled && !busy && !duration_measured && progress < 1 &&
            now - started < std::chrono::milliseconds(250)) return;
        if (busy && enabled)
            blog(LOG_WARNING, "[Flux Motion] Outro wait timed out; continuing the scene transition.");
        internal = true;
        obs_transition_set(transition.get(), from.get());
        obs_set_output_source(0, transition.get());
        obs_transition_start(transition.get(), OBS_TRANSITION_MODE_AUTO, duration, to.get());
        internal = false;
        release();
    }
};
std::unique_ptr<Gate> gate;
std::atomic<bool> configured_enabled {false};
std::atomic<uint32_t> configured_duration {300};
}

void transition_cue_gate_register()
{
    static obs_source_info info = {};
    info.id = kHoldId;
    info.type = OBS_SOURCE_TYPE_TRANSITION;
    info.output_flags = OBS_SOURCE_CAP_DISABLED;
    info.get_name = [](void *) { return "Flux Motion outro hold"; };
    info.create = [](obs_data_t *, obs_source_t *source) -> void * { return source; };
    info.destroy = [](void *) {};
    info.video_render = [](void *data, gs_effect_t *) {
        obs_transition_video_render_direct(static_cast<obs_source_t *>(data), OBS_TRANSITION_SOURCE_A);
    };
    info.video_get_color_space = [](void *data, size_t, const gs_color_space *) {
        return obs_transition_video_get_color_space(static_cast<obs_source_t *>(data));
    };
    info.audio_render = [](void *data, uint64_t *ts, obs_source_audio_mix *audio,
                           uint32_t mixers, size_t channels, size_t rate) {
        return obs_transition_audio_render(static_cast<obs_source_t *>(data), ts, audio, mixers, channels, rate,
            [](void *, float) { return 1.0f; }, [](void *, float) { return 0.0f; });
    };
    obs_register_source(&info);
}
void transition_cue_gate_start(std::function<bool()> bypass)
{
    if (!gate) gate = std::make_unique<Gate>(std::move(bypass));
    gate->enabled = configured_enabled.load();
    gate->default_duration = configured_duration.load();
}
void transition_cue_gate_set_enabled(bool enabled)
{
    configured_enabled = enabled;
    if (gate) gate->enabled = enabled;
}
void transition_cue_gate_set_duration(uint32_t milliseconds)
{
    configured_duration = milliseconds;
    if (gate) gate->default_duration = milliseconds;
}
void transition_cue_gate_cancel() { if (gate) gate->cancel(); }
void transition_cue_gate_watch(obs_source_t *source) { if (gate) gate->watch(source); }
void transition_cue_gate_clear_watches() { if (gate) gate->clear_watches(); }
void transition_cue_gate_stop() { gate.reset(); }
