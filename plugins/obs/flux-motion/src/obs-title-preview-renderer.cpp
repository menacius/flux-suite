#include "obs-title-preview-renderer.h"

#include "title-source.h"

namespace fxm::obs_plugin {
namespace {

class ObsTitlePreviewRenderer final
    : public fxm::rendering::ITitlePreviewRenderer {
public:
    QImage render_title(const Title &title, double time,
                        std::uint64_t model_revision) override
    {
        return render_title_to_image_obs(title, time, model_revision);
    }
};

} // namespace

fxm::rendering::ITitlePreviewRenderer &obs_title_preview_renderer() noexcept
{
    static ObsTitlePreviewRenderer renderer;
    return renderer;
}

} // namespace fxm::obs_plugin
