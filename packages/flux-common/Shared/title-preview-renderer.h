#pragma once

#include <cstdint>

class QImage;
struct Title;

namespace fxm::rendering {

class ITitlePreviewRenderer {
public:
    virtual ~ITitlePreviewRenderer() = default;
    virtual QImage render_title(const Title &title, double time,
                                std::uint64_t model_revision) = 0;
};

void set_title_preview_renderer(ITitlePreviewRenderer *renderer) noexcept;
ITitlePreviewRenderer *title_preview_renderer() noexcept;

} // namespace fxm::rendering

QImage render_title_to_image(const Title &title, double time,
                             std::uint64_t model_revision = 0);
