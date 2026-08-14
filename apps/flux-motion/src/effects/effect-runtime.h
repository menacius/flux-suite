#pragma once

#include "layer-effects.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct Title;

enum class EffectExecutionBackend : std::uint8_t {
    Gpu = 0,
    Cpu = 1,
    Hybrid = 2,
};

enum class EffectColorContract : std::uint8_t {
    PreserveInput = 0,
    LinearLight = 1,
    DisplayReferred = 2,
};

enum class EffectAlphaContract : std::uint8_t {
    PremultipliedPreserve = 0,
    PremultipliedExpand = 1,
    PremultipliedReplace = 2,
};

enum class EffectParameterKind : std::uint8_t {
    Boolean = 0,
    Integer = 1,
    Scalar = 2,
    Angle = 3,
    Color = 4,
    Enumeration = 5,
    Point = 6,
};

enum class EffectDirtyScope : std::uint8_t {
    None = 0,
    EffectOutput = 1,
    LayerRaster = 2,
    Composition = 3,
};

struct EffectParameterDescriptor {
    const char *path = nullptr;
    EffectParameterKind kind = EffectParameterKind::Scalar;
    double default_value = 0.0;
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.01;
    bool keyframeable = true;
};

/* Canonical effect metadata shared by the editor, extension catalog, shader
 * registry, cache policy, 2D compositor and planar-3D compositor. Built-in
 * effect IDs and schema versions are deliberately independent from the title
 * document schema so an effect can migrate without rewriting unrelated title
 * data. */
struct EffectDescriptor {
    LayerEffectType type = LayerEffectType::BackgroundColor;
    const char *stable_id = nullptr;
    const char *legacy_id = nullptr;
    const char *display_name = nullptr;
    const char *category = nullptr;
    const char *relative_path = nullptr;
    bool has_embedded_fallback = false;

    std::uint32_t schema_version = 1;
    LayerEffectSpace execution_space = LayerEffectSpace::LayerSpace;
    EffectExecutionBackend backend = EffectExecutionBackend::Gpu;
    EffectColorContract color_contract = EffectColorContract::PreserveInput;
    EffectAlphaContract alpha_contract = EffectAlphaContract::PremultipliedPreserve;
    std::uint8_t minimum_render_passes = 1;
    bool supports_hdr = true;
    bool expands_bounds = false;
    bool cacheable_when_static = true;
    bool requires_background = false;
    const EffectParameterDescriptor *parameters = nullptr;
    std::size_t parameter_count = 0;
};

struct EffectBoundsExpansion {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    double maximum() const;
    void accumulate(const EffectBoundsExpansion &other);
};

/* Allocation-free evaluated state. The previous hot path copied LayerEffect,
 * including every std::string and keyframe vector, for every effect on every
 * rendered frame. This POD-like snapshot contains only render-time scalar
 * state and retains a non-owning pointer for optional extension JSON. */
struct ResolvedLayerEffect {
    const LayerEffect *source = nullptr;
    LayerEffectType type = LayerEffectType::BackgroundColor;
    LayerEffectSpace execution_space = LayerEffectSpace::LayerSpace;
    bool enabled = true;

    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    std::uint32_t effect_color = 0xFFFFFFFF;
    std::uint32_t effect_secondary_color = 0xFF4EA3FF;
    std::uint32_t effect_stroke_color = 0x00000000;
    float effect_opacity = 1.0f;
    float effect_size = 16.0f;
    float effect_distance = 8.0f;
    float effect_angle = 135.0f;
    float effect_spread = 0.0f;
    float effect_falloff = 1.0f;
    int effect_blur_type = static_cast<int>(ShadowBlurType::StackFast);
    int effect_samples = 8;
    bool effect_centered = true;
    bool effect_outside_hard_alpha = false;
    bool effect_outside_hard_alpha_invert = false;
    bool affect_layers_behind = false;
    bool affect_layers_behind_invert = false;
    EffectBlendMode blend_mode = EffectBlendMode::Normal;
    int effect_source_mode = 0;
    int effect_x_channel = 0;
    int effect_y_channel = 0;
    int effect_wrap_mode = 0;
    int effect_mapping_space = 0;
    bool effect_alpha_aware = true;

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
    std::uint32_t effect_schema_version = 1;
    bool effect_affect_alpha = false;
    bool effect_clamp_output = true;
    bool effect_temporal_stability = true;

    int effect_fill_type = 0;
    int effect_join_style = 1;
    bool effect_on_front = true;
    bool effect_antialias = true;
    float effect_stroke_width = 0.0f;
    float effect_stroke_opacity = 1.0f;
    float effect_trim_start = 0.0f;
    float effect_trim_end = 100.0f;
    float effect_trim_offset = 0.0f;
    float effect_stroke_offset = 0.0f;
    int effect_trim_multiple_shapes = 0;
    float effect_padding_left = 0.0f;
    float effect_padding_right = 0.0f;
    float effect_padding_top = 0.0f;
    float effect_padding_bottom = 0.0f;
    float effect_corner_radius_tl = 0.0f;
    float effect_corner_radius_tr = 0.0f;
    float effect_corner_radius_br = 0.0f;
    float effect_corner_radius_bl = 0.0f;
    int effect_corner_type = 0;
    int effect_gradient_type = 0;
    int effect_gradient_spread = 0;
    std::uint32_t effect_gradient_start_color = 0xFF4B6EA8;
    std::uint32_t effect_gradient_end_color = 0xFF1B1B1B;
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
};

const std::vector<EffectDescriptor> &builtin_effect_descriptors();
const EffectDescriptor *effect_descriptor(LayerEffectType type);
const EffectDescriptor *effect_descriptor(const LayerEffect &effect);
bool title_requires_destination_compositing(const Title &title);
LayerEffectSpace effect_execution_space(const LayerEffect &effect);
EffectDirtyScope effect_dirty_scope(const LayerEffect &effect);
bool effect_is_time_variant(const LayerEffect &effect);
ResolvedLayerEffect resolve_layer_effect(const LayerEffect &effect, double time);
ResolvedLayerEffect make_resolved_layer_effect(LayerEffectType type);
EffectBoundsExpansion effect_bounds_expansion(const ResolvedLayerEffect &effect,
                                              double layer_width,
                                              double layer_height);
