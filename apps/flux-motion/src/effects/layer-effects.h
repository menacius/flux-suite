#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "../core/serialization-passthrough.h"
#include "animation.h"

/* ══════════════════════════════════════════════════════════════════
 *  Stackable layer effects
 * ══════════════════════════════════════════════════════════════════ */
enum class ShadowBlurType {
    Box = 0,
    Gaussian = 1,
    StackFast = 2,
    AlphaMask = 3,
    Triangular = 4,
    DualKawase = 5,
};

enum class LongShadowBlurType {
    None = 0,
    Box = 1,
    Gaussian = 2,
    StackFast = 3,
};

enum class LayerEffectType {
    BackgroundColor = 0,
    Outline = 1,
    DropShadow = 2,
    LongShadow = 3,
    BrightnessContrast = 4,
    Saturation = 5,
    ColorOverlay = 6,
    Glow = 7,
    InnerGlow = 8,
    InnerShadow = 9,
    Blur = 10,
    MotionBlur = 11,
    Bloom = 12,
    Emboss = 13,
    LensFlare = 14,
    Vignette = 15,
    Noise = 16,
    RoughenEdges = 17,
    FourColorGradient = 18,
    Sharpen = 19,
    UnsharpMask = 20,
    HighPass = 21,
    Clarity = 22,
    BilateralSharpen = 23,
    Glare = 24,
    Halation = 25,
    LensDistortion = 26,
    ChromaticAberration = 27,
    DirectionalBlur = 28,
    ZoomBlur = 29,
    RadialBlur = 30,
    Ripple = 31,
    WaveWarp = 32,
    Pixelate = 33,
    EdgeDetect = 34,
    Posterize = 35,
    Threshold = 36,
    Scanlines = 37,
    ChromaKey = 38,
    LumaKey = 39,
    ColorRange = 40,
    SpillSuppression = 41,
    MatteChoker = 42,
    LightWrap = 43,
    DisplacementMap = 44,
    Grain = 45,
    FilmDistortion = 46,
    AnalogDistortion = 47,
    DigitalDistortion = 48,
    TrimPaths = 49,
};

enum class EffectBlendMode {
    Normal = 0,
    Multiply = 1,
    Additive = 2,
    Screen = 3,
    Overlay = 4,
    Color = 5,
};

/* Stable execution-space contract used by both the compatibility compositor
 * and the planar-3D depth compositor. This is deliberately derived from the
 * effect type/flags rather than serialized as another user setting, so older
 * projects keep identical results.
 *
 * LayerSpace    : evaluated on the padded transform-neutral layer raster, then
 *                 projected into 2D/3D. Shadows, glow, blur and outline belong
 *                 here and therefore rotate/scale with the plane.
 * PostTransform : evaluated as a final per-layer temporal pass. Motion Blur
 *                 samples the complete effected/transitioned projection because
 *                 camera/parent motion and source geometry can both change.
 * ScreenSpace   : reads or modifies the already composited background.
 *                 Affect-layers-behind effects belong here.
 */
enum class LayerEffectSpace {
    LayerSpace = 0,
    PostTransform = 1,
    ScreenSpace = 2,
};

struct LayerEffect {
    OpaqueSerializationPassthrough serialization_passthrough_json;

    /* Stable extension identity. Empty means use the built-in ID mapped from type.
     * Unknown IDs and their parameter payload survive project round-trips.
     * Development Version 244 audits the complete effect/plugin serialization
     * envelope: effect_id/extension_id remain stable, schema versions are
     * preserved independently from runtime implementation versions, and missing
     * external plugins reopen as inert placeholders instead of deleting opaque
     * parameter/binary state. */
    std::string extension_id;
    std::string extension_parameters_json = "{}";
    uint32_t extension_schema_version = 1;
    uint32_t extension_loaded_schema_version = 1;
    uint32_t extension_runtime_schema_version = 1;
    bool extension_explicit_migration = false;
    bool missing_plugin_placeholder = false;
    std::string extension_provider_id;
    std::string extension_provider_version;
    std::string extension_plugin_binary_id;
    std::string extension_binary_state_json = "{}";
    std::string extension_binary_state_base64;
    std::string effect_preset_id;
    uint32_t effect_preset_schema_version = 0;
    /* Host-owned animation tracks for extension parameters. JSON object keyed by
     * parameter path (for example "intensity" or "elements.0.opacity"). */
    std::string extension_keyframes_json = "{}";

    LayerEffectType type = LayerEffectType::BackgroundColor;
    bool enabled = true;

    /* OBS color-correction compatible values for stackable layer color effects. */
    float brightness = 0.0f;     /* -1.0 .. 1.0 additive RGB offset */
    float contrast = 1.0f;       /* 0.0 .. 4.0 multiplier around 0.5 */
    float saturation = 1.0f;     /* 0.0 .. 4.0 luma/chroma mix */
    uint32_t tint_color = 0xFFFFFFFF; /* Kept for backward-compatible Color Overlay JSON. */
    float tint_amount = 1.0f;    /* 0.0 .. 1.0 color overlay amount */
    uint32_t effect_color = 0xFFFFFFFF;
    float effect_opacity = 1.0f;
    float effect_size = 16.0f;
    float effect_distance = 8.0f;
    float effect_angle = 135.0f;
    float effect_spread = 0.0f;
    float effect_falloff = 1.0f;
    int effect_blur_type = (int)ShadowBlurType::StackFast;
    int effect_samples = 8;
    bool effect_centered = true;
    bool effect_outside_hard_alpha = false;
    bool effect_outside_hard_alpha_invert = false;
    bool affect_layers_behind = false;
    bool affect_layers_behind_invert = false;
    EffectBlendMode blend_mode = EffectBlendMode::Normal;

    /* Cross-layer effect source contract (Development Version 226).
     * Source IDs stay independent from visibility: hidden layers can remain
     * render dependencies without being composited into the final frame. */
    std::string effect_source_layer_id;
    int effect_source_mode = 0;       /* 0=composition, 1=layer */
    int effect_x_channel = 0;         /* 0=luma, 1=red, 2=green, 3=blue, 4=alpha */
    int effect_y_channel = 0;
    int effect_wrap_mode = 0;         /* 0=clamp, 1=repeat, 2=mirror, 3=transparent */
    int effect_mapping_space = 0;     /* 0=source-space, 1=composition-space */
    bool effect_alpha_aware = true;

    /* Procedural effect controls. These fields are shared by Lens Flare,
     * Vignette, Grain, Noise, damage/distortion effects and Roughen Edges;
     * unused values are ignored by each effect's GPU shader. */
    int effect_profile = 0;
    bool effect_animated = false;
    bool effect_monochrome = true;
    bool effect_invert = false;
    int effect_seed = 1;
    float effect_amount = 1.0f;
    float effect_scale = 1.0f;
    float effect_softness = 0.25f;
    float effect_roundness = 0.0f;
    float effect_speed = 1.0f;
    float effect_center_x = 0.5f;
    float effect_center_y = 0.5f;
    float effect_complexity = 4.0f;
    float effect_evolution = 0.0f;
    /* Current procedural controls. Generic fields remain the keyframeable
     * carriers: spread=lacunarity, falloff=gain, roundness=log2 aspect,
     * center=offset and brightness/contrast=tonal shaping. */
    bool effect_affect_alpha = false;
    bool effect_clamp_output = true;
    bool effect_temporal_stability = true;
    uint32_t effect_secondary_color = 0xFF4EA3FF;

    /* Effect-owned style data. Legacy layer fields may still be read while
     * loading old projects, but active editor/render state belongs here.
     */
    int effect_fill_type = 0; /* background: 0=solid, 1=gradient; outline: 0=none, 1=color, 2=gradient */
    int effect_join_style = 1;
    bool effect_on_front = true;
    bool effect_antialias = true;
    bool effect_owned_style_loaded = false;
    uint32_t effect_stroke_color = 0x00000000;
    float effect_stroke_width = 0.0f;
    float effect_stroke_opacity = 1.0f;
    float effect_trim_start = 0.0f;
    float effect_trim_end = 100.0f;
    float effect_trim_offset = 0.0f;
    float effect_stroke_offset = 0.0f;
    int effect_trim_multiple_shapes = 0; /* 0=simultaneously, 1=individually */
    float effect_padding_left = 0.0f;
    float effect_padding_right = 0.0f;
    float effect_padding_top = 0.0f;
    float effect_padding_bottom = 0.0f;
    float effect_corner_radius_tl = 0.0f;
    float effect_corner_radius_tr = 0.0f;
    float effect_corner_radius_br = 0.0f;
    float effect_corner_radius_bl = 0.0f;
    int effect_corner_type = 0;
    int effect_gradient_type = 0; /* 0=linear, 1=radial, 2=conical */
    int effect_gradient_spread = 0; /* 0=no/pad, 1=reflect, 2=repeat */
    uint32_t effect_gradient_start_color = 0xFF4B6EA8;
    uint32_t effect_gradient_end_color = 0xFF1B1B1B;
    float effect_gradient_start_pos = 0.0f;
    float effect_gradient_end_pos = 1.0f;
    float effect_gradient_start_opacity = 1.0f;
    float effect_gradient_end_opacity = 1.0f;
    float effect_gradient_opacity = 1.0f;
    float effect_gradient_angle = 0.0f;
    float effect_gradient_center_x = 0.5f;
    float effect_gradient_center_y = 0.5f;
    float effect_gradient_scale = 1.0f;
    float effect_gradient_focal_x = 0.5f;
    float effect_gradient_focal_y = 0.5f;

    AnimatedProperty enabled_prop { "effect_enabled", 1.0 };
    AnimatedProperty brightness_prop { "effect_brightness", 0.0 };
    AnimatedProperty contrast_prop { "effect_contrast", 1.0 };
    AnimatedProperty saturation_prop { "effect_saturation", 1.0 };
    AnimatedProperty opacity_prop { "effect_opacity", 1.0 };
    AnimatedProperty size_prop { "effect_size", 16.0 };
    AnimatedProperty distance_prop { "effect_distance", 8.0 };
    AnimatedProperty angle_prop { "effect_angle", 135.0 };
    AnimatedProperty spread_prop { "effect_spread", 0.0 };
    AnimatedProperty falloff_prop { "effect_falloff", 1.0 };
    AnimatedProperty amount_prop { "effect_amount", 1.0 };
    AnimatedProperty scale_prop { "effect_scale", 1.0 };
    AnimatedProperty softness_prop { "effect_softness", 0.25 };
    AnimatedProperty roundness_prop { "effect_roundness", 0.0 };
    AnimatedProperty speed_prop { "effect_speed", 1.0 };
    AnimatedProperty center_x_prop { "effect_center_x", 0.5 };
    AnimatedProperty center_y_prop { "effect_center_y", 0.5 };
    AnimatedProperty complexity_prop { "effect_complexity", 4.0 };
    AnimatedProperty evolution_prop { "effect_evolution", 0.0 };
    AnimatedProperty stroke_width_prop { "effect_stroke_width", 0.0 };
    AnimatedProperty stroke_opacity_prop { "effect_stroke_opacity", 1.0 };
    AnimatedProperty trim_start_prop { "effect_trim_start", 0.0 };
    AnimatedProperty trim_end_prop { "effect_trim_end", 100.0 };
    AnimatedProperty trim_offset_prop { "effect_trim_offset", 0.0 };
    AnimatedProperty stroke_offset_prop { "effect_stroke_offset", 0.0 };
    AnimatedProperty padding_left_prop { "effect_padding_left", 0.0 };
    AnimatedProperty padding_right_prop { "effect_padding_right", 0.0 };
    AnimatedProperty padding_top_prop { "effect_padding_top", 0.0 };
    AnimatedProperty padding_bottom_prop { "effect_padding_bottom", 0.0 };
    AnimatedProperty corner_radius_tl_prop { "effect_corner_radius_tl", 0.0 };
    AnimatedProperty corner_radius_tr_prop { "effect_corner_radius_tr", 0.0 };
    AnimatedProperty corner_radius_br_prop { "effect_corner_radius_br", 0.0 };
    AnimatedProperty corner_radius_bl_prop { "effect_corner_radius_bl", 0.0 };
    AnimatedProperty gradient_start_pos_prop { "effect_gradient_start_pos", 0.0 };
    AnimatedProperty gradient_end_pos_prop { "effect_gradient_end_pos", 1.0 };
    AnimatedProperty gradient_start_opacity_prop { "effect_gradient_start_opacity", 1.0 };
    AnimatedProperty gradient_end_opacity_prop { "effect_gradient_end_opacity", 1.0 };
    AnimatedProperty gradient_angle_prop { "effect_gradient_angle", 0.0 };
    AnimatedProperty gradient_center_x_prop { "effect_gradient_center_x", 0.5 };
    AnimatedProperty gradient_center_y_prop { "effect_gradient_center_y", 0.5 };
    AnimatedProperty gradient_scale_prop { "effect_gradient_scale", 1.0 };
    AnimatedProperty gradient_focal_x_prop { "effect_gradient_focal_x", 0.5 };
    AnimatedProperty gradient_focal_y_prop { "effect_gradient_focal_y", 0.5 };
    AnimatedProperty gradient_opacity_prop { "effect_gradient_opacity", 1.0 };
    AnimatedProperty gradient_start_color_a { "effect_gradient_start_color_a", 255.0 };
    AnimatedProperty gradient_start_color_r { "effect_gradient_start_color_r", 75.0 };
    AnimatedProperty gradient_start_color_g { "effect_gradient_start_color_g", 110.0 };
    AnimatedProperty gradient_start_color_b { "effect_gradient_start_color_b", 168.0 };
    AnimatedProperty gradient_end_color_a { "effect_gradient_end_color_a", 255.0 };
    AnimatedProperty gradient_end_color_r { "effect_gradient_end_color_r", 27.0 };
    AnimatedProperty gradient_end_color_g { "effect_gradient_end_color_g", 27.0 };
    AnimatedProperty gradient_end_color_b { "effect_gradient_end_color_b", 27.0 };
    AnimatedProperty color_a { "effect_color_a", 255.0 };
    AnimatedProperty color_r { "effect_color_r", 255.0 };
    AnimatedProperty color_g { "effect_color_g", 255.0 };
    AnimatedProperty color_b { "effect_color_b", 255.0 };
    AnimatedProperty stroke_color_a { "effect_stroke_color_a", 0.0 };
    AnimatedProperty stroke_color_r { "effect_stroke_color_r", 0.0 };
    AnimatedProperty stroke_color_g { "effect_stroke_color_g", 0.0 };
    AnimatedProperty stroke_color_b { "effect_stroke_color_b", 0.0 };
    AnimatedProperty secondary_color_a { "effect_secondary_color_a", 255.0 };
    AnimatedProperty secondary_color_r { "effect_secondary_color_r", 78.0 };
    AnimatedProperty secondary_color_g { "effect_secondary_color_g", 163.0 };
    AnimatedProperty secondary_color_b { "effect_secondary_color_b", 255.0 };
};

/* Compatibility entry points backed by the canonical effect runtime. */
LayerEffectSpace layer_effect_execution_space(const LayerEffect &effect);
bool layer_effect_stack_has_space(const std::vector<LayerEffect> &effects,
                                  LayerEffectSpace space);

