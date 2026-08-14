#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace fxm::rendering {

enum class TextureFormat : std::uint8_t {
    Unknown,
    R8,
    RGBA8,
    BGRA8,
    RGBA16Float,
};

enum class TextureUsage : std::uint8_t {
    Immutable,
    Dynamic,
    RenderTarget,
};

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    constexpr bool valid() const noexcept
    {
        return width > 0 && height > 0;
    }
};

struct TextureDescriptor {
    Extent2D extent;
    TextureFormat format = TextureFormat::Unknown;
    TextureUsage usage = TextureUsage::Immutable;
};

struct TextureUpload {
    const void *data = nullptr;
    std::size_t size = 0;
    std::uint32_t row_stride = 0;
};

struct RenderTargetDescriptor {
    Extent2D extent;
    TextureFormat color_format = TextureFormat::BGRA8;
    bool depth_stencil = false;
};

struct ShaderDescriptor {
    std::string name;
    std::string source;
};

struct FrameDescriptor {
    Extent2D output_extent;
    double time_seconds = 0.0;
    std::uint64_t frame_index = 0;
};

class ITexture {
public:
    virtual ~ITexture() = default;
    virtual const TextureDescriptor &descriptor() const noexcept = 0;
};

class ITextureProvider {
public:
    virtual ~ITextureProvider() = default;
    virtual const ITexture *texture() const noexcept = 0;
};

class IRenderTarget : public ITextureProvider {
public:
    ~IRenderTarget() override = default;
    virtual const RenderTargetDescriptor &descriptor() const noexcept = 0;
};

class IShader {
public:
    virtual ~IShader() = default;
    virtual const std::string &name() const noexcept = 0;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual std::unique_ptr<ITexture> create_texture(
        const TextureDescriptor &descriptor,
        const TextureUpload *initial_data,
        std::string *error) = 0;
    virtual bool upload_texture(ITexture &texture,
                                const TextureUpload &upload,
                                std::string *error) = 0;
    virtual std::unique_ptr<IRenderTarget> create_render_target(
        const RenderTargetDescriptor &descriptor,
        std::string *error) = 0;
    virtual std::unique_ptr<IShader> create_shader(
        const ShaderDescriptor &descriptor,
        std::string *error) = 0;

    virtual bool begin_frame(IRenderTarget &target,
                             const FrameDescriptor &frame,
                             std::string *error) = 0;
    virtual bool end_frame(std::string *error) = 0;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool render(IRenderBackend &backend,
                        IRenderTarget &target,
                        const FrameDescriptor &frame,
                        std::string *error) = 0;
};

class IFrameRenderer : public ITextureProvider {
public:
    ~IFrameRenderer() override = default;
    virtual bool render_frame(IRenderBackend &backend,
                              const FrameDescriptor &frame,
                              std::string *error) = 0;
};

} // namespace fxm::rendering
