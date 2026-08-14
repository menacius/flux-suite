#pragma once

#include "editor-host-interfaces.h"

namespace fxm::obs_plugin {

class ObsEditorHost final : public editor_host::IEditorHost {
public:
    void *main_window_handle() const noexcept override;
    std::vector<editor_host::TitleSourceBinding> title_sources()
        const override;
    int remove_title_sources(
        const std::vector<std::string> &title_ids) override;
    bool add_title_to_current_scene(
        const std::string &title_id, const std::string &display_name,
        std::string *error) override;
    bool open_title_in_editor(
        const std::string &title_id, std::string *error) override;
    bool open_editor_preferences(std::string *error) override;
    std::unique_ptr<editor_host::IHostEventSubscription> subscribe(
        editor_host::HostEventHandler handler) override;
    std::unique_ptr<editor_host::IProgramCommandSubscription>
        subscribe_program_commands(std::function<void(
            editor_host::ProgramCommand, const std::string &)> handler) override;
    std::unique_ptr<editor_host::ILiveCuePreviewSession>
        create_live_cue_preview_session() override;
    std::unique_ptr<editor_host::IEditorAudioPreviewSession>
        create_editor_audio_preview_session(
            const std::string &title_id) override;
    editor_host::AudioMeterConfiguration
        audio_meter_configuration() const override;
    std::vector<editor_host::AudioOutputDeviceInfo>
        audio_output_devices() const override;
};

ObsEditorHost &obs_editor_host() noexcept;

} // namespace fxm::obs_plugin
