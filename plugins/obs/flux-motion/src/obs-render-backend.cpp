#include "obs-render-backend.h"

#include <graphics/graphics.h>
#include <util/bmem.h>

#include <cstdint>
#include <utility>

namespace fxm::obs_plugin {
namespace {

static void set_error(std::string *error, const char *message)
{
    if (error)
        *error = message ? message : "";
}

static gs_color_format obs_texture_format(rendering::TextureFormat format)
{
    switch (format) {
    case rendering::TextureFormat::R8:
        return GS_R8;
    case rendering::TextureFormat::RGBA8:
        return GS_RGBA;
    case rendering::TextureFormat::BGRA8:
        return GS_BGRA;
    case rendering::TextureFormat::RGBA16Float:
        return GS_RGBA16F;
    case rendering::TextureFormat::Unknown:
    default:
        return GS_UNKNOWN;
    }
}

static std::uint32_t bytes_per_pixel(rendering::TextureFormat format)
{
    switch (format) {
    case rendering::TextureFormat::R8:
        return 1;
    case rendering::TextureFormat::RGBA8:
    case rendering::TextureFormat::BGRA8:
        return 4;
    case rendering::TextureFormat::RGBA16Float:
        return 8;
    case rendering::TextureFormat::Unknown:
    default:
        return 0;
    }
}

class ObsTexture final : public rendering::ITexture {
public:
    ObsTexture(gs_texture_t *texture,
               rendering::TextureDescriptor descriptor)
        : texture_(texture), descriptor_(descriptor)
    {
    }

    ~ObsTexture() override
    {
        if (texture_)
            gs_texture_destroy(texture_);
    }

    const rendering::TextureDescriptor &descriptor() const noexcept override
    {
        return descriptor_;
    }

    gs_texture_t *handle() const noexcept
    {
        return texture_;
    }

private:
    gs_texture_t *texture_ = nullptr;
    rendering::TextureDescriptor descriptor_;
};

class ObsTextureView final : public rendering::ITexture {
public:
    explicit ObsTextureView(rendering::TextureDescriptor descriptor)
        : descriptor_(descriptor)
    {
    }

    const rendering::TextureDescriptor &descriptor() const noexcept override
    {
        return descriptor_;
    }

    void set_handle(gs_texture_t *texture) const noexcept
    {
        texture_ = texture;
    }

    gs_texture_t *handle() const noexcept
    {
        return texture_;
    }

private:
    mutable gs_texture_t *texture_ = nullptr;
    rendering::TextureDescriptor descriptor_;
};

class ObsRenderTarget final : public rendering::IRenderTarget {
public:
    ObsRenderTarget(gs_texrender_t *target,
                    rendering::RenderTargetDescriptor descriptor)
        : target_(target),
          descriptor_(descriptor),
          texture_view_({
              descriptor.extent,
              descriptor.color_format,
              rendering::TextureUsage::RenderTarget,
          })
    {
    }

    ~ObsRenderTarget() override
    {
        if (target_)
            gs_texrender_destroy(target_);
    }

    const rendering::RenderTargetDescriptor &descriptor() const noexcept override
    {
        return descriptor_;
    }

    const rendering::ITexture *texture() const noexcept override
    {
        gs_texture_t *texture = target_ ? gs_texrender_get_texture(target_) : nullptr;
        texture_view_.set_handle(texture);
        return texture ? &texture_view_ : nullptr;
    }

    gs_texrender_t *handle() const noexcept
    {
        return target_;
    }

private:
    gs_texrender_t *target_ = nullptr;
    rendering::RenderTargetDescriptor descriptor_;
    mutable ObsTextureView texture_view_;
};

class ObsShader final : public rendering::IShader {
public:
    ObsShader(gs_effect_t *effect, std::string name)
        : effect_(effect), name_(std::move(name))
    {
    }

    ~ObsShader() override
    {
        if (effect_)
            gs_effect_destroy(effect_);
    }

    const std::string &name() const noexcept override
    {
        return name_;
    }

    gs_effect_t *handle() const noexcept
    {
        return effect_;
    }

private:
    gs_effect_t *effect_ = nullptr;
    std::string name_;
};

} // namespace

std::unique_ptr<rendering::ITexture> ObsRenderBackend::create_texture(
    const rendering::TextureDescriptor &descriptor,
    const rendering::TextureUpload *initial_data,
    std::string *error)
{
    return create_texture_with_native_format(
        descriptor, obs_texture_format(descriptor.format),
        initial_data, error);
}

std::unique_ptr<rendering::ITexture>
ObsRenderBackend::create_texture_with_native_format(
    const rendering::TextureDescriptor &descriptor,
    enum gs_color_format format,
    const rendering::TextureUpload *initial_data,
    std::string *error)
{
    if (error)
        error->clear();
    const std::uint32_t pixel_size =
        initial_data ? bytes_per_pixel(descriptor.format) : 0;
    if (!descriptor.extent.valid() || format == GS_UNKNOWN ||
        (initial_data && pixel_size == 0)) {
        set_error(error, "Invalid OBS texture descriptor.");
        return {};
    }

    const std::uint8_t *planes[1] = {nullptr};
    if (initial_data) {
        const std::size_t row_size =
            static_cast<std::size_t>(descriptor.extent.width) * pixel_size;
        const std::size_t required_size =
            row_size * descriptor.extent.height;
        if (!initial_data->data || initial_data->row_stride != row_size ||
            initial_data->size < required_size) {
            set_error(error, "Invalid initial OBS texture upload.");
            return {};
        }
        planes[0] = static_cast<const std::uint8_t *>(initial_data->data);
    }

    const std::uint32_t flags =
        descriptor.usage == rendering::TextureUsage::Dynamic ? GS_DYNAMIC : 0;
    gs_texture_t *texture = gs_texture_create(
        descriptor.extent.width, descriptor.extent.height, format, 1,
        initial_data ? planes : nullptr, flags);
    if (!texture) {
        set_error(error, "Could not create an OBS texture.");
        return {};
    }
    return std::make_unique<ObsTexture>(texture, descriptor);
}

bool ObsRenderBackend::upload_texture(
    rendering::ITexture &texture,
    const rendering::TextureUpload &upload,
    std::string *error)
{
    if (error)
        error->clear();
    auto *obs_texture = dynamic_cast<ObsTexture *>(&texture);
    if (!obs_texture || !obs_texture->handle() || !upload.data ||
        upload.row_stride == 0 || upload.size == 0) {
        set_error(error, "Invalid OBS texture upload.");
        return false;
    }

    const auto &descriptor = texture.descriptor();
    const std::size_t required_size =
        static_cast<std::size_t>(upload.row_stride) * descriptor.extent.height;
    if (upload.size < required_size) {
        set_error(error, "OBS texture upload data is truncated.");
        return false;
    }

    gs_texture_set_image(
        obs_texture->handle(),
        static_cast<const std::uint8_t *>(upload.data),
        upload.row_stride, false);
    return true;
}

std::unique_ptr<rendering::IRenderTarget>
ObsRenderBackend::create_render_target(
    const rendering::RenderTargetDescriptor &descriptor,
    std::string *error)
{
    if (error)
        error->clear();
    const gs_color_format format = obs_texture_format(descriptor.color_format);
    if (!descriptor.extent.valid() || format == GS_UNKNOWN) {
        set_error(error, "Invalid OBS render-target descriptor.");
        return {};
    }

    gs_texrender_t *target = gs_texrender_create(
        format, descriptor.depth_stencil ? GS_Z24_S8 : GS_ZS_NONE);
    if (!target) {
        set_error(error, "Could not create an OBS render target.");
        return {};
    }
    return std::make_unique<ObsRenderTarget>(target, descriptor);
}

std::unique_ptr<rendering::IShader> ObsRenderBackend::create_shader(
    const rendering::ShaderDescriptor &descriptor,
    std::string *error)
{
    if (error)
        error->clear();
    if (descriptor.name.empty() || descriptor.source.empty()) {
        set_error(error, "Invalid OBS shader descriptor.");
        return {};
    }

    char *compile_errors = nullptr;
    gs_effect_t *effect = gs_effect_create(
        descriptor.source.c_str(), descriptor.name.c_str(), &compile_errors);
    if (!effect) {
        if (error) {
            *error = compile_errors && *compile_errors
                ? compile_errors
                : "Could not compile an OBS shader.";
        }
        if (compile_errors)
            bfree(compile_errors);
        return {};
    }
    if (compile_errors)
        bfree(compile_errors);
    return std::make_unique<ObsShader>(effect, descriptor.name);
}

bool ObsRenderBackend::begin_frame(
    rendering::IRenderTarget &target,
    const rendering::FrameDescriptor &frame,
    std::string *error)
{
    if (error)
        error->clear();
    auto *obs_target = dynamic_cast<ObsRenderTarget *>(&target);
    if (active_target_ || !obs_target || !obs_target->handle() ||
        !frame.output_extent.valid() ||
        frame.output_extent.width != target.descriptor().extent.width ||
        frame.output_extent.height != target.descriptor().extent.height) {
        set_error(error, "Invalid OBS frame target.");
        return false;
    }

    gs_texrender_reset(obs_target->handle());
    if (!gs_texrender_begin(obs_target->handle(),
                            frame.output_extent.width,
                            frame.output_extent.height)) {
        set_error(error, "Could not begin an OBS render-target frame.");
        return false;
    }
    active_target_ = obs_target->handle();
    return true;
}

bool ObsRenderBackend::end_frame(std::string *error)
{
    if (error)
        error->clear();
    if (!active_target_) {
        set_error(error, "No OBS render-target frame is active.");
        return false;
    }
    gs_texrender_end(active_target_);
    active_target_ = nullptr;
    return true;
}

gs_texture_t *native_texture(
    const rendering::ITexture *texture) noexcept
{
    if (const auto *owned = dynamic_cast<const ObsTexture *>(texture))
        return owned->handle();
    if (const auto *view = dynamic_cast<const ObsTextureView *>(texture))
        return view->handle();
    return nullptr;
}

gs_texrender_t *native_render_target(
    const rendering::IRenderTarget *target) noexcept
{
    const auto *obs_target = dynamic_cast<const ObsRenderTarget *>(target);
    return obs_target ? obs_target->handle() : nullptr;
}

gs_effect_t *native_shader(
    const rendering::IShader *shader) noexcept
{
    const auto *obs_shader = dynamic_cast<const ObsShader *>(shader);
    return obs_shader ? obs_shader->handle() : nullptr;
}

} // namespace fxm::obs_plugin
