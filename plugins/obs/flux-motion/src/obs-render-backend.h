#pragma once

#include "rendering-interfaces.h"

#include <graphics/graphics.h>

namespace fxm::obs_plugin {

/* OBS graphics resources must be created, used and destroyed while the caller
 * holds the OBS graphics context. The backend deliberately does not enter or
 * leave that context so existing render-thread ownership remains unchanged. */
class ObsRenderBackend final : public rendering::IRenderBackend {
public:
    ~ObsRenderBackend() override = default;

    std::unique_ptr<rendering::ITexture> create_texture(
        const rendering::TextureDescriptor &descriptor,
        const rendering::TextureUpload *initial_data,
        std::string *error) override;
    std::unique_ptr<rendering::ITexture> create_texture_with_native_format(
        const rendering::TextureDescriptor &descriptor,
        enum gs_color_format format,
        const rendering::TextureUpload *initial_data,
        std::string *error);
    bool upload_texture(rendering::ITexture &texture,
                        const rendering::TextureUpload &upload,
                        std::string *error) override;
    std::unique_ptr<rendering::IRenderTarget> create_render_target(
        const rendering::RenderTargetDescriptor &descriptor,
        std::string *error) override;
    std::unique_ptr<rendering::IShader> create_shader(
        const rendering::ShaderDescriptor &descriptor,
        std::string *error) override;
    bool begin_frame(rendering::IRenderTarget &target,
                     const rendering::FrameDescriptor &frame,
                     std::string *error) override;
    bool end_frame(std::string *error) override;

private:
    gs_texrender_t *active_target_ = nullptr;
};

gs_texture_t *native_texture(
    const rendering::ITexture *texture) noexcept;
gs_texrender_t *native_render_target(
    const rendering::IRenderTarget *target) noexcept;
gs_effect_t *native_shader(
    const rendering::IShader *shader) noexcept;

} // namespace fxm::obs_plugin
