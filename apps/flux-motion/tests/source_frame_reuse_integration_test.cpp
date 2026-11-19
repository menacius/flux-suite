#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
constexpr uint32_t width = 256, height = 144;
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
struct Stats {
    long long compositions, reused, captures, mattes;
};
Stats stats(obs_source_t *source)
{
    calldata_t p;
    calldata_init(&p);
    const bool ok = proc_handler_call(obs_source_get_proc_handler(source),
                                     "flux_motion_get_render_diagnostics", &p);
    Stats result{calldata_int(&p, "compositions"), calldata_int(&p, "reused_frames"),
                 calldata_int(&p, "scene_captures"), calldata_int(&p, "matte_rasters")};
    calldata_free(&p);
    require(ok, "render diagnostics unavailable");
    return result;
}
QJsonObject scalar(double value)
{
    return {{"static_value", value}, {"keyframes", QJsonArray{}}};
}
QString write_project(const QString &directory)
{
    QJsonArray titles;
    for (const QString &id : {QStringLiteral("plain"), QStringLiteral("masks"),
                              QStringLiteral("backdrop")}) {
        QJsonArray layers;
        for (int i = 0; i < (id == "plain" ? 1 : 3); ++i) {
            QJsonObject layer{{"id", QString("mask-%1").arg(i)}, {"type", 3},
                {"name", "Rectangle"}, {"visible", true}, {"in_time", 0.0},
                {"out_time", 10.0}, {"use_as_scene_mask", id != "plain"},
                {"rect_width", 64.0}, {"rect_height", 80.0},
                {"fill_color", 4294901760.0}, {"fill_color_a", scalar(255)},
                {"fill_color_r", scalar(255)}, {"fill_color_g", scalar(0)},
                {"fill_color_b", scalar(0)}, {"origin_x", 0.5}, {"origin_y", 0.5},
                {"position", QJsonObject{{"static_value", QJsonObject{
                    {"x", 48.0 + 80.0 * i}, {"y", 72.0}}}, {"keyframes", QJsonArray{}}}}};
            if (id == "backdrop") layer["blend_mode"] = 1;
            layers.append(layer);
        }
        titles.append(QJsonObject{{"id", id}, {"name", id}, {"schema_version", 6},
            {"width", int(width)}, {"height", int(height)}, {"duration", 10.0},
            {"pause_time", 1.0}, {"playback_mode", 2}, {"cue_end_behavior", 0},
            {"frame_rate", 25.0}, {"bg_color", 0}, {"layers", layers}});
    }
    const QString path = directory + "/frames.fxmproj";
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "cannot write test project");
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"titles", titles}}).toJson());
    return path;
}
void pump(QApplication &app, int milliseconds)
{
    for (int i = 0; i < milliseconds / 10; ++i) {
        app.processEvents();
        QThread::msleep(10);
    }
}
struct RenderJob {
    obs_source_t *source = nullptr;
    std::atomic<bool> pending{false};
    std::atomic<bool> done{false};
    bool different_backgrounds = false;
    Stats first{}, second{};
    QImage images[2];
    std::string error;
};
void render_views(void *opaque, uint32_t, uint32_t)
{
    auto &job = *static_cast<RenderJob *>(opaque);
    if (!job.pending.exchange(false)) return;
    try {
        for (int view = 0; view < 2; ++view) {
            gs_texrender_t *target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
            gs_viewport_push(); gs_projection_push(); gs_blend_state_push(); gs_matrix_push();
            require(target && gs_texrender_begin(target, width, height), "view target failed");
            gs_matrix_identity();
            gs_ortho(0, float(width), 0, float(height), -100, 100);
            gs_enable_depth_test(false); gs_enable_color(true, true, true, true);
            vec4 clear;
            const float background = job.different_backgrounds ? float(view) : 0.0f;
            vec4_set(&clear, background, background, background,
                     job.different_backgrounds ? 1.0f : 0.0f);
            gs_clear(GS_CLEAR_COLOR, &clear, 1, 0);
            obs_source_video_render(job.source);
            gs_texrender_end(target);
            gs_stagesurf_t *stage = gs_stagesurface_create(width, height, GS_BGRA);
            gs_stage_texture(stage, gs_texrender_get_texture(target));
            uint8_t *pixels = nullptr; uint32_t stride = 0;
            require(gs_stagesurface_map(stage, &pixels, &stride), "readback failed");
            job.images[view] = QImage(pixels, width, height, stride,
                                      QImage::Format_ARGB32_Premultiplied).copy();
            gs_stagesurface_unmap(stage); gs_stagesurface_destroy(stage);
            gs_texrender_destroy(target);
            gs_matrix_pop(); gs_blend_state_pop(); gs_projection_pop(); gs_viewport_pop();
            (view == 0 ? job.first : job.second) = stats(job.source);
        }
    } catch (const std::exception &e) { job.error = e.what(); }
    job.done.store(true, std::memory_order_release);
}
void run_views(QApplication &app, RenderJob &job, obs_source_t *source, bool backdrop = false)
{
    job.source = source; job.different_backgrounds = backdrop; job.error.clear();
    job.done.store(false); job.pending.store(true);
    for (int i = 0; i < 500 && !job.done.load(std::memory_order_acquire); ++i) pump(app, 10);
    require(job.done.load(), "render callback timed out");
    require(job.error.empty(), job.error.c_str());
    const auto pixel = job.images[0].pixelColor(48, 72);
    require(pixel.alpha() > 0, "test source did not render visible pixels");
    if (backdrop) {
        require(job.second.compositions == job.first.compositions + 1,
                "backdrop-dependent frame was incorrectly reused");
        require(job.second.reused == job.first.reused, "backdrop reused final frame");
        require(job.images[0].pixelColor(48, 72) != job.images[1].pixelColor(48, 72),
                "different backdrops produced the same multiplied pixel");
    } else {
        require(job.second.compositions == job.first.compositions,
                "second view recomposed the same video frame");
        require(job.second.reused == job.first.reused + 1, "second view was not reused");
        require(job.images[0] == job.images[1], "views differ within the same frame");
    }
    require(job.second.captures == job.first.captures, "second view recaptured mask scenes");
}
obs_source_t *make_source(const QString &project, const char *id, const char *name,
                          bool move_with_mask = true)
{
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "project_path", project.toUtf8().constData());
    obs_data_set_string(settings, "title_id", id);
    for (int i = 0; i < 3; ++i) {
        const QByteArray prefix = "scene_mask_" + (QByteArray("mask-") + QByteArray::number(i))
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) + "_";
        obs_data_set_string(settings, (prefix + "scene").constData(), "Live input");
        obs_data_set_bool(settings, (prefix + "move_with_mask").constData(), move_with_mask || i != 2);
    }
    auto *source = obs_source_create("flux_motion_source", name, settings, nullptr);
    obs_data_release(settings);
    require(source, "cannot create Flux source");
    obs_source_inc_showing(source); obs_source_inc_active(source);
    obs_source_media_restart(source);
    return source;
}
void remove_source(obs_source_t *source)
{
    obs_source_dec_active(source); obs_source_dec_showing(source);
    obs_source_remove(source); obs_source_release(source);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Flux frame reuse test");
    if (argc != 4) return 77;
#ifdef _WIN32
    const std::wstring plugin_bin = QFileInfo(QString::fromUtf8(argv[1])).absolutePath().toStdWString();
    const std::wstring obs_bin = (QString::fromUtf8(argv[3]) + "/bin/64bit").toStdWString();
    AddDllDirectory(plugin_bin.c_str());
    AddDllDirectory(obs_bin.c_str());
#endif
    QTemporaryDir config;
    const QByteArray config_path = config.path().toUtf8();
    if (!obs_startup("en-US", config_path.constData(), nullptr)) return 77;
    const QString obs_root = QString::fromUtf8(argv[3]);
    const QByteArray data_path = (obs_root + "/data/libobs").toUtf8();
    obs_add_data_path(data_path.constData());
    obs_audio_info audio{}; audio.samples_per_sec = 48000; audio.speakers = SPEAKERS_STEREO;
    obs_reset_audio(&audio);
    obs_video_info video{};
    const QByteArray graphics = (obs_root + "/bin/64bit/libobs-d3d11.dll").toUtf8();
    video.graphics_module = graphics.constData(); video.fps_num = 25; video.fps_den = 1;
    video.base_width = video.output_width = width;
    video.base_height = video.output_height = height;
    video.output_format = VIDEO_FORMAT_NV12; video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_PARTIAL; video.scale_type = OBS_SCALE_BICUBIC;
    const QString cwd = QDir::currentPath(); QDir::setCurrent(QString::fromUtf8(data_path));
    const int reset = obs_reset_video(&video); QDir::setCurrent(cwd);
    if (reset != OBS_VIDEO_SUCCESS) { obs_shutdown(); return 77; }
    int result = 0;
    RenderJob job;
    bool callback_registered = false;
    std::vector<obs_source_t *> graphics_sources;
    obs_scene_t *input = nullptr;
    obs_source_t *fill = nullptr;
    try {
        obs_module_t *module = nullptr;
        require(obs_open_module(&module, argv[1], argv[2]) == MODULE_SUCCESS &&
                obs_init_module(module), "cannot load Flux plugin");
        obs_source_info solid{}; solid.id = "frame_reuse_live_input";
        solid.type = OBS_SOURCE_TYPE_INPUT;
        solid.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
        solid.get_name = [](void *) { return "Live input"; };
        solid.create = [](obs_data_t *, obs_source_t *s) -> void * { return s; };
        solid.destroy = [](void *) {};
        solid.get_width = [](void *) { return width; };
        solid.get_height = [](void *) { return height; };
        solid.video_render = [](void *, gs_effect_t *) {
            auto *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
            const float red = 0.1f + float((obs_get_video_frame_time() / 40000000) % 13) / 16.0f;
            vec4 color; vec4_set(&color, red, 0.5f, 0.8f, 1.0f);
            gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
            while (gs_effect_loop(effect, "Solid")) gs_draw_sprite(nullptr, 0, width, height);
        };
        obs_register_source(&solid);
        input = obs_scene_create("Live input");
        fill = obs_source_create("frame_reuse_live_input", "Live color", nullptr, nullptr);
        obs_scene_add(input, fill);
        const QString project = write_project(config.path());
        obs_add_main_render_callback(render_views, &job);
        callback_registered = true;
        for (const char *id : {"plain", "masks", "backdrop"}) {
            auto *source = make_source(project, id, id);
            graphics_sources.push_back(source);
            obs_set_output_source(0, source);
            pump(app, 1600); obs_source_media_play_pause(source, true); pump(app, 200);
            run_views(app, job, source, std::string(id) == "backdrop");
            if (std::string(id) == "masks") {
                const Stats before = stats(source);
                const QImage held = job.images[0];
                bool changed = false;
                for (int i = 0; i < 6; ++i) {
                    pump(app, 50); run_views(app, job, source);
                    changed = changed || held != job.images[0];
                }
                const Stats after = stats(source);
                require(after.captures > before.captures, "held title froze live mask content");
                require(after.mattes == before.mattes, "static mattes rerasterized between frames");
                require(changed, "live input pixels did not advance while title was held");
                auto *duplicate = make_source(project, "masks", "Independent duplicate", false);
                graphics_sources.push_back(duplicate);
                obs_set_output_source(0, duplicate);
                pump(app, 1000);
                run_views(app, job, duplicate);
                obs_source_media_stop(duplicate); pump(app, 100);
                obs_set_output_source(0, source);
                run_views(app, job, source);
                remove_source(duplicate);
                graphics_sources.pop_back();
                const QImage before_transform = job.images[0];
                auto *settings = obs_source_get_settings(source);
                const QByteArray key = "scene_mask_" + QByteArray("mask-0").toBase64(
                    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) + "_x";
                obs_data_set_double(settings, key.constData(), 20.0);
                obs_source_update(source, settings); obs_data_release(settings);
                pump(app, 100); run_views(app, job, source);
                require(before_transform.pixelColor(20, 72).alpha() > 0 &&
                        job.images[0].pixelColor(20, 72).alpha() == 0,
                        "mask position update was not displayed");
            }
            std::printf("PASS %s: compositions=%lld reused=%lld captures=%lld mattes=%lld\n",
                id, job.second.compositions, job.second.reused, job.second.captures, job.second.mattes);
            obs_set_output_source(0, nullptr);
            remove_source(source);
            graphics_sources.pop_back();
        }
        obs_remove_main_render_callback(render_views, &job);
        callback_registered = false;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); result = 1;
    }
    if (callback_registered) obs_remove_main_render_callback(render_views, &job);
    obs_set_output_source(0, nullptr);
    for (auto *source : graphics_sources) remove_source(source);
    obs_wait_for_destroy_queue();
    if (input) { obs_source_remove(obs_scene_get_source(input)); obs_scene_release(input); }
    if (fill) { obs_source_remove(fill); obs_source_release(fill); }
    obs_wait_for_destroy_queue(); obs_shutdown();
    return result;
}
