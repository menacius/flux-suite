#include "obs-editor-host.h"

#include "host-context-provider.h"
#include "title-data.h"
#include "title-source.h"
#include "title-hotkeys.h"
#include "obs-plugin-preferences.h"

#include <obs-audio-controls.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <utility>

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

namespace fxm::obs_plugin {
namespace {

constexpr const char *kTitleSourceId = "flux_motion_source";
constexpr const char *kTitleIdSetting = "title_id";

bool launch_standalone_editor(const std::string *title_id,
                              bool preferences, std::string *error)
{
    const QString executable = resolved_editor_executable();
    if (executable.isEmpty()) {
        if (error) {
            *error = "Flux Motion was not found. Install Flux Motion "
                     "Editor or select its executable in Flux Motion OBS "
                     "Plugin Preferences.";
        }
        return false;
    }

    /* Flush dock-side changes before the independent process loads the shared
     * scene-collection file. */
    TitleDataStore::instance().save();

    QStringList arguments;
    arguments << QStringLiteral("--hosted-by-obs");
    obs_video_info host_video{};
    if (obs_get_video_info(&host_video) && host_video.base_width > 0 &&
        host_video.base_height > 0 && host_video.fps_num > 0 &&
        host_video.fps_den > 0) {
        arguments << QStringLiteral("--host-width")
                  << QString::number(host_video.base_width)
                  << QStringLiteral("--host-height")
                  << QString::number(host_video.base_height)
                  << QStringLiteral("--host-fps-num")
                  << QString::number(host_video.fps_num)
                  << QStringLiteral("--host-fps-den")
                  << QString::number(host_video.fps_den);
    }
    if (title_id) {
        arguments << QStringLiteral("--title-id")
                  << QString::fromStdString(*title_id);
    }
    if (preferences)
        arguments << QStringLiteral("--preferences");

    if (const auto *context = fxm::host_context_provider()) {
        arguments << QStringLiteral("--config-root")
                  << QString::fromStdString(context->config_path(""));
        arguments << QStringLiteral("--project-scope")
                  << QString::fromStdString(context->project_scope_name());
    }
    if (const char *data_path = obs_get_module_data_path(
            obs_current_module()); data_path && *data_path) {
        arguments << QStringLiteral("--data-root")
                   << QString::fromUtf8(data_path);
    }
    arguments << QStringLiteral("--obs-bin-root")
              << QCoreApplication::applicationDirPath();

    /* The editor links against the same libobs runtime as the plugin. Windows
     * resolves obs.dll's transitive dependencies (zlib, w32-pthreads, graphics
     * backends) from the host bin directory, so carry that directory into the
     * detached process instead of requiring a machine-wide OBS PATH entry. */
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString obs_bin_root = QCoreApplication::applicationDirPath();
    environment.insert(
        QStringLiteral("PATH"),
        obs_bin_root + QDir::listSeparator() +
            environment.value(QStringLiteral("PATH")));
    process.setProcessEnvironment(environment);

    qint64 process_id = 0;
    if (!process.startDetached(&process_id)) {
        if (error)
            *error = "Could not start Flux Motion.";
        return false;
    }
    return true;
}

bool lock_private_preview_item(obs_scene_t *, obs_sceneitem_t *item, void *)
{
    if (!item)
        return true;
    if (obs_sceneitem_is_group(item))
        obs_sceneitem_group_enum_items(item, lock_private_preview_item,
                                       nullptr);
    obs_sceneitem_set_locked(item, true);
    return true;
}

bool duplicate_scene_preview_enabled()
{
    if (!obs_frontend_preview_program_mode_active())
        return false;
    config_t *user = obs_frontend_get_user_config();
    if (user && config_has_user_value(user, "BasicWindow",
                                      "SceneDuplicationMode")) {
        return config_get_bool(user, "BasicWindow", "SceneDuplicationMode");
    }
    config_t *global = obs_frontend_get_global_config();
    return global && config_get_bool(global, "BasicWindow",
                                     "SceneDuplicationMode");
}

class ObsHostEventSubscription final
    : public editor_host::IHostEventSubscription {
public:
    explicit ObsHostEventSubscription(editor_host::HostEventHandler handler)
        : handler_(std::move(handler))
    {
        signal_handler_t *signal_dispatcher = obs_get_signal_handler();
        if (signal_dispatcher) {
            static const char *names[] = {
                "source_activate", "source_deactivate", "source_show",
                "source_hide", "source_create", "source_destroy"};
            for (const char *name : names)
                signal_handler_connect(signal_dispatcher, name, source_event,
                                       this);
        }
        obs_frontend_add_event_callback(frontend_event, this);
    }

    ~ObsHostEventSubscription() override
    {
        signal_handler_t *signal_dispatcher = obs_get_signal_handler();
        if (signal_dispatcher) {
            static const char *names[] = {
                "source_activate", "source_deactivate", "source_show",
                "source_hide", "source_create", "source_destroy"};
            for (const char *name : names)
                signal_handler_disconnect(signal_dispatcher, name,
                                          source_event, this);
        }
        obs_frontend_remove_event_callback(frontend_event, this);
    }

private:
    static void source_event(void *data, calldata_t *)
    {
        auto *self = static_cast<ObsHostEventSubscription *>(data);
        if (self && self->handler_)
            self->handler_(editor_host::HostEvent::SourceStateChanged);
    }

    static void frontend_event(obs_frontend_event event, void *data)
    {
        auto *self = static_cast<ObsHostEventSubscription *>(data);
        if (!self || !self->handler_)
            return;
        switch (event) {
        case OBS_FRONTEND_EVENT_SCENE_CHANGED:
            self->handler_(editor_host::HostEvent::SceneChanged);
            break;
        case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
            self->handler_(editor_host::HostEvent::ProjectScopeChanged);
            break;
        case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
        case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
        case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
            self->handler_(editor_host::HostEvent::PreviewRouteChanged);
            break;
        default:
            break;
        }
    }

    editor_host::HostEventHandler handler_;
};

class ObsLiveCuePreviewSession final
    : public editor_host::ILiveCuePreviewSession {
public:
    ObsLiveCuePreviewSession()
    {
        obs_data_t *settings = obs_data_create();
        obs_data_set_bool(settings, "editor_audio_preview", true);
        obs_data_set_bool(settings, "editor_transport_controlled", true);
        source_ = obs_source_create_private(kTitleSourceId,
                                            "FXM Cue Preview", settings);
        obs_data_release(settings);
        if (source_) {
            obs_source_inc_active(source_);
            obs_source_inc_showing(source_);
            obs_source_set_monitoring_type(source_, OBS_MONITORING_TYPE_NONE);
            obs_source_set_muted(source_, true);
            obs_source_media_play_pause(source_, true);
        }
    }

    ~ObsLiveCuePreviewSession() override
    {
        detach_from_host_preview();
        if (!source_)
            return;
        ::leavePreview(source_);
        obs_source_media_stop(source_);
        obs_source_dec_showing(source_);
        obs_source_dec_active(source_);
        obs_source_release(source_);
    }

    bool enter(const std::shared_ptr<Title> &title,
               double preview_time) override
    {
        return source_ && ::enterPreview(source_, title, preview_time);
    }

    void leave() override
    {
        detach_from_host_preview();
        if (source_)
            ::leavePreview(source_);
    }

    bool ready() const override
    {
        return !source_ || ::isPreviewReady(source_);
    }

    bool attach_to_host_preview() override
    {
        if (!source_ || !duplicate_scene_preview_enabled())
            return false;
        obs_source_t *preview_source =
            obs_frontend_get_current_preview_scene();
        if (!preview_source)
            return false;
        obs_source_t *private_source = private_scene_
            ? obs_scene_get_source(private_scene_) : nullptr;
        if (preview_item_ && private_source && preview_source == private_source) {
            obs_source_release(preview_source);
            return true;
        }

        detach_from_host_preview();
        obs_scene_t *base = obs_scene_from_source(preview_source);
        if (!base) {
            obs_source_release(preview_source);
            return false;
        }
        obs_scene_t *scene = obs_scene_duplicate(
            base, "FXM Cue Preview Scene", OBS_SCENE_DUP_PRIVATE_REFS);
        if (!scene) {
            obs_source_release(preview_source);
            return false;
        }
        obs_scene_enum_items(scene, lock_private_preview_item, nullptr);
        obs_sceneitem_t *item = obs_scene_add(scene, source_);
        if (!item) {
            obs_scene_release(scene);
            obs_source_release(preview_source);
            return false;
        }
        vec2 origin{0.0f, 0.0f};
        obs_sceneitem_set_pos(item, &origin);
        obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
        obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
        obs_sceneitem_set_bounds_alignment(item,
                                           OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
        obs_video_info video{};
        if (obs_get_video_info(&video)) {
            vec2 bounds{static_cast<float>(video.base_width),
                        static_cast<float>(video.base_height)};
            obs_sceneitem_set_bounds(item, &bounds);
        }
        obs_sceneitem_set_locked(item, true);
        obs_sceneitem_set_visible(item, true);
        obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);

        private_scene_ = scene;
        previous_preview_source_ = preview_source;
        preview_item_ = item;
        obs_frontend_set_current_preview_scene(obs_scene_get_source(scene));
        return true;
    }

    void detach_from_host_preview() override
    {
        obs_source_t *private_source = private_scene_
            ? obs_scene_get_source(private_scene_) : nullptr;
        obs_source_t *current = obs_frontend_preview_program_mode_active()
            ? obs_frontend_get_current_preview_scene() : nullptr;
        if (current && private_source && current == private_source &&
            previous_preview_source_) {
            obs_frontend_set_current_preview_scene(previous_preview_source_);
        }
        if (current)
            obs_source_release(current);
        if (preview_item_)
            obs_sceneitem_remove(preview_item_);
        if (private_scene_)
            obs_scene_release(private_scene_);
        if (previous_preview_source_)
            obs_source_release(previous_preview_source_);
        preview_item_ = nullptr;
        private_scene_ = nullptr;
        previous_preview_source_ = nullptr;
    }

private:
    obs_source_t *source_ = nullptr;
    obs_scene_t *private_scene_ = nullptr;
    obs_source_t *previous_preview_source_ = nullptr;
    obs_sceneitem_t *preview_item_ = nullptr;
};

editor_host::MediaState editor_media_state(enum obs_media_state state)
{
    switch (state) {
    case OBS_MEDIA_STATE_PLAYING:
        return editor_host::MediaState::Playing;
    case OBS_MEDIA_STATE_PAUSED:
        return editor_host::MediaState::Paused;
    case OBS_MEDIA_STATE_STOPPED:
        return editor_host::MediaState::Stopped;
    case OBS_MEDIA_STATE_ENDED:
        return editor_host::MediaState::Ended;
    case OBS_MEDIA_STATE_ERROR:
        return editor_host::MediaState::Error;
    default:
        return editor_host::MediaState::None;
    }
}

class ObsEditorAudioPreviewSession final
    : public editor_host::IEditorAudioPreviewSession {
public:
    explicit ObsEditorAudioPreviewSession(const std::string &title_id)
    {
        obs_data_t *settings = obs_data_create();
        obs_data_set_string(settings, kTitleIdSetting, title_id.c_str());
        obs_data_set_bool(settings, "editor_audio_preview", true);
        obs_data_set_bool(settings, "editor_transport_controlled", true);
        source_ = obs_source_create_private(
            kTitleSourceId, "FXM Editor Audio Preview", settings);
        obs_data_release(settings);
        if (!source_)
            return;

        obs_source_inc_active(source_);
        obs_source_inc_showing(source_);
        const char *source_name = obs_source_get_name(source_);
        if (source_name)
            name_ = source_name;

        volmeter_ = obs_volmeter_create(OBS_FADER_LOG);
        if (volmeter_ && obs_volmeter_attach_source(volmeter_, source_)) {
            config_t *profile_config = obs_frontend_get_profile_config();
            if (profile_config && config_get_uint(
                    profile_config, "Audio", "PeakMeterType") != 0) {
                obs_volmeter_set_peak_meter_type(
                    volmeter_, TRUE_PEAK_METER);
            }
            obs_volmeter_add_callback(
                volmeter_, &ObsEditorAudioPreviewSession::levels_changed,
                this);
            channel_count_ = std::clamp(
                obs_volmeter_get_nr_channels(volmeter_), 1,
                static_cast<int>(editor_host::kAudioMeterChannelCapacity));
        }
    }

    ~ObsEditorAudioPreviewSession() override
    {
        if (volmeter_) {
            obs_volmeter_remove_callback(
                volmeter_, &ObsEditorAudioPreviewSession::levels_changed,
                this);
            obs_volmeter_detach_source(volmeter_);
            obs_volmeter_destroy(volmeter_);
        }
        if (source_) {
            obs_source_media_stop(source_);
            obs_source_dec_showing(source_);
            obs_source_dec_active(source_);
            obs_source_release(source_);
        }
    }

    bool valid() const noexcept { return source_ != nullptr; }
    const std::string &name() const noexcept override { return name_; }
    bool active() const noexcept override
    {
        return source_ && obs_source_active(source_);
    }
    bool showing() const noexcept override
    {
        return source_ && obs_source_showing(source_);
    }
    int channel_count() const noexcept override { return channel_count_; }
    void set_meter_handler(editor_host::AudioMeterHandler handler) override
    {
        meter_handler_ = std::move(handler);
    }
    void set_monitoring(bool enabled) override
    {
        if (source_)
            obs_source_set_monitoring_type(
                source_, enabled ? OBS_MONITORING_TYPE_MONITOR_ONLY
                                 : OBS_MONITORING_TYPE_NONE);
    }
    int monitoring_mode() const noexcept override
    {
        return source_
            ? static_cast<int>(obs_source_get_monitoring_type(source_)) : 0;
    }
    void set_title_snapshot(const std::shared_ptr<Title> &title) override
    {
        if (source_)
            title_source_set_editor_title_snapshot(source_, title);
    }
    void set_transport(double playhead, bool reverse,
                       double speed_factor) override
    {
        if (source_)
            title_source_set_editor_transport(
                source_, playhead, reverse, speed_factor);
    }
    void seek(double playhead) override
    {
        if (source_)
            obs_source_media_set_time(
                source_, static_cast<int64_t>(
                    std::llround(playhead * 1000.0)));
    }
    void set_playing(bool playing) override
    {
        if (source_)
            obs_source_media_play_pause(source_, !playing);
    }
    void stop() override
    {
        if (source_)
            obs_source_media_stop(source_);
    }
    editor_host::MediaState media_state() const noexcept override
    {
        return source_ ? editor_media_state(obs_source_media_get_state(source_))
                       : editor_host::MediaState::None;
    }
    double media_time() const noexcept override
    {
        return source_
            ? static_cast<double>(obs_source_media_get_time(source_)) / 1000.0
            : 0.0;
    }
    bool audio_levels(float *left, float *right,
                      std::uint64_t *last_update_ns) const override
    {
        return source_ && title_source_get_audio_levels(
            source_, left, right, last_update_ns);
    }

private:
    static void levels_changed(
        void *data, const float magnitude[MAX_AUDIO_CHANNELS],
        const float peak[MAX_AUDIO_CHANNELS],
        const float input_peak[MAX_AUDIO_CHANNELS])
    {
        auto *self = static_cast<ObsEditorAudioPreviewSession *>(data);
        if (!self->meter_handler_)
            return;
        editor_host::AudioMeterLevels levels;
        const std::size_t count = std::min<std::size_t>(
            MAX_AUDIO_CHANNELS, editor_host::kAudioMeterChannelCapacity);
        for (std::size_t channel = 0; channel < count; ++channel) {
            levels.magnitude[channel] = magnitude[channel];
            levels.peak[channel] = peak[channel];
            levels.input_peak[channel] = input_peak[channel];
        }
        self->meter_handler_(levels);
    }

    obs_source_t *source_ = nullptr;
    obs_volmeter_t *volmeter_ = nullptr;
    std::string name_;
    int channel_count_ = 2;
    editor_host::AudioMeterHandler meter_handler_;
};

class ObsProgramCommandSubscription final
    : public editor_host::IProgramCommandSubscription {
public:
    explicit ObsProgramCommandSubscription(std::function<void(
        editor_host::ProgramCommand, const std::string &)> handler)
    {
        title_hotkeys_set_program_command_handler(
            [handler = std::move(handler)](TitleProgramHotkeyCommand command,
                                           const std::string &title_id) {
                if (handler) {
                    handler(static_cast<editor_host::ProgramCommand>(command),
                            title_id);
                }
            });
    }

    ~ObsProgramCommandSubscription() override
    {
        title_hotkeys_set_program_command_handler({});
    }
};

} // namespace

void *ObsEditorHost::main_window_handle() const noexcept
{
    return obs_frontend_get_main_window();
}

std::vector<editor_host::TitleSourceBinding>
ObsEditorHost::title_sources() const
{
    std::vector<editor_host::TitleSourceBinding> result;
    obs_enum_sources([](void *data, obs_source_t *source) {
        auto *items = static_cast<
            std::vector<editor_host::TitleSourceBinding> *>(data);
        if (!source || std::strcmp(obs_source_get_id(source), kTitleSourceId) != 0)
            return true;
        obs_data_t *settings = obs_source_get_settings(source);
        const char *title_id = settings
            ? obs_data_get_string(settings, kTitleIdSetting) : nullptr;
        const char *name = obs_source_get_name(source);
        items->push_back({name ? name : "", title_id ? title_id : "",
                          obs_source_active(source),
                          obs_source_showing(source)});
        if (settings)
            obs_data_release(settings);
        return true;
    }, &result);
    return result;
}

int ObsEditorHost::remove_title_sources(
    const std::vector<std::string> &title_ids)
{
    const std::set<std::string> ids(title_ids.begin(), title_ids.end());
    int count = 0;
    for (const auto &binding : title_sources()) {
        if (!ids.count(binding.title_id))
            continue;
        obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
        if (!source)
            continue;
        obs_source_remove(source);
        obs_source_release(source);
        ++count;
    }
    return count;
}

bool ObsEditorHost::add_title_to_current_scene(
    const std::string &title_id, const std::string &display_name,
    std::string *error)
{
    obs_source_t *scene_source = obs_frontend_preview_program_mode_active()
        ? obs_frontend_get_current_preview_scene() : nullptr;
    if (!scene_source)
        scene_source = obs_frontend_get_current_scene();
    if (!scene_source) {
        if (error)
            *error = "No active scene.";
        return false;
    }
    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene) {
        obs_source_release(scene_source);
        if (error)
            *error = "The active source is not a scene.";
        return false;
    }
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, kTitleIdSetting, title_id.c_str());
    obs_source_t *source = obs_source_create(
        kTitleSourceId, display_name.c_str(), settings, nullptr);
    bool added = false;
    if (source) {
        obs_sceneitem_t *item = obs_scene_add(scene, source);
        if (item) {
            vec2 position{0.0f, 0.0f};
            obs_sceneitem_set_pos(item, &position);
            obs_sceneitem_set_visible(item, true);
            added = true;
        }
        obs_source_release(source);
    }
    obs_data_release(settings);
    obs_source_release(scene_source);
    if (!added && error)
        *error = "Could not create the Flux Motion source.";
    return added;
}

bool ObsEditorHost::open_title_in_editor(
    const std::string &title_id, std::string *error)
{
    return launch_standalone_editor(&title_id, false, error);
}

bool ObsEditorHost::open_editor_preferences(std::string *error)
{
    return launch_standalone_editor(nullptr, true, error);
}

std::unique_ptr<editor_host::IHostEventSubscription>
ObsEditorHost::subscribe(editor_host::HostEventHandler handler)
{
    return std::make_unique<ObsHostEventSubscription>(std::move(handler));
}

std::unique_ptr<editor_host::IProgramCommandSubscription>
ObsEditorHost::subscribe_program_commands(std::function<void(
    editor_host::ProgramCommand, const std::string &)> handler)
{
    return std::make_unique<ObsProgramCommandSubscription>(
        std::move(handler));
}

std::unique_ptr<editor_host::ILiveCuePreviewSession>
ObsEditorHost::create_live_cue_preview_session()
{
    return std::make_unique<ObsLiveCuePreviewSession>();
}

std::unique_ptr<editor_host::IEditorAudioPreviewSession>
ObsEditorHost::create_editor_audio_preview_session(
    const std::string &title_id)
{
    auto session = std::make_unique<ObsEditorAudioPreviewSession>(title_id);
    return session->valid() ? std::move(session) : nullptr;
}

editor_host::AudioMeterConfiguration
ObsEditorHost::audio_meter_configuration() const
{
    editor_host::AudioMeterConfiguration result;
    config_t *profile_config = obs_frontend_get_profile_config();
    result.true_peak = profile_config && config_get_uint(
        profile_config, "Audio", "PeakMeterType") != 0;

    config_t *user_config = obs_frontend_get_user_config();
    result.override_colors = user_config && config_get_bool(
        user_config, "Accessibility", "OverrideColors");
    if (result.override_colors) {
        result.background_nominal = static_cast<std::uint32_t>(config_get_int(
            user_config, "Accessibility", "MixerGreen"));
        result.background_warning = static_cast<std::uint32_t>(config_get_int(
            user_config, "Accessibility", "MixerYellow"));
        result.background_error = static_cast<std::uint32_t>(config_get_int(
            user_config, "Accessibility", "MixerRed"));
        result.foreground_nominal = static_cast<std::uint32_t>(config_get_int(
            user_config, "Accessibility", "MixerGreenActive"));
        result.foreground_warning = static_cast<std::uint32_t>(config_get_int(
            user_config, "Accessibility", "MixerYellowActive"));
        result.foreground_error = static_cast<std::uint32_t>(config_get_int(
            user_config, "Accessibility", "MixerRedActive"));
    }
    return result;
}

std::vector<editor_host::AudioOutputDeviceInfo>
ObsEditorHost::audio_output_devices() const
{
    /* The OBS-hosted preview is routed by OBS' own monitor-device setting.
     * The standalone process exposes the selectable physical devices. */
    return {};
}

ObsEditorHost &obs_editor_host() noexcept
{
    static ObsEditorHost host;
    return host;
}

} // namespace fxm::obs_plugin
