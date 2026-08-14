#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {
void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing: " << needle << '\n';
        assert(false);
    }
}

void forbid(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "forbidden: " << needle << '\n';
        assert(false);
    }
}

void require_layer_space_shader(const std::string &source)
{
    require(source, "uniform float2 layerUvOrigin;");
    require(source, "uniform float2 layerUvAxisX;");
    require(source, "uniform float2 layerUvAxisY;");
    require(source, "float2 layer_space_uv(float2");
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 17);
    const std::string gpu = read_file(argv[1]);
    const std::string compatibility = read_file(argv[2]);
    const std::string lifecycle = read_file(argv[3]);
    const std::string raster = read_file(argv[4]);
    const std::string background = read_file(argv[5]);
    const std::string gradient = read_file(argv[6]);
    const std::string lens_flare = read_file(argv[7]);
    const std::string vignette = read_file(argv[8]);
    const std::string noise = read_file(argv[9]);
    const std::string roughen = read_file(argv[10]);
    const std::string detail = read_file(argv[11]);
    const std::string glare = read_file(argv[12]);
    const std::string halation = read_file(argv[13]);
    const std::string finishing = read_file(argv[14]);
    const std::string keying = read_file(argv[15]);
    const std::string source_effects = read_file(argv[16]);

    /* An expanding effect may enlarge the retained local surface, but it must
     * never reroute the complete stack to a canvas-sized texture. */
    forbid(gpu, "layer_requires_full_canvas_effect_pass");
    require(gpu, "Ordinary artwork effects execute on the transform-neutral, padded layer");
    require(gpu, "apply_gpu_layer_effect_stack(");
    require(gpu, "source_height, &entry, false, composition_background, false,");
    require(gpu, "static_cast<int>(LayerEffectSpace::LayerSpace)");
    require(gpu, "static_cast<int>(LayerEffectSpace::ScreenSpace)");
    require(gpu, "gpu-effects-v17-keying-matte-v18-source-effects");

    /* Full-canvas group/matte paths receive a complete affine layer basis so
     * procedural fields follow translation, scaling and rotation. */
    require(gpu, "struct GpuEffectLayerSpace");
    require(gpu, "layerUvOrigin");
    require(gpu, "layerUvAxisX");
    require(gpu, "layerUvAxisY");
    require(gpu, "layerPixelSize");
    require(gpu, "layer_world_transform_qt");
    require(gpu, "Geometry is expressed relative to the layer box");
    require(gpu, "const double x = -resolved.effect_padding_left * scale_x;");

    /* Adding the first effect changes the retained raster contract even when
     * a padding bucket happens to have the same dimensions. */
    require(compatibility, "layerUvOrigin");
    require(compatibility, "layerUvAxisX");
    require(compatibility, "layerUvAxisY");
    require(compatibility, "layerPixelSize");
    require(compatibility, "layer_requires_preserved_effect_surface");
    require(compatibility, "case LayerEffectType::LensFlare");
    require(compatibility, "case LayerEffectType::RoughenEdges");
    require(compatibility, "static bool is_modern_gpu_pixel_effect_type");
    require(compatibility, "is_modern_gpu_pixel_effect_type(effect.type)");
    require(compatibility, "case LayerEffectType::Sharpen:");
    require(compatibility, "case LayerEffectType::UnsharpMask:");
    require(compatibility, "case LayerEffectType::HighPass:");
    require(compatibility, "case LayerEffectType::Clarity:");
    require(compatibility, "case LayerEffectType::BilateralSharpen:");
    require(compatibility, "case LayerEffectType::LensDistortion:");
    require(compatibility, "case LayerEffectType::Scanlines:");
    require(compatibility, "case LayerEffectType::ChromaKey:");
    require(compatibility, "case LayerEffectType::MatteChoker:");
    require(lifecycle, "|gpu-effect-surface=");
    require(lifecycle, "preserve_effect_surface ? std::string(\"preserved\")");
    require(raster, "result.layer_box_rect.translate(-bounds.x(), -bounds.y());");
    require(raster, "result.image_clip_rect.translate(-bounds.x(), -bounds.y());");

    require_layer_space_shader(background);
    require(background, "layer_space_uv(v_in.uv) * max(textureSize");
    require_layer_space_shader(gradient);
    require(gradient, "float2 effect_uv = layer_space_uv(v_in.uv);");
    require(gradient, "point_weight(effect_uv, point1");
    require(gradient, "random_value(effect_uv * 8192.0)");
    require_layer_space_shader(lens_flare);
    require(lens_flare, "float2 flare_pos = effect_uv - center;");
    require_layer_space_shader(vignette);
    require(vignette, "float2 p = (effect_uv - center) * 2.0;");
    require_layer_space_shader(noise);
    require(noise, "float2 p=(px+noiseOffset)/max(scale");
    require_layer_space_shader(roughen);
    require(roughen, "float2 p = effect_uv * max(scale");
    require(detail, "uniform texture2d blurredImage;");
    require(detail, "technique BilateralSharpen");
    require(glare, "uniform texture2d blurredImage;");
    require(glare, "technique GlareComposite");
    require(halation, "uniform texture2d blurredImage;");
    require(halation, "technique HalationComposite");
    require(finishing, "technique LensDistortion");
    require(finishing, "technique Scanlines");
    require(keying, "technique ChromaKey");
    require(keying, "technique MatteChoker");
    require(keying, "float4 PSChromaKey(VertDataOut v_in) : TARGET");
    require(source_effects, "uniform float2 layerUvOrigin;");
    require(source_effects, "uniform float2 layerUvAxisX;");
    require(source_effects, "uniform float2 layerUvAxisY;");
    require(source_effects, "uniform int inputIsComposition;");
    require(source_effects, "float2 composition_uv(float2 local_uv)");
    require(source_effects, "float2 source_space_uv(float2 input_uv)");
    require(source_effects, "technique LightWrap");
    require(source_effects, "technique DisplacementMap");

    std::cout << "effects stack layer-space/bounds contract: PASS\n";
    return 0;
}
