#include "effect-runtime.h"

#include "performance-counters.h"
#include "title-data.h"

#include <algorithm>
#include <array>
#include <cmath>

bool title_requires_destination_compositing(const Title &title)
{
    for (const auto &layer_ptr : title.layers) {
        if (!layer_ptr)
            continue;
        const Layer &layer = *layer_ptr;
        if (layer.type == LayerType::Adjustment ||
            layer.blend_mode != EffectBlendMode::Normal)
            return true;
        for (const LayerEffect &effect : layer.effects) {
            if (effect.enabled && effect.affect_layers_behind)
                return true;
        }
    }
    return false;
}

namespace {

constexpr EffectParameterDescriptor kTrimPathsParameters[] = {
    {"effect_trim_start", EffectParameterKind::Scalar, 0.0, 0.0, 100.0, 0.1, true},
    {"effect_trim_end", EffectParameterKind::Scalar, 100.0, 0.0, 100.0, 0.1, true},
    {"effect_trim_offset", EffectParameterKind::Angle, 0.0, -1000000000.0, 1000000000.0, 0.1, true},
    {"effect_trim_multiple_shapes", EffectParameterKind::Enumeration, 0.0, 0.0, 1.0, 1.0, false},
};
constexpr EffectParameterDescriptor kAppearanceParameters[] = {
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_opacity", EffectParameterKind::Scalar, 1.0, 0.0, 1.0, 0.01, true},
    {"effect_size", EffectParameterKind::Scalar, 16.0, 0.0, 4096.0, 0.1, true},
};
constexpr EffectParameterDescriptor kShadowParameters[] = {
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_opacity", EffectParameterKind::Scalar, 1.0, 0.0, 1.0, 0.01, true},
    {"effect_size", EffectParameterKind::Scalar, 16.0, 0.0, 4096.0, 0.1, true},
    {"effect_distance", EffectParameterKind::Scalar, 8.0, 0.0, 16384.0, 0.1, true},
    {"effect_angle", EffectParameterKind::Angle, 135.0, -1000000000.0, 1000000000.0, 0.1, true},
    {"effect_spread", EffectParameterKind::Scalar, 0.0, 0.0, 4096.0, 0.1, true},
};
constexpr EffectParameterDescriptor kColorParameters[] = {
    {"effect_brightness", EffectParameterKind::Scalar, 0.0, -1.0, 1.0, 0.01, true},
    {"effect_contrast", EffectParameterKind::Scalar, 1.0, 0.0, 4.0, 0.01, true},
    {"effect_saturation", EffectParameterKind::Scalar, 1.0, 0.0, 4.0, 0.01, true},
};
constexpr EffectParameterDescriptor kProceduralParameters[] = {
    {"effect_amount", EffectParameterKind::Scalar, 1.0, 0.0, 100.0, 0.01, true},
    {"effect_scale", EffectParameterKind::Scalar, 1.0, 0.001, 4096.0, 0.01, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.25, 0.0, 1.0, 0.01, true},
    {"effect_evolution", EffectParameterKind::Scalar, 0.0, -1000000000.0, 1000000000.0, 0.01, true},
    {"effect_center", EffectParameterKind::Point, 0.5, -10.0, 10.0, 0.001, true},
};
constexpr EffectParameterDescriptor kNoiseParameters[] = {
    {"effect_profile", EffectParameterKind::Enumeration, 9.0, 0.0, 13.0, 1.0, false},
    {"effect_amount", EffectParameterKind::Scalar, 0.12, 0.0, 4.0, 0.01, true},
    {"effect_scale", EffectParameterKind::Scalar, 1.0, 0.001, 4096.0, 0.01, true},
    {"effect_roundness", EffectParameterKind::Scalar, 0.0, -3.0, 3.0, 0.01, true},
    {"effect_center", EffectParameterKind::Point, 0.0, -100000.0, 100000.0, 0.01, true},
    {"effect_complexity", EffectParameterKind::Scalar, 5.0, 1.0, 8.0, 1.0, true},
    {"effect_spread", EffectParameterKind::Scalar, 2.0, 1.01, 8.0, 0.01, true},
    {"effect_falloff", EffectParameterKind::Scalar, 0.5, 0.0, 1.0, 0.01, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.0, 0.0, 1.0, 0.01, true},
    {"effect_brightness", EffectParameterKind::Scalar, 0.0, -1.0, 1.0, 0.01, true},
    {"effect_contrast", EffectParameterKind::Scalar, 1.0, 0.0, 4.0, 0.01, true},
    {"effect_evolution", EffectParameterKind::Scalar, 0.0, -1000000000.0, 1000000000.0, 0.01, true},
    {"effect_speed", EffectParameterKind::Scalar, 1.0, -1000.0, 1000.0, 0.01, true},
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
};
constexpr EffectParameterDescriptor kDamageParameters[] = {
    {"effect_amount", EffectParameterKind::Scalar, 0.35, 0.0, 1.0, 0.01, true},
    {"effect_scale", EffectParameterKind::Scalar, 8.0, 0.001, 4096.0, 0.01, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.15, 0.0, 1.0, 0.01, true},
    {"effect_complexity", EffectParameterKind::Scalar, 6.0, 1.0, 12.0, 0.1, true},
    {"effect_spread", EffectParameterKind::Scalar, 2.0, 1.01, 8.0, 0.01, true},
    {"effect_falloff", EffectParameterKind::Scalar, 0.5, 0.0, 1.0, 0.01, true},
    {"effect_roundness", EffectParameterKind::Scalar, 0.0, -3.0, 3.0, 0.01, true},
    {"effect_center", EffectParameterKind::Point, 0.0, -100000.0, 100000.0, 0.01, true},
    {"effect_brightness", EffectParameterKind::Scalar, 0.0, -1.0, 1.0, 0.01, true},
    {"effect_contrast", EffectParameterKind::Scalar, 1.0, 0.0, 4.0, 0.01, true},
    {"effect_evolution", EffectParameterKind::Scalar, 0.0, -1000000000.0, 1000000000.0, 0.01, true},
    {"effect_speed", EffectParameterKind::Scalar, 1.0, -1000.0, 1000.0, 0.01, true},
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_secondary_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
};
constexpr EffectParameterDescriptor kOpticalParameters[] = {
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_secondary_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_opacity", EffectParameterKind::Scalar, 0.8, 0.0, 1.0, 0.01, true},
    {"effect_amount", EffectParameterKind::Scalar, 1.0, 0.0, 8.0, 0.01, true},
    {"effect_size", EffectParameterKind::Scalar, 24.0, 0.0, 512.0, 0.1, true},
    {"effect_distance", EffectParameterKind::Scalar, 180.0, 0.0, 4096.0, 1.0, true},
    {"effect_angle", EffectParameterKind::Angle, 0.0, -1000000000.0, 1000000000.0, 0.1, true},
    {"effect_spread", EffectParameterKind::Scalar, 0.7, 0.0, 1.0, 0.01, true},
    {"effect_falloff", EffectParameterKind::Scalar, 1.0, 0.0, 8.0, 0.01, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.25, 0.0, 1.0, 0.01, true},
};
constexpr EffectParameterDescriptor kDetailParameters[] = {
    {"effect_amount", EffectParameterKind::Scalar, 0.75, 0.0, 4.0, 0.01, true},
    {"effect_size", EffectParameterKind::Scalar, 2.0, 0.25, 64.0, 0.05, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.04, 0.0, 1.0, 0.005, true},
    {"effect_spread", EffectParameterKind::Scalar, 0.25, 0.0, 1.0, 0.01, true},
    {"effect_falloff", EffectParameterKind::Scalar, 0.25, 0.0, 1.0, 0.01, true},
    {"effect_brightness", EffectParameterKind::Scalar, 0.0, -1.0, 1.0, 0.01, true},
};
constexpr EffectParameterDescriptor kFinishingParameters[] = {
    {"effect_amount", EffectParameterKind::Scalar, 1.0, -100.0, 100.0, 0.01, true},
    {"effect_size", EffectParameterKind::Scalar, 8.0, 0.0, 4096.0, 0.1, true},
    {"effect_scale", EffectParameterKind::Scalar, 8.0, 0.001, 4096.0, 0.01, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.25, 0.0, 1.0, 0.01, true},
    {"effect_roundness", EffectParameterKind::Scalar, 0.0, -3.0, 3.0, 0.01, true},
    {"effect_angle", EffectParameterKind::Angle, 0.0, -1000000000.0, 1000000000.0, 0.1, true},
    {"effect_center", EffectParameterKind::Point, 0.5, -10.0, 10.0, 0.001, true},
    {"effect_evolution", EffectParameterKind::Scalar, 0.0, -1000000000.0, 1000000000.0, 0.01, true},
    {"effect_complexity", EffectParameterKind::Scalar, 6.0, 2.0, 64.0, 1.0, true},
};
constexpr EffectParameterDescriptor kKeyingParameters[] = {
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_opacity", EffectParameterKind::Scalar, 1.0, 0.0, 1.0, 0.01, true},
    {"effect_amount", EffectParameterKind::Scalar, 0.25, -1.0, 1.0, 0.01, true},
    {"effect_softness", EffectParameterKind::Scalar, 0.10, 0.0, 1.0, 0.01, true},
    {"effect_spread", EffectParameterKind::Scalar, 0.25, 0.0, 1.0, 0.01, true},
    {"effect_falloff", EffectParameterKind::Scalar, 0.20, 0.0, 1.0, 0.01, true},
    {"effect_size", EffectParameterKind::Scalar, 2.0, 0.0, 64.0, 0.1, true},
    {"effect_invert", EffectParameterKind::Boolean, 0.0, 0.0, 1.0, 1.0, false},
};
constexpr EffectParameterDescriptor kLightWrapParameters[] = {
    {"effect_size", EffectParameterKind::Scalar, 24.0, 0.0, 512.0, 0.1, true},
    {"effect_amount", EffectParameterKind::Scalar, 1.0, 0.0, 8.0, 0.01, true},
    {"effect_spread", EffectParameterKind::Scalar, 12.0, 0.0, 512.0, 0.1, true},
    {"effect_color", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"effect_falloff", EffectParameterKind::Scalar, 0.5, 0.0, 1.0, 0.01, true},
};
constexpr EffectParameterDescriptor kDisplacementMapParameters[] = {
    {"effect_amount", EffectParameterKind::Scalar, 20.0, -4096.0, 4096.0, 0.1, true},
    {"effect_distance", EffectParameterKind::Scalar, 20.0, -4096.0, 4096.0, 0.1, true},
};
constexpr EffectParameterDescriptor kMotionParameters[] = {
    {"effect_opacity", EffectParameterKind::Scalar, 1.0, 0.0, 1.0, 0.01, true},
    {"effect_size", EffectParameterKind::Angle, 180.0, 0.0, 720.0, 1.0, true},
    {"effect_samples", EffectParameterKind::Integer, 8.0, 2.0, 64.0, 1.0, false},
};
constexpr EffectParameterDescriptor kGradientParameters[] = {
    {"point1", EffectParameterKind::Point, 0.25, -2.0, 3.0, 0.01, true},
    {"color1", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"point2", EffectParameterKind::Point, 0.75, -2.0, 3.0, 0.01, true},
    {"color2", EffectParameterKind::Color, 1.0, 0.0, 1.0, 1.0, true},
    {"blend", EffectParameterKind::Scalar, 50.0, 0.0, 1000.0, 1.0, true},
    {"jitter", EffectParameterKind::Scalar, 0.0, 0.0, 100.0, 1.0, true},
};

template<std::size_t N>
constexpr std::size_t countof(const EffectParameterDescriptor (&)[N])
{
    return N;
}

const std::vector<EffectDescriptor> kDescriptors = {
    {LayerEffectType::BackgroundColor, "fxm.builtin.background-color", "background-color", "Background Color", "Generate", "effect-transitions/shaders/background-color/background-color.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedExpand, 1, true, true, true, false, kAppearanceParameters, countof(kAppearanceParameters)},
    {LayerEffectType::Outline, "fxm.builtin.outline", "outline", "Outline", "Generate", "effect-transitions/shaders/outline/outline.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedExpand, 1, true, true, true, false, kAppearanceParameters, countof(kAppearanceParameters)},
    {LayerEffectType::DropShadow, "fxm.builtin.drop-shadow", "drop-shadow", "Drop Shadow", "Stylize", "effect-transitions/shaders/drop-shadow/drop-shadow.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 3, true, true, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::LongShadow, "fxm.builtin.long-shadow", "long-shadow", "Long Shadow", "Stylize", "effect-transitions/shaders/long-shadow/long-shadow.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 2, true, true, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::BrightnessContrast, "fxm.builtin.brightness-contrast", "brightness-contrast", "Brightness & Contrast", "Color Correction", "effect-transitions/shaders/brightness-contrast/brightness-contrast.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kColorParameters, countof(kColorParameters)},
    {LayerEffectType::Saturation, "fxm.builtin.saturation", "saturation", "Saturation", "Color Correction", "effect-transitions/shaders/saturation/saturation.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kColorParameters, countof(kColorParameters)},
    {LayerEffectType::ColorOverlay, "fxm.builtin.color-overlay", "color-overlay", "Color Overlay", "Color Correction", "effect-transitions/shaders/color-overlay/color-overlay.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kAppearanceParameters, countof(kAppearanceParameters)},
    {LayerEffectType::Glow, "fxm.builtin.glow", "glow", "Glow", "Stylize", "effect-transitions/shaders/glow/glow.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 3, true, true, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::InnerGlow, "fxm.builtin.inner-glow", "inner-glow", "Inner Glow", "Stylize", "effect-transitions/shaders/inner-glow/inner-glow.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::InnerShadow, "fxm.builtin.inner-shadow", "inner-shadow", "Inner Shadow", "Stylize", "effect-transitions/shaders/inner-shadow/inner-shadow.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::Blur, "fxm.builtin.blur", "blur", "Blur", "Blur and Sharpen", "effect-transitions/shaders/blur/blur.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 2, true, true, true, false, kAppearanceParameters, countof(kAppearanceParameters)},
    {LayerEffectType::MotionBlur, "fxm.builtin.motion-blur", "motion-blur", "Motion Blur", "Blur and Sharpen", "effect-transitions/shaders/motion-blur/motion-blur.effect", false, 1, LayerEffectSpace::PostTransform, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 1, true, false, false, false, kMotionParameters, countof(kMotionParameters)},
    {LayerEffectType::Bloom, "fxm.builtin.bloom", "bloom", "Bloom", "Stylize", "effect-transitions/shaders/bloom/bloom.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 3, true, true, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::Emboss, "fxm.builtin.emboss", "emboss", "Emboss", "Stylize", "effect-transitions/shaders/emboss/emboss.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kShadowParameters, countof(kShadowParameters)},
    {LayerEffectType::LensFlare, "fxm.builtin.lens-flare", "lens-flare", "Lens Flare", "Generate", "effect-transitions/shaders/lens-flare/lens-flare.effect", true, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 1, true, true, false, false, kProceduralParameters, countof(kProceduralParameters)},
    {LayerEffectType::Vignette, "fxm.builtin.vignette", "vignette", "Vignette", "Stylize", "effect-transitions/shaders/vignette/vignette.effect", true, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kProceduralParameters, countof(kProceduralParameters)},
    {LayerEffectType::Noise, "fxm.builtin.noise", "noise", "Noise", "Noise and Grain", "effect-transitions/shaders/noise/noise.effect", true, 4, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kNoiseParameters, countof(kNoiseParameters)},
    {LayerEffectType::RoughenEdges, "fxm.builtin.roughen-edges", "roughen-edges", "Roughen Edges", "Stylize", "effect-transitions/shaders/roughen-edges/roughen-edges.effect", true, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedReplace, 1, true, true, true, false, kProceduralParameters, countof(kProceduralParameters)},
    {LayerEffectType::FourColorGradient, "fxm.builtin.4-color-gradient", "4-color-gradient", "4-Color Gradient", "Generate", "effect-transitions/shaders/4-color-gradient/4-color-gradient.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 1, true, false, true, false, kGradientParameters, countof(kGradientParameters)},
    {LayerEffectType::Sharpen, "fxm.builtin.sharpen", "sharpen", "Sharpen", "Blur and Sharpen", "effect-transitions/shaders/detail/detail.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kDetailParameters, countof(kDetailParameters)},
    {LayerEffectType::UnsharpMask, "fxm.builtin.unsharp-mask", "unsharp-mask", "Unsharp Mask", "Blur and Sharpen", "effect-transitions/shaders/detail/detail.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kDetailParameters, countof(kDetailParameters)},
    {LayerEffectType::HighPass, "fxm.builtin.high-pass", "high-pass", "High Pass", "Blur and Sharpen", "effect-transitions/shaders/detail/detail.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kDetailParameters, countof(kDetailParameters)},
    {LayerEffectType::Clarity, "fxm.builtin.clarity", "clarity", "Clarity / Local Contrast", "Blur and Sharpen", "effect-transitions/shaders/detail/detail.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kDetailParameters, countof(kDetailParameters)},
    {LayerEffectType::BilateralSharpen, "fxm.builtin.bilateral-sharpen", "bilateral-sharpen", "Bilateral Sharpen", "Blur and Sharpen", "effect-transitions/shaders/detail/detail.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 3, true, false, true, false, kDetailParameters, countof(kDetailParameters)},
    {LayerEffectType::Glare, "fxm.builtin.glare", "glare", "Real Glare", "Light and Optical", "effect-transitions/shaders/glare/glare.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 3, true, true, true, false, kOpticalParameters, countof(kOpticalParameters)},
    {LayerEffectType::Halation, "fxm.builtin.halation", "halation", "Halation", "Light and Optical", "effect-transitions/shaders/halation/halation.effect", false, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 3, true, true, true, false, kOpticalParameters, countof(kOpticalParameters)},
    {LayerEffectType::LensDistortion, "fxm.builtin.lens-distortion", "lens-distortion", "Lens Distortion", "Distortion", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::ChromaticAberration, "fxm.builtin.chromatic-aberration", "chromatic-aberration", "Chromatic Aberration", "Light and Optical", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::DirectionalBlur, "fxm.builtin.directional-blur", "directional-blur", "Directional Blur", "Blur and Sharpen", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 1, true, true, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::ZoomBlur, "fxm.builtin.zoom-blur", "zoom-blur", "Zoom Blur", "Blur and Sharpen", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 1, true, true, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::RadialBlur, "fxm.builtin.radial-blur", "radial-blur", "Radial Blur", "Blur and Sharpen", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedExpand, 1, true, true, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::Ripple, "fxm.builtin.ripple", "ripple", "Ripple", "Distortion", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::WaveWarp, "fxm.builtin.wave-warp", "wave-warp", "Wave Warp", "Distortion", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::Pixelate, "fxm.builtin.pixelate", "pixelate", "Pixelate / Mosaic", "Stylize", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::EdgeDetect, "fxm.builtin.edge-detect", "edge-detect", "Edge Detect", "Stylize", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::Posterize, "fxm.builtin.posterize", "posterize", "Posterize", "Stylize", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::Threshold, "fxm.builtin.threshold", "threshold", "Threshold", "Stylize", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::Scanlines, "fxm.builtin.scanlines", "scanlines", "Scanlines", "Stylize", "effect-transitions/shaders/finishing/finishing.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kFinishingParameters, countof(kFinishingParameters)},
    {LayerEffectType::ChromaKey, "fxm.builtin.chroma-key", "chroma-key", "Chroma Key", "Keying", "effect-transitions/shaders/keying/keying.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedReplace, 1, true, false, true, false, kKeyingParameters, countof(kKeyingParameters)},
    {LayerEffectType::LumaKey, "fxm.builtin.luma-key", "luma-key", "Luma Key", "Keying", "effect-transitions/shaders/keying/keying.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedReplace, 1, true, false, true, false, kKeyingParameters, countof(kKeyingParameters)},
    {LayerEffectType::ColorRange, "fxm.builtin.color-range", "color-range", "Color Range", "Keying", "effect-transitions/shaders/keying/keying.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedReplace, 1, true, false, true, false, kKeyingParameters, countof(kKeyingParameters)},
    {LayerEffectType::SpillSuppression, "fxm.builtin.spill-suppression", "spill-suppression", "Spill Suppression", "Keying", "effect-transitions/shaders/keying/keying.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kKeyingParameters, countof(kKeyingParameters)},
    {LayerEffectType::MatteChoker, "fxm.builtin.matte-choker", "matte-choker", "Matte Choker", "Keying", "effect-transitions/shaders/keying/keying.effect", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedReplace, 1, true, false, true, false, kKeyingParameters, countof(kKeyingParameters)},
    {LayerEffectType::LightWrap, "fxm.builtin.light-wrap", "light-wrap", "Light Wrap", "Light and Optical", "effect-transitions/shaders/source-effects/source-effects.effect", true, 1, LayerEffectSpace::ScreenSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, false, true, kLightWrapParameters, countof(kLightWrapParameters)},
    {LayerEffectType::DisplacementMap, "fxm.builtin.displacement-map", "displacement-map", "Displacement Map", "Distortion", "effect-transitions/shaders/source-effects/source-effects.effect", true, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 1, true, false, false, false, kDisplacementMapParameters, countof(kDisplacementMapParameters)},
    {LayerEffectType::Grain, "fxm.builtin.grain", "grain", "Grain", "Noise and Grain", "effect-transitions/shaders/noise/noise.effect", true, 4, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, false, true, false, kNoiseParameters, countof(kNoiseParameters)},
    {LayerEffectType::FilmDistortion, "fxm.builtin.film-distortion", "film-distortion", "Film Distortion", "Distortion", "effect-transitions/shaders/damage-distortion/damage-distortion.effect", true, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::LinearLight, EffectAlphaContract::PremultipliedPreserve, 1, true, true, true, false, kDamageParameters, countof(kDamageParameters)},
    {LayerEffectType::AnalogDistortion, "fxm.builtin.analog-distortion", "analog-distortion", "Analog Distortion", "Distortion", "effect-transitions/shaders/damage-distortion/damage-distortion.effect", true, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::DisplayReferred, EffectAlphaContract::PremultipliedPreserve, 1, true, true, true, false, kDamageParameters, countof(kDamageParameters)},
    {LayerEffectType::DigitalDistortion, "fxm.builtin.digital-distortion", "digital-distortion", "Digital Distortion", "Distortion", "effect-transitions/shaders/damage-distortion/damage-distortion.effect", true, 2, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Gpu, EffectColorContract::DisplayReferred, EffectAlphaContract::PremultipliedPreserve, 1, true, true, true, false, kDamageParameters, countof(kDamageParameters)},
    {LayerEffectType::TrimPaths, "fxm.builtin.trim-paths", "trim-paths", "Trim Paths", "Generate", "", false, 1, LayerEffectSpace::LayerSpace, EffectExecutionBackend::Cpu, EffectColorContract::PreserveInput, EffectAlphaContract::PremultipliedPreserve, 0, true, true, true, false, kTrimPathsParameters, countof(kTrimPathsParameters)},
};

double evaluate(const AnimatedProperty &property, double fallback, double time)
{
    return property.is_animated() ? property.evaluate(time) : fallback;
}

int channel(const AnimatedProperty &property, std::uint32_t fallback, double time)
{
    return static_cast<int>(std::clamp(
        std::round(evaluate(property, static_cast<double>(fallback), time)),
        0.0, 255.0));
}

std::uint32_t color(const AnimatedProperty &a, const AnimatedProperty &r,
                    const AnimatedProperty &g, const AnimatedProperty &b,
                    std::uint32_t fallback, double time)
{
    return (static_cast<std::uint32_t>(channel(a, (fallback >> 24) & 0xFFu, time)) << 24) |
           (static_cast<std::uint32_t>(channel(r, (fallback >> 16) & 0xFFu, time)) << 16) |
           (static_cast<std::uint32_t>(channel(g, (fallback >> 8) & 0xFFu, time)) << 8) |
           static_cast<std::uint32_t>(channel(b, fallback & 0xFFu, time));
}

int gaussian_downsample(double radius)
{
    const double clamped = std::clamp(radius, 0.0, 4096.0);
    int downsample = 1;
    while (downsample < 64 && clamped > 8.0 * downsample)
        downsample *= 2;
    return downsample;
}

double gaussian_support(double radius)
{
    return radius <= 0.01 ? 0.0 : 8.0 * gaussian_downsample(radius);
}

EffectBoundsExpansion symmetric(double amount)
{
    const double safe = std::max(0.0, amount);
    return {safe, safe, safe, safe};
}

} // namespace

double EffectBoundsExpansion::maximum() const
{
    return std::max({left, top, right, bottom});
}

void EffectBoundsExpansion::accumulate(const EffectBoundsExpansion &other)
{
    left += other.left;
    top += other.top;
    right += other.right;
    bottom += other.bottom;
}

const std::vector<EffectDescriptor> &builtin_effect_descriptors()
{
    return kDescriptors;
}

const EffectDescriptor *effect_descriptor(LayerEffectType type)
{
    const auto index = static_cast<std::size_t>(type);
    if (index < kDescriptors.size() && kDescriptors[index].type == type)
        return &kDescriptors[index];
    const auto found = std::find_if(kDescriptors.begin(), kDescriptors.end(),
        [type](const EffectDescriptor &descriptor) {
            return descriptor.type == type;
        });
    return found == kDescriptors.end() ? nullptr : &*found;
}

const EffectDescriptor *effect_descriptor(const LayerEffect &effect)
{
    return effect_descriptor(effect.type);
}

LayerEffectSpace effect_execution_space(const LayerEffect &effect)
{
    if (effect.affect_layers_behind)
        return LayerEffectSpace::ScreenSpace;
    if (const EffectDescriptor *descriptor = effect_descriptor(effect))
        return descriptor->execution_space;
    return LayerEffectSpace::LayerSpace;
}

EffectDirtyScope effect_dirty_scope(const LayerEffect &effect)
{
    const LayerEffectSpace space = effect_execution_space(effect);
    if (space == LayerEffectSpace::ScreenSpace)
        return EffectDirtyScope::Composition;
    if (effect.type == LayerEffectType::BackgroundColor ||
        effect.type == LayerEffectType::Outline ||
        effect.type == LayerEffectType::TrimPaths)
        return EffectDirtyScope::LayerRaster;
    return EffectDirtyScope::EffectOutput;
}

bool effect_is_time_variant(const LayerEffect &effect)
{
    if (effect.effect_animated)
        return true;
    const AnimatedProperty *properties[] = {
        &effect.enabled_prop, &effect.brightness_prop, &effect.contrast_prop,
        &effect.saturation_prop, &effect.opacity_prop, &effect.size_prop,
        &effect.distance_prop, &effect.angle_prop, &effect.spread_prop,
        &effect.falloff_prop, &effect.amount_prop, &effect.scale_prop,
        &effect.softness_prop, &effect.roundness_prop, &effect.speed_prop,
        &effect.center_x_prop, &effect.center_y_prop, &effect.complexity_prop,
        &effect.evolution_prop, &effect.stroke_width_prop,
        &effect.trim_start_prop, &effect.trim_end_prop,
        &effect.trim_offset_prop,
        &effect.stroke_opacity_prop, &effect.padding_left_prop,
        &effect.padding_right_prop, &effect.padding_top_prop,
        &effect.padding_bottom_prop, &effect.corner_radius_tl_prop,
        &effect.corner_radius_tr_prop, &effect.corner_radius_br_prop,
        &effect.corner_radius_bl_prop, &effect.gradient_start_pos_prop,
        &effect.gradient_end_pos_prop, &effect.gradient_start_opacity_prop,
        &effect.gradient_end_opacity_prop, &effect.gradient_angle_prop,
        &effect.gradient_center_x_prop, &effect.gradient_center_y_prop,
        &effect.gradient_scale_prop, &effect.gradient_focal_x_prop,
        &effect.gradient_focal_y_prop, &effect.gradient_opacity_prop,
        &effect.gradient_start_color_a, &effect.gradient_start_color_r,
        &effect.gradient_start_color_g, &effect.gradient_start_color_b,
        &effect.gradient_end_color_a, &effect.gradient_end_color_r,
        &effect.gradient_end_color_g, &effect.gradient_end_color_b,
        &effect.color_a, &effect.color_r, &effect.color_g, &effect.color_b,
        &effect.stroke_color_a, &effect.stroke_color_r,
        &effect.stroke_color_g, &effect.stroke_color_b,
        &effect.secondary_color_a, &effect.secondary_color_r,
        &effect.secondary_color_g, &effect.secondary_color_b,
    };
    return std::any_of(std::begin(properties), std::end(properties),
        [](const AnimatedProperty *property) {
            return property && property->is_animated();
        });
}

ResolvedLayerEffect make_resolved_layer_effect(LayerEffectType type)
{
    ResolvedLayerEffect resolved;
    resolved.type = type;
    if (const EffectDescriptor *descriptor = effect_descriptor(type))
        resolved.execution_space = descriptor->execution_space;
    return resolved;
}

ResolvedLayerEffect resolve_layer_effect(const LayerEffect &effect, double time)
{
    fxm::perf::ScopedTimer timer(fxm::perf::Counter::EffectParameterResolutionNanoseconds);
    fxm::perf::add(fxm::perf::Counter::EffectParameterResolutions);

    ResolvedLayerEffect resolved = make_resolved_layer_effect(effect.type);
    resolved.source = &effect;
    resolved.enabled = effect.enabled &&
        (!effect.enabled_prop.is_animated() || effect.enabled_prop.evaluate(time) >= 0.5);
    resolved.execution_space = effect_execution_space(effect);
    resolved.brightness = static_cast<float>(std::clamp(
        evaluate(effect.brightness_prop, effect.brightness, time), -1.0, 1.0));
    resolved.contrast = static_cast<float>(std::clamp(
        evaluate(effect.contrast_prop, effect.contrast, time), 0.0, 4.0));
    resolved.saturation = static_cast<float>(std::clamp(
        evaluate(effect.saturation_prop, effect.saturation, time), 0.0, 4.0));
    resolved.effect_color = color(effect.color_a, effect.color_r, effect.color_g,
                                  effect.color_b, effect.effect_color, time);
    resolved.effect_secondary_color = color(
        effect.secondary_color_a, effect.secondary_color_r,
        effect.secondary_color_g, effect.secondary_color_b,
        effect.effect_secondary_color, time);
    resolved.effect_stroke_color = color(
        effect.stroke_color_a, effect.stroke_color_r, effect.stroke_color_g,
        effect.stroke_color_b, effect.effect_stroke_color, time);
    resolved.effect_opacity = static_cast<float>(std::clamp(
        evaluate(effect.opacity_prop, effect.effect_opacity, time), 0.0, 1.0));
    resolved.effect_size = static_cast<float>(std::max(
        0.0, evaluate(effect.size_prop, effect.effect_size, time)));
    const double evaluated_distance = evaluate(
        effect.distance_prop, effect.effect_distance, time);
    resolved.effect_distance = static_cast<float>(
        effect.type == LayerEffectType::DisplacementMap
            ? std::clamp(evaluated_distance, -4096.0, 4096.0)
            : std::max(0.0, evaluated_distance));
    resolved.effect_angle = static_cast<float>(
        evaluate(effect.angle_prop, effect.effect_angle, time));
    resolved.effect_spread = static_cast<float>(std::max(
        0.0, evaluate(effect.spread_prop, effect.effect_spread, time)));
    resolved.effect_falloff = static_cast<float>(std::max(
        0.0, evaluate(effect.falloff_prop, effect.effect_falloff, time)));
    resolved.effect_blur_type = effect.effect_blur_type;
    resolved.effect_samples = effect.effect_samples;
    resolved.effect_centered = effect.effect_centered;
    resolved.effect_outside_hard_alpha = effect.effect_outside_hard_alpha;
    resolved.effect_outside_hard_alpha_invert = effect.effect_outside_hard_alpha_invert;
    resolved.affect_layers_behind = effect.affect_layers_behind;
    resolved.affect_layers_behind_invert = effect.affect_layers_behind_invert;
    resolved.blend_mode = effect.blend_mode;
    resolved.effect_source_mode = effect.effect_source_mode;
    resolved.effect_x_channel = effect.effect_x_channel;
    resolved.effect_y_channel = effect.effect_y_channel;
    resolved.effect_wrap_mode = effect.effect_wrap_mode;
    resolved.effect_mapping_space = effect.effect_mapping_space;
    resolved.effect_alpha_aware = effect.effect_alpha_aware;
    resolved.effect_profile = effect.effect_profile;
    resolved.effect_animated = effect.effect_animated;
    resolved.effect_monochrome = effect.effect_monochrome;
    resolved.effect_invert = effect.effect_invert;
    resolved.effect_seed = effect.effect_seed;
    const double evaluated_amount = evaluate(
        effect.amount_prop, effect.effect_amount, time);
    double clamped_amount = std::max(0.0, evaluated_amount);
    if (effect.type == LayerEffectType::Ripple ||
        effect.type == LayerEffectType::WaveWarp ||
        effect.type == LayerEffectType::DisplacementMap) {
        clamped_amount = std::clamp(evaluated_amount, -4096.0, 4096.0);
    } else if (effect.type == LayerEffectType::MatteChoker) {
        clamped_amount = std::clamp(evaluated_amount, -1.0, 1.0);
    } else if (effect.type == LayerEffectType::ChromaKey ||
               effect.type == LayerEffectType::LumaKey ||
               effect.type == LayerEffectType::ColorRange ||
               effect.type == LayerEffectType::SpillSuppression) {
        clamped_amount = std::clamp(evaluated_amount, 0.0, 1.0);
    }
    resolved.effect_amount = static_cast<float>(clamped_amount);
    resolved.effect_scale = static_cast<float>(std::max(
        0.001, evaluate(effect.scale_prop, effect.effect_scale, time)));
    resolved.effect_softness = static_cast<float>(std::clamp(
        evaluate(effect.softness_prop, effect.effect_softness, time), 0.0, 1.0));
    resolved.effect_roundness = static_cast<float>(std::clamp(
        evaluate(effect.roundness_prop, effect.effect_roundness, time), -3.0, 3.0));
    resolved.effect_speed = static_cast<float>(
        evaluate(effect.speed_prop, effect.effect_speed, time));
    resolved.effect_center_x = static_cast<float>(
        evaluate(effect.center_x_prop, effect.effect_center_x, time));
    resolved.effect_center_y = static_cast<float>(
        evaluate(effect.center_y_prop, effect.effect_center_y, time));
    resolved.effect_complexity = static_cast<float>(std::clamp(
        evaluate(effect.complexity_prop, effect.effect_complexity, time), 1.0, 12.0));
    resolved.effect_evolution = static_cast<float>(
        evaluate(effect.evolution_prop, effect.effect_evolution, time));
    resolved.effect_schema_version = effect.extension_schema_version;
    resolved.effect_affect_alpha = effect.effect_affect_alpha;
    resolved.effect_clamp_output = effect.effect_clamp_output;
    resolved.effect_temporal_stability = effect.effect_temporal_stability;
    resolved.effect_fill_type = effect.effect_fill_type;
    resolved.effect_join_style = effect.effect_join_style;
    resolved.effect_on_front = effect.effect_on_front;
    resolved.effect_antialias = effect.effect_antialias;
    resolved.effect_stroke_width = static_cast<float>(std::max(
        0.0, evaluate(effect.stroke_width_prop, effect.effect_stroke_width, time)));
    resolved.effect_stroke_opacity = static_cast<float>(std::clamp(
        evaluate(effect.stroke_opacity_prop, effect.effect_stroke_opacity, time),
        0.0, 1.0));
    resolved.effect_trim_start = static_cast<float>(std::clamp(
        evaluate(effect.trim_start_prop, effect.effect_trim_start, time), 0.0, 100.0));
    resolved.effect_trim_end = static_cast<float>(std::clamp(
        evaluate(effect.trim_end_prop, effect.effect_trim_end, time), 0.0, 100.0));
    resolved.effect_trim_offset = static_cast<float>(
        evaluate(effect.trim_offset_prop, effect.effect_trim_offset, time));
    resolved.effect_trim_multiple_shapes = std::clamp(effect.effect_trim_multiple_shapes, 0, 1);
    resolved.effect_padding_left = static_cast<float>(
        evaluate(effect.padding_left_prop, effect.effect_padding_left, time));
    resolved.effect_padding_right = static_cast<float>(
        evaluate(effect.padding_right_prop, effect.effect_padding_right, time));
    resolved.effect_padding_top = static_cast<float>(
        evaluate(effect.padding_top_prop, effect.effect_padding_top, time));
    resolved.effect_padding_bottom = static_cast<float>(
        evaluate(effect.padding_bottom_prop, effect.effect_padding_bottom, time));
    resolved.effect_corner_radius_tl = static_cast<float>(std::max(
        0.0, evaluate(effect.corner_radius_tl_prop, effect.effect_corner_radius_tl, time)));
    resolved.effect_corner_radius_tr = static_cast<float>(std::max(
        0.0, evaluate(effect.corner_radius_tr_prop, effect.effect_corner_radius_tr, time)));
    resolved.effect_corner_radius_br = static_cast<float>(std::max(
        0.0, evaluate(effect.corner_radius_br_prop, effect.effect_corner_radius_br, time)));
    resolved.effect_corner_radius_bl = static_cast<float>(std::max(
        0.0, evaluate(effect.corner_radius_bl_prop, effect.effect_corner_radius_bl, time)));
    resolved.effect_corner_type = effect.effect_corner_type;
    resolved.effect_gradient_type = effect.effect_gradient_type;
    resolved.effect_gradient_spread = effect.effect_gradient_spread;
    resolved.effect_gradient_start_color = color(
        effect.gradient_start_color_a, effect.gradient_start_color_r,
        effect.gradient_start_color_g, effect.gradient_start_color_b,
        effect.effect_gradient_start_color, time);
    resolved.effect_gradient_end_color = color(
        effect.gradient_end_color_a, effect.gradient_end_color_r,
        effect.gradient_end_color_g, effect.gradient_end_color_b,
        effect.effect_gradient_end_color, time);
    resolved.effect_gradient_start_pos = static_cast<float>(std::clamp(
        evaluate(effect.gradient_start_pos_prop, effect.effect_gradient_start_pos, time),
        0.0, 1.0));
    resolved.effect_gradient_end_pos = static_cast<float>(std::clamp(
        evaluate(effect.gradient_end_pos_prop, effect.effect_gradient_end_pos, time),
        0.0, 1.0));
    resolved.effect_gradient_start_opacity = static_cast<float>(std::clamp(
        evaluate(effect.gradient_start_opacity_prop,
                 effect.effect_gradient_start_opacity, time), 0.0, 1.0));
    resolved.effect_gradient_end_opacity = static_cast<float>(std::clamp(
        evaluate(effect.gradient_end_opacity_prop,
                 effect.effect_gradient_end_opacity, time), 0.0, 1.0));
    resolved.effect_gradient_opacity = static_cast<float>(std::clamp(
        evaluate(effect.gradient_opacity_prop, effect.effect_gradient_opacity, time),
        0.0, 1.0));
    resolved.effect_gradient_angle = static_cast<float>(
        evaluate(effect.gradient_angle_prop, effect.effect_gradient_angle, time));
    resolved.effect_gradient_center_x = static_cast<float>(
        evaluate(effect.gradient_center_x_prop, effect.effect_gradient_center_x, time));
    resolved.effect_gradient_center_y = static_cast<float>(
        evaluate(effect.gradient_center_y_prop, effect.effect_gradient_center_y, time));
    resolved.effect_gradient_scale = static_cast<float>(std::max(
        0.001, evaluate(effect.gradient_scale_prop, effect.effect_gradient_scale, time)));
    resolved.effect_gradient_focal_x = static_cast<float>(
        evaluate(effect.gradient_focal_x_prop, effect.effect_gradient_focal_x, time));
    resolved.effect_gradient_focal_y = static_cast<float>(
        evaluate(effect.gradient_focal_y_prop, effect.effect_gradient_focal_y, time));
    return resolved;
}

EffectBoundsExpansion effect_bounds_expansion(const ResolvedLayerEffect &effect,
                                              double layer_width,
                                              double layer_height)
{
    fxm::perf::add(fxm::perf::Counter::EffectBoundsEvaluations);
    switch (effect.type) {
    case LayerEffectType::BackgroundColor:
        return {
            std::max(0.0f, effect.effect_padding_left) + effect.effect_stroke_width + 4.0,
            std::max(0.0f, effect.effect_padding_top) + effect.effect_stroke_width + 4.0,
            std::max(0.0f, effect.effect_padding_right) + effect.effect_stroke_width + 4.0,
            std::max(0.0f, effect.effect_padding_bottom) + effect.effect_stroke_width + 4.0,
        };
    case LayerEffectType::DropShadow: {
        const double radius = effect.effect_size + effect.effect_spread;
        const double halo = gaussian_support(radius) + radius + 12.0;
        const double radians = effect.effect_angle * 3.14159265358979323846 / 180.0;
        const double dx = std::cos(radians) * effect.effect_distance;
        const double dy = std::sin(radians) * effect.effect_distance;
        return {halo + std::max(0.0, -dx), halo + std::max(0.0, -dy),
                halo + std::max(0.0, dx), halo + std::max(0.0, dy)};
    }
    case LayerEffectType::LongShadow: {
        const double blur = effect.effect_blur_type == static_cast<int>(LongShadowBlurType::None)
            ? 0.0 : gaussian_support(effect.effect_size) + effect.effect_size;
        const double halo = blur + 12.0;
        const double radians = effect.effect_angle * 3.14159265358979323846 / 180.0;
        const double dx = std::cos(radians) * effect.effect_distance;
        const double dy = std::sin(radians) * effect.effect_distance;
        return {halo + std::max(0.0, -dx), halo + std::max(0.0, -dy),
                halo + std::max(0.0, dx), halo + std::max(0.0, dy)};
    }
    case LayerEffectType::Outline:
        return symmetric(effect.effect_size + effect.effect_distance + 4.0);
    case LayerEffectType::Glow:
    case LayerEffectType::Bloom:
    case LayerEffectType::Halation: {
        const double radius = effect.effect_size + effect.effect_spread;
        return symmetric(gaussian_support(radius) + radius + 12.0);
    }
    case LayerEffectType::Glare: {
        const double blur = gaussian_support(effect.effect_size) + effect.effect_size;
        const double streak = std::max(0.0f, effect.effect_distance);
        return symmetric(blur + streak + 12.0);
    }
    case LayerEffectType::Blur:
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::HighPass:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen:
        return symmetric(gaussian_support(effect.effect_size) + 4.0);
    case LayerEffectType::DirectionalBlur:
        return symmetric(std::max(0.0f, effect.effect_size) + 4.0);
    case LayerEffectType::ZoomBlur:
    case LayerEffectType::RadialBlur:
        return symmetric(std::max(layer_width, layer_height) *
                         std::min(1.0f, std::max(0.0f, effect.effect_size) / 1000.0f) + 4.0);
    case LayerEffectType::ChromaticAberration:
        return symmetric(std::max(0.0f, effect.effect_amount) * 2.0 + 4.0);
    case LayerEffectType::Ripple:
    case LayerEffectType::WaveWarp:
        return symmetric(std::abs(effect.effect_amount) * 8.0 + 4.0);
    case LayerEffectType::LensFlare:
        return symmetric(std::max(layer_width, layer_height) *
                         effect.effect_size * effect.effect_scale * 11.0 + 12.0);
    case LayerEffectType::FilmDistortion:
    case LayerEffectType::AnalogDistortion:
    case LayerEffectType::DigitalDistortion:
        return symmetric(effect.effect_amount * 32.0 + 4.0);
    case LayerEffectType::TrimPaths:
        return symmetric(4.0);
    case LayerEffectType::RoughenEdges:
        return symmetric(effect.effect_amount * 64.0 + 4.0);
    default:
        return {};
    }
}

LayerEffectSpace layer_effect_execution_space(const LayerEffect &effect)
{
    return effect_execution_space(effect);
}

bool layer_effect_stack_has_space(const std::vector<LayerEffect> &effects,
                                  LayerEffectSpace space)
{
    return std::any_of(effects.begin(), effects.end(),
        [space](const LayerEffect &effect) {
            return effect.enabled && effect_execution_space(effect) == space;
        });
}
