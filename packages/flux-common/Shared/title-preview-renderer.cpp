#include "title-preview-renderer.h"

#include <QImage>

#include <atomic>

namespace {
std::atomic<fxm::rendering::ITitlePreviewRenderer *> g_renderer{nullptr};
}

namespace fxm::rendering {

void set_title_preview_renderer(ITitlePreviewRenderer *renderer) noexcept
{
    g_renderer.store(renderer, std::memory_order_release);
}

ITitlePreviewRenderer *title_preview_renderer() noexcept
{
    return g_renderer.load(std::memory_order_acquire);
}

} // namespace fxm::rendering

QImage render_title_to_image(const Title &title, double time,
                             std::uint64_t model_revision)
{
    auto *renderer = fxm::rendering::title_preview_renderer();
    return renderer ? renderer->render_title(title, time, model_revision)
                    : QImage();
}
