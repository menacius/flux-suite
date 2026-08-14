#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Title;

namespace fxm::editor_host {

struct TitleSourceBinding {
    std::string source_name;
    std::string title_id;
    bool active = false;
    bool showing = false;
};

enum class HostEvent {
    SourceStateChanged,
    SceneChanged,
    ProjectScopeChanged,
    PreviewRouteChanged,
};

enum class ProgramCommand {
    CueToProgram,
    Uncue,
    CueLast,
    NextCue,
    PreviousCue,
};

using HostEventHandler = std::function<void(HostEvent)>;

inline constexpr std::size_t kAudioMeterChannelCapacity = 8;

struct AudioMeterLevels {
    std::array<float, kAudioMeterChannelCapacity> magnitude{};
    std::array<float, kAudioMeterChannelCapacity> peak{};
    std::array<float, kAudioMeterChannelCapacity> input_peak{};
};

struct AudioMeterConfiguration {
    bool true_peak = false;
    bool override_colors = false;
    std::uint32_t background_nominal = 0;
    std::uint32_t background_warning = 0;
    std::uint32_t background_error = 0;
    std::uint32_t foreground_nominal = 0;
    std::uint32_t foreground_warning = 0;
    std::uint32_t foreground_error = 0;
};

struct AudioOutputDeviceInfo {
    std::string id;
    std::string description;
    bool is_default = false;
};

enum class MediaState {
    None,
    Playing,
    Paused,
    Stopped,
    Ended,
    Error,
};

using AudioMeterHandler = std::function<void(const AudioMeterLevels &)>;

class IHostEventSubscription {
public:
    virtual ~IHostEventSubscription() = default;
};

class IProgramCommandSubscription {
public:
    virtual ~IProgramCommandSubscription() = default;
};

class ILiveCuePreviewSession {
public:
    virtual ~ILiveCuePreviewSession() = default;
    virtual bool enter(const std::shared_ptr<Title> &title,
                       double preview_time) = 0;
    virtual void leave() = 0;
    virtual bool ready() const = 0;
    virtual bool attach_to_host_preview() = 0;
    virtual void detach_from_host_preview() = 0;
};

class IEditorAudioPreviewSession {
public:
    virtual ~IEditorAudioPreviewSession() = default;
    virtual const std::string &name() const noexcept = 0;
    virtual bool active() const noexcept = 0;
    virtual bool showing() const noexcept = 0;
    virtual int channel_count() const noexcept = 0;
    virtual void set_meter_handler(AudioMeterHandler handler) = 0;
    virtual void set_monitoring(bool enabled) = 0;
    virtual int monitoring_mode() const noexcept = 0;
    virtual void set_title_snapshot(const std::shared_ptr<Title> &title) = 0;
    virtual void set_transport(double playhead, bool reverse,
                               double speed_factor) = 0;
    virtual void seek(double playhead) = 0;
    virtual void set_playing(bool playing) = 0;
    virtual void stop() = 0;
    virtual MediaState media_state() const noexcept = 0;
    virtual double media_time() const noexcept = 0;
    virtual bool audio_levels(float *left, float *right,
                              std::uint64_t *last_update_ns) const = 0;
};

class IEditorHost {
public:
    virtual ~IEditorHost() = default;
    virtual void *main_window_handle() const noexcept = 0;
    virtual std::vector<TitleSourceBinding> title_sources() const = 0;
    virtual int remove_title_sources(
        const std::vector<std::string> &title_ids) = 0;
    virtual bool add_title_to_current_scene(
        const std::string &title_id, const std::string &display_name,
        std::string *error) = 0;
    virtual bool open_title_in_editor(
        const std::string &title_id, std::string *error) = 0;
    virtual bool open_editor_preferences(std::string *error) = 0;
    virtual std::unique_ptr<IHostEventSubscription> subscribe(
        HostEventHandler handler) = 0;
    virtual std::unique_ptr<IProgramCommandSubscription>
        subscribe_program_commands(
            std::function<void(ProgramCommand, const std::string &)> handler) = 0;
    virtual std::unique_ptr<ILiveCuePreviewSession>
        create_live_cue_preview_session() = 0;
    virtual std::unique_ptr<IEditorAudioPreviewSession>
        create_editor_audio_preview_session(
            const std::string &title_id) = 0;
    virtual AudioMeterConfiguration audio_meter_configuration() const = 0;
    virtual std::vector<AudioOutputDeviceInfo>
        audio_output_devices() const = 0;
};

void set_editor_host(IEditorHost *host) noexcept;
IEditorHost *editor_host() noexcept;

} // namespace fxm::editor_host
