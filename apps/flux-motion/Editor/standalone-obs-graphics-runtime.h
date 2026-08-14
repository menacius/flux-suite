#pragma once

#include <QString>
#include <cstdint>

namespace fxm::editor {

/* Owns the private libobs video/graphics runtime used by the standalone
 * editor. It does not register sources or create outputs; it only provides the
 * same graphics device, effect compiler, texture/render-target implementation
 * and swap-chain API used by the OBS live-output compositor. */
class StandaloneObsGraphicsRuntime final {
public:
    explicit StandaloneObsGraphicsRuntime(
        QString obs_bin_root = {}, std::uint32_t canvas_width = 1920,
        std::uint32_t canvas_height = 1080, std::uint32_t fps_num = 60,
        std::uint32_t fps_den = 1);
    ~StandaloneObsGraphicsRuntime();

    StandaloneObsGraphicsRuntime(const StandaloneObsGraphicsRuntime &) = delete;
    StandaloneObsGraphicsRuntime &operator=(
        const StandaloneObsGraphicsRuntime &) = delete;

    bool ready() const noexcept { return ready_; }
    const QString &error() const noexcept { return error_; }
    const QString &graphics_module_path() const noexcept
    {
        return graphics_module_path_;
    }

private:
    bool started_ = false;
    bool ready_ = false;
    QString error_;
    QString graphics_module_path_;
};

} // namespace fxm::editor
