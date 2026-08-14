#include "rendering-interfaces.h"

#include <type_traits>

using namespace fxm::rendering;

static_assert(std::is_abstract_v<ITexture>);
static_assert(std::is_abstract_v<ITextureProvider>);
static_assert(std::is_abstract_v<IRenderTarget>);
static_assert(std::is_abstract_v<IShader>);
static_assert(std::is_abstract_v<IRenderBackend>);
static_assert(std::is_abstract_v<IRenderer>);
static_assert(std::is_abstract_v<IFrameRenderer>);

static_assert(std::has_virtual_destructor_v<ITexture>);
static_assert(std::has_virtual_destructor_v<ITextureProvider>);
static_assert(std::has_virtual_destructor_v<IRenderTarget>);
static_assert(std::has_virtual_destructor_v<IShader>);
static_assert(std::has_virtual_destructor_v<IRenderBackend>);
static_assert(std::has_virtual_destructor_v<IRenderer>);
static_assert(std::has_virtual_destructor_v<IFrameRenderer>);

int main()
{
    const Extent2D valid_extent {1920, 1080};
    const Extent2D invalid_extent {};
    const TextureDescriptor texture {
        valid_extent,
        TextureFormat::BGRA8,
        TextureUsage::RenderTarget,
    };
    const FrameDescriptor frame {valid_extent, 1.25, 75};

    return valid_extent.valid() &&
                   !invalid_extent.valid() &&
                   texture.extent.width == frame.output_extent.width &&
                   texture.format == TextureFormat::BGRA8 &&
                   texture.usage == TextureUsage::RenderTarget
               ? 0
               : 1;
}
