#pragma once

#include "asset-path-provider.h"
#include "editor-host-interfaces.h"
#include "frame-rate-provider.h"
#include "host-context-provider.h"
#include "logger.h"
#include "translation-provider.h"

#include <memory>
#include <atomic>
#include <cmath>
#include <QString>
#include <string>

class QMainWindow;

namespace fxm::editor {

/* Resolve the resource root once during standalone bootstrap. Installed
 * builds must use the data directory beside the executable; the compiled
 * source-tree location is only a development fallback. */
QString resolve_standalone_data_root(const QString &explicit_root = {});

class StandaloneEditorHost final : public editor_host::IEditorHost {
public:
    explicit StandaloneEditorHost(QMainWindow *window) : window_(window) {}
    void set_main_window(QMainWindow *window) noexcept { window_ = window; }

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

private:
    QMainWindow *window_ = nullptr;
};

class StandaloneAssetPathProvider final : public IAssetPathProvider {
public:
    explicit StandaloneAssetPathProvider(std::string data_root = {});
    std::string resolve_asset_path(
        std::string_view relative_path) const override;

private:
    std::string data_root_;
};

class StandaloneHostContextProvider final : public IHostContextProvider {
public:
    StandaloneHostContextProvider(std::string config_root = {},
                                  std::string project_scope = {});
    std::string config_path(
        std::string_view relative_path) const override;
    std::string project_scope_name() const override;

private:
    std::string config_root_;
    std::string project_scope_;
};

class StandaloneFrameRateProvider final : public IFrameRateProvider {
public:
    explicit StandaloneFrameRateProvider(double frame_rate = 30.0,
                                         bool accept_document_rate = true)
        : frame_rate_(frame_rate),
          accept_document_rate_(accept_document_rate)
    {
    }

    double frame_rate() const noexcept override
    {
        return frame_rate_.load(std::memory_order_relaxed);
    }
    void set_document_frame_rate(double frame_rate) const noexcept override
    {
        if (accept_document_rate_ && std::isfinite(frame_rate) &&
            frame_rate > 0.0)
            frame_rate_.store(frame_rate, std::memory_order_relaxed);
    }

private:
    mutable std::atomic<double> frame_rate_{30.0};
    bool accept_document_rate_ = true;
};

class StandaloneLogger final : public ILogger {
public:
    bool enabled(LogLevel, const std::string &) const override { return true; }
    void log(LogLevel level, const std::string &category,
             const std::string &message) override;
};

class StandaloneTranslationProvider final : public ITranslationProvider {
public:
    explicit StandaloneTranslationProvider(std::string data_root = {});
    const char *translate(const char *key) const noexcept override;

private:
    struct Storage;
    std::shared_ptr<Storage> storage_;
};

} // namespace fxm::editor
