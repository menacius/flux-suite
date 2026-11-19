#include "transition-cue-gate.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <obs-module.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void wait(int milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do { QCoreApplication::processEvents(); QThread::msleep(5); }
    while (std::chrono::steady_clock::now() < end);
}
struct Graphic {
    std::atomic<bool> active {true}, ending {false};
    std::atomic<int> requests {0};
    std::atomic<double> remaining {0.6};
};
Graphic outgoing, shared, incoming;
void state(void *data, calldata_t *params)
{
    auto *graphic = static_cast<Graphic *>(data);
    calldata_set_bool(params, "active", graphic->active);
    calldata_set_bool(params, "pending", false);
    calldata_set_float(params, "remaining_seconds", graphic->remaining);
}
void uncue(void *data, calldata_t *)
{
    auto *graphic = static_cast<Graphic *>(data);
    ++graphic->requests;
    graphic->ending = true;
}
void register_sources()
{
    obs_source_info graphic = {};
    graphic.id = "test_cue_graphic";
    graphic.type = OBS_SOURCE_TYPE_INPUT;
    graphic.output_flags = OBS_SOURCE_VIDEO;
    graphic.get_name = [](void *) { return "Cue test"; };
    graphic.create = [](obs_data_t *, obs_source_t *source) -> void * {
        auto *data = std::string(obs_source_get_name(source)) == "outgoing" ? &outgoing :
            std::string(obs_source_get_name(source)) == "shared" ? &shared : &incoming;
        auto *procs = obs_source_get_proc_handler(source);
        proc_handler_add(procs, "void flux_motion_get_cue_state(out bool active, out bool pending, out float remaining_seconds)", state, data);
        proc_handler_add(procs, "void flux_motion_uncue()", uncue, data);
        return data;
    };
    graphic.destroy = [](void *) {};
    graphic.get_width = graphic.get_height = [](void *) -> uint32_t { return 16; };
    graphic.video_render = [](void *, gs_effect_t *) {};
    graphic.video_tick = [](void *data, float seconds) {
        auto *graphic = static_cast<Graphic *>(data);
        if (graphic->ending) {
            const double next = graphic->remaining.load() - seconds;
            graphic->remaining = next;
            if (next <= 0) { graphic->active = false; graphic->ending = false; }
        }
    };
    obs_register_source(&graphic);
    obs_source_info transition = {};
    transition.id = "test_cue_transition";
    transition.type = OBS_SOURCE_TYPE_TRANSITION;
    transition.get_name = [](void *) { return "Test fade"; };
    transition.create = [](obs_data_t *, obs_source_t *source) -> void * { return source; };
    transition.destroy = [](void *) {};
    transition.video_render = [](void *data, gs_effect_t *) {
        obs_transition_video_render(static_cast<obs_source_t *>(data), nullptr);
    };
    transition.audio_render = [](void *data, uint64_t *ts, obs_source_audio_mix *audio,
        uint32_t mixers, size_t channels, size_t rate) {
        return obs_transition_audio_render(static_cast<obs_source_t *>(data), ts, audio, mixers, channels, rate,
            [](void *, float t) { return 1 - t; }, [](void *, float t) { return t; });
    };
    obs_register_source(&transition);
}
void set_source_uncue(obs_source_t *source, bool enabled)
{
    auto *settings = obs_source_get_settings(source);
    obs_data_set_bool(settings, "uncue_before_scene_transition", enabled);
    obs_source_update(source, settings);
    obs_data_release(settings);
}
bool output_is(obs_source_t *source)
{
    auto *output = obs_get_output_source(0);
    const bool same = output == source;
    obs_source_release(output);
    return same;
}
std::atomic<int> video_stops {0};
void stopped(void *, calldata_t *) { ++video_stops; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString data = qEnvironmentVariable("FXM_TEST_OBS_DATA_PATH");
    QString graphics = qEnvironmentVariable("FXM_TEST_OBS_GRAPHICS_MODULE");
#ifdef _WIN32
    const QString installed = qEnvironmentVariable("ProgramFiles") + "/obs-studio";
    if (data.isEmpty()) data = installed + "/data/libobs";
    if (graphics.isEmpty()) graphics = installed + "/bin/64bit/libobs-d3d11.dll";
#endif
    if (!QFileInfo::exists(data + "/default.effect") || !QFileInfo::exists(graphics)) return 77;
    QTemporaryDir config;
    require(obs_startup("en-US", config.path().toUtf8().constData(), nullptr), "OBS startup");
    obs_add_data_path(data.toUtf8().constData());
    obs_audio_info audio = {}; audio.samples_per_sec = 48000; audio.speakers = SPEAKERS_STEREO;
    require(obs_reset_audio(&audio), "Audio initialization");
    obs_video_info video = {};
    const auto module = graphics.toUtf8(); video.graphics_module = module.constData();
    video.fps_num = 50; video.fps_den = 1;
    video.base_width = video.output_width = 320; video.base_height = video.output_height = 180;
    video.output_format = VIDEO_FORMAT_NV12; video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_PARTIAL; video.scale_type = OBS_SCALE_BICUBIC;
    const QString cwd = QDir::currentPath(); QDir::setCurrent(data);
    const auto reset = obs_reset_video(&video); QDir::setCurrent(cwd);
    require(reset == OBS_VIDEO_SUCCESS, "Video initialization");
    register_sources(); transition_cue_gate_register();
    bool manual = false;
    transition_cue_gate_start([&] { return manual; });
    transition_cue_gate_set_enabled(true);
    transition_cue_gate_set_duration(300); // Deliberately different from actual 120 ms.
    auto *a = obs_scene_create("A"); auto *b = obs_scene_create("B");
    auto *out = obs_source_create("test_cue_graphic", "outgoing", nullptr, nullptr);
    auto *common = obs_source_create("test_cue_graphic", "shared", nullptr, nullptr);
    auto *in = obs_source_create("test_cue_graphic", "incoming", nullptr, nullptr);
    auto *titles = obs_scene_create("Shared titles");
    set_source_uncue(out, true); set_source_uncue(common, true); set_source_uncue(in, true);
    obs_scene_add(titles, common);
    obs_scene_add(a, out); obs_scene_add(a, obs_scene_get_source(titles));
    obs_scene_add(b, obs_scene_get_source(titles)); obs_scene_add(b, in);
    auto *tr = obs_source_create_private("test_cue_transition", "test fade", nullptr);
    // Exercise automatic observation of a private output transition.
    signal_handler_connect(obs_source_get_signal_handler(tr), "transition_video_stop", stopped, nullptr);
    obs_transition_set(tr, obs_scene_get_source(a)); obs_set_output_source(0, tr); wait(100);
    require(obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b)), "Transition starts");
    require(!output_is(tr), "Outgoing scene must be held during outro");
    require(outgoing.requests == 1 && shared.requests == 1 && incoming.requests == 0,
        "Uncue active outgoing and shared nested titles; retain incoming-only sources");
    wait(200);
    require(video_stops == 0 && !output_is(tr), "No premature scene completion during outro");
    wait(450);
    require(output_is(tr), "Original native transition must resume after outro");
    wait(180);
    require(video_stops == 1, "Preserve actual 120 ms duration, not the 300 ms preference");
    auto *current = obs_transition_get_source(tr, OBS_TRANSITION_SOURCE_A);
    require(current == obs_scene_get_source(b), "Transition reaches destination"); obs_source_release(current);
    // Reproduce the user's collection: the only cued title is in a nested
    // scene shared by A and B. This used to bypass the gate entirely.
    outgoing.active = false; shared.active = true; shared.ending = false; shared.remaining = 0.25;
    obs_transition_set(tr, obs_scene_get_source(a));
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(!output_is(tr) && shared.requests == 2, "Shared nested title alone must delay transition");
    wait(450);
    require(output_is(tr) && !shared.active, "Shared nested outro finishes before transition");
    // Default/off behavior and manual bypass leave the native transition alone.
    transition_cue_gate_set_enabled(false);
    obs_transition_set(tr, obs_scene_get_source(a)); outgoing.active = true; outgoing.remaining = 0.6;
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(output_is(tr) && outgoing.requests == 1, "Disabled setting preserves native behavior"); wait(180);
    transition_cue_gate_set_enabled(true); manual = true;
    obs_transition_set(tr, obs_scene_get_source(a));
    obs_transition_start(tr, OBS_TRANSITION_MODE_MANUAL, 120, obs_scene_get_source(b));
    require(output_is(tr) && outgoing.requests == 1, "T-bar bypass");
    obs_transition_clear(tr); obs_transition_set(tr, obs_scene_get_source(a)); manual = false;
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(!output_is(tr), "Second outro begins");
    transition_cue_gate_cancel();
    require(output_is(tr), "Cleanup restores original output before releasing hold");
    outgoing.ending = false; outgoing.active = true; outgoing.remaining = 0.6;
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(!output_is(tr), "Outro before disabling preference");
    transition_cue_gate_set_enabled(false); wait(50);
    require(output_is(tr), "Disabling preference resumes a pending transition"); wait(180);
    outgoing.ending = false; outgoing.active = true; outgoing.remaining = 0.6;
    transition_cue_gate_set_enabled(true); obs_transition_set(tr, obs_scene_get_source(a));
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(!output_is(tr), "Outro before external output replacement");
    obs_set_output_source(0, obs_scene_get_source(b)); wait(50);
    require(output_is(obs_scene_get_source(b)), "Do not overwrite an external output replacement");
    // Per-source policy: selected shared source runs its outro; disabled
    // outgoing source stays cued even with the global master switch on.
    obs_transition_set(tr, obs_scene_get_source(a)); obs_set_output_source(0, tr);
    outgoing.active = true; outgoing.ending = false; outgoing.remaining = 0.6;
    shared.active = true; shared.ending = false; shared.remaining = 0.6;
    set_source_uncue(out, false); set_source_uncue(common, true);
    const int previous_out = outgoing.requests, previous_common = shared.requests;
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(!output_is(tr), "An opted-in source delays the transition");
    require(outgoing.requests == previous_out && shared.requests == previous_common + 1,
        "Only sources with their own toggle enabled receive Uncue");
    wait(850);
    require(output_is(tr) && outgoing.active && !shared.active,
        "Opted-out source remains cued while selected source completes its outro");
    // No participating sources means no hold, even with the master enabled.
    shared.active = true; shared.ending = false; shared.remaining = 0.6;
    set_source_uncue(common, false);
    obs_transition_set(tr, obs_scene_get_source(a));
    obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, 120, obs_scene_get_source(b));
    require(output_is(tr) && outgoing.requests == previous_out && shared.requests == previous_common + 1,
        "Master on with all source toggles off preserves the native transition");
    wait(180);
    transition_cue_gate_stop(); obs_set_output_source(0, nullptr);
    obs_source_remove(obs_scene_get_source(a)); obs_source_remove(obs_scene_get_source(b));
    obs_source_remove(obs_scene_get_source(titles));
    obs_source_remove(out); obs_source_remove(common); obs_source_remove(in);
    obs_source_release(tr); obs_scene_release(a); obs_scene_release(b); obs_scene_release(titles);
    obs_source_release(out); obs_source_release(common); obs_source_release(in);
    obs_wait_for_destroy_queue();
    obs_shutdown();
    std::puts("Transition outro, per-source/master policy, shared sources, duration and cleanup passed.");
}
