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
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 14);
    const std::string effects_model = read_file(argv[1]);
    const std::string registry = read_file(argv[2]);
    const std::string extension_catalog = read_file(argv[3]);
    const std::string effects_panel = read_file(argv[4]);
    const std::string gpu_pipeline = read_file(argv[5]);
    const std::string compatibility = read_file(argv[6]);
    const std::string background_shader = read_file(argv[7]);
    const std::string emboss_shader = read_file(argv[8]);
    const std::string gradient_shader = read_file(argv[9]);
    const std::string gradient_preset = read_file(argv[10]);
    const std::string asset_runtime = read_file(argv[11]);
    const std::string source_runtime = read_file(argv[12]);
    const std::string preset_catalog = read_file(argv[13]);

    require(effects_model, "FourColorGradient = 18");
    require(registry, "fxm.builtin.4-color-gradient");
    require(registry, "Built-in/Generate");
    require(registry, "shaders/4-color-gradient/4-color-gradient.effect");

    require(extension_catalog, "QStringLiteral(\"point1\")");
    require(extension_catalog, "QStringLiteral(\"point4\")");
    require(extension_catalog, "QStringLiteral(\"color1\")");
    require(extension_catalog, "QStringLiteral(\"color4\")");
    require(extension_catalog, "QStringLiteral(\"blend\")");
    require(extension_catalog, "QStringLiteral(\"jitter\")");
    require(extension_catalog, "QStringLiteral(\"opacity\")");
    require(extension_catalog, "QStringLiteral(\"animatable\"), true");
    require(extension_catalog, "QStringLiteral(\"enum\")");

    require(effects_panel, "selected_effect()->type == LayerEffectType::FourColorGradient");
    require(effects_panel, "make_effect_keyframe_button");
    require(effects_panel, "keyframe_diamond_icon(false)");
    require(effects_panel, "keyframe_diamond_icon(keyed, outlined)");
    forbid(effects_panel, "QStringLiteral(\"◇\")");
    forbid(effects_panel, "QStringLiteral(\"◆\")");
    require(effects_panel, "Toggle extension keyframe at the current timeline position");
    require(effects_panel, "Toggle extension color keyframe at the current timeline position");
    require(effects_panel, "type == QStringLiteral(\"enum\")");
    require(effects_panel, "return fxm::effects::animation::evaluate_track(effect, path, time, fallback);");
    require(effects_panel, "handle.insert(QStringLiteral(\"minX\"), meta.value(QStringLiteral(\"minX\"))");
    forbid(effects_panel, "add_shadow_blur_items");
    forbid(effects_panel, "LongShadowBlurType");
    forbid(effects_panel, "effect_blur_type");

    require(gpu_pipeline, "declaredType == QStringLiteral(\"enum\")");
    require(gpu_pipeline, "set_gpu_background_geometry_params");
    require(gpu_pipeline, "gradientStartColor");
    require(gpu_pipeline, "gradientFocal");
    require(compatibility, "set_gpu_extension_surface_params");
    require(compatibility, "gs_effect_set_texture(hard_mask, source)");
    require(compatibility, "resolved.type == LayerEffectType::Emboss");
    require(compatibility, "set_effect_float_param(effect, \"depth\"");
    require(compatibility, "set_effect_float_param(effect, \"height\"");

    require(background_shader, "uniform int fillType;");
    require(background_shader, "uniform float4 backgroundRect;");
    require(background_shader, "return over(base, under);");
    require(emboss_shader, "uniform float2 direction;");
    require(emboss_shader, "uniform float depth;");
    require(emboss_shader, "uniform float height;");
    require(emboss_shader, "float slope = clamp");

    require(gradient_shader, "uniform float2 point1;");
    require(gradient_shader, "uniform float2 point4;");
    require(gradient_shader, "uniform float4 color1;");
    require(gradient_shader, "uniform float4 color4;");
    require(gradient_shader, "uniform float blend;");
    require(gradient_shader, "uniform float jitter;");
    require(gradient_shader, "uniform int blendMode;");
    require(gradient_shader, "point_weight");
    require(gradient_shader, "uniform texture2d hardMask;");
    require(gradient_shader, "float artworkAlpha = hardMask.Sample");
    require(gradient_shader, "return base;");
    require(gradient_shader, "float outputAlpha = base.a;");
    require(gradient_shader, "float3 outputRgb = outputStraight * outputAlpha;");
    require(gradient_preset, "\"type\": \"4-color-gradient\"");
    require(asset_runtime, "effect.extension_keyframes_json != \"{}\"");
    require(source_runtime, "effect.extension_keyframes_json != \"{}\"");
    require(preset_catalog, "effect.extension_id = FxmEffectExtensionCatalog::builtInId(type).toStdString();");

    std::cout << "effects cleanup / keyframes / 4-color gradient contract: PASS\n";
    return 0;
}
