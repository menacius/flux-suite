#pragma once

#include "animation.h"
#include "../core/serialization-passthrough.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

enum class LayerTransitionKind {
    General = 0,
    Text = 1,
};

enum class LayerTransitionEdge {
    In = 0,
    Out = 1,
};

/* Persisted as an integer in title files. Keep existing values stable. */
enum class LayerTransitionType {
    Dissolve = 0,
    Opacity = 1,
    OpacityBlur = 2,
    Scale = 3,
    Wipe = 4,
    Slide = 5,
    ZoomBlur = 6,
    TextFade = 7,
    TextSlide = 8,
    TextScale = 9,
    TextBlur = 10,
    TextWipe = 11,
    BlurSlide = 12,
    TextBlurSlide = 13,
    Blocks = 14,
    ImageWipe = 15,
    Clock = 16,
    Iris = 17,
    GradientWipe = 18,
};

enum class LayerTransitionUnit {
    Character = 0,
    Word = 1,
    Sentence = 2,
};

enum class LayerTransitionDirection {
    None = 0,
    Left,
    Right,
    Up,
    Down,
};

inline bool layer_transition_type_is_text(LayerTransitionType type)
{
    switch (type) {
    case LayerTransitionType::TextFade:
    case LayerTransitionType::TextSlide:
    case LayerTransitionType::TextScale:
    case LayerTransitionType::TextBlur:
    case LayerTransitionType::TextWipe:
    case LayerTransitionType::TextBlurSlide:
        return true;
    default:
        return false;
    }
}

struct LayerTransition {
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id;
    std::string preset_id;
    std::string display_name;
    bool enabled = true;
    LayerTransitionKind kind = LayerTransitionKind::General;
    LayerTransitionType type = LayerTransitionType::Dissolve;
    LayerTransitionEdge edge = LayerTransitionEdge::In;
    LayerTransitionUnit unit = LayerTransitionUnit::Character;
    LayerTransitionDirection direction = LayerTransitionDirection::None;
    EasingType easing = EasingType::EaseInOut;
    double duration = 0.5;
    double blur_amount = 18.0;
    double scale_from = 0.82;
    double offset = 80.0;
    double stagger = 0.35;
    double softness = 0.0;
    bool reverse_order = false;
    /* Text Slide/Blur Slide options. Fade preserves the historical default;
     * unit cropping clips the moving unit to its authored Animate By bounds. */
    bool text_slide_fade = true;
    bool text_slide_crop_to_unit_bounds = false;

    /* Procedural/matte transition controls. */
    int blocks_columns = 8;
    int blocks_rows = 6;
    int random_seed = 1;
    std::string image_path;
    int image_channel = 0; /* 0=luma, 1=alpha, 2=R, 3=G, 4=B */
    bool invert = false;
    bool clockwise = true;
    double center_x = 0.5;
    double center_y = 0.5;
    double rotation = 0.0;
    double aspect = 1.0;
    int profile = 0;
};

struct LayerTransitionVisualState {
    double progress = 1.0;       /* 0 = fully transitioned out, 1 = normal layer */
    double opacity = 1.0;
    double scale = 1.0;
    double translate_x = 0.0;
    double translate_y = 0.0;
    double blur = 0.0;
    double wipe = 1.0;
    LayerTransitionDirection wipe_direction = LayerTransitionDirection::None;
    double wipe_softness = 0.0;
    LayerTransitionType type = LayerTransitionType::Dissolve;
    int blocks_columns = 8;
    int blocks_rows = 6;
    int random_seed = 1;
    int image_channel = 0;
    std::string image_path;
    bool invert = false;
    bool clockwise = true;
    double center_x = 0.5;
    double center_y = 0.5;
    double rotation = 0.0;
    double aspect = 1.0;
    int profile = 0;
    bool active = false;
};

inline double layer_transition_ease(double x, EasingType easing)
{
    x = std::clamp(x, 0.0, 1.0);
    switch (easing) {
    case EasingType::Linear: return x;
    case EasingType::EaseIn: return x * x;
    case EasingType::EaseOut: return x * (2.0 - x);
    case EasingType::EaseInOut:
    case EasingType::Bezier:
        return x < 0.5 ? 2.0 * x * x : -1.0 + (4.0 - 2.0 * x) * x;
    case EasingType::Hold: return x >= 1.0 ? 1.0 : 0.0;
    default: return x;
    }
}

inline const LayerTransition *find_layer_transition(const std::vector<LayerTransition> &transitions,
                                                     LayerTransitionEdge edge)
{
    for (const auto &transition : transitions) {
        if (transition.edge == edge)
            return &transition;
    }
    return nullptr;
}

inline LayerTransition *find_layer_transition(std::vector<LayerTransition> &transitions,
                                               LayerTransitionEdge edge)
{
    for (auto &transition : transitions) {
        if (transition.edge == edge)
            return &transition;
    }
    return nullptr;
}

inline double layer_transition_progress(const LayerTransition &transition,
                                        double layer_in_time,
                                        double layer_out_time,
                                        double title_time)
{
    if (!transition.enabled)
        return 1.0;
    const double layer_duration = std::max(0.0, layer_out_time - layer_in_time);
    const double duration = std::clamp(transition.duration, 1e-6, std::max(1e-6, layer_duration));
    double linear = 1.0;
    if (transition.edge == LayerTransitionEdge::In) {
        linear = (title_time - layer_in_time) / duration;
    } else {
        linear = (layer_out_time - title_time) / duration;
    }
    return layer_transition_ease(std::clamp(linear, 0.0, 1.0), transition.easing);
}

inline LayerTransitionVisualState evaluate_general_layer_transition(const LayerTransition &transition,
                                                                    double layer_in_time,
                                                                    double layer_out_time,
                                                                    double title_time)
{
    LayerTransitionVisualState state;
    if (!transition.enabled || transition.kind != LayerTransitionKind::General)
        return state;

    const double duration = std::max(1e-6, transition.duration);
    const bool within = transition.edge == LayerTransitionEdge::In
        ? title_time <= layer_in_time + duration
        : title_time >= layer_out_time - duration;
    if (!within)
        return state;

    state.active = true;
    state.type = transition.type;
    state.blocks_columns = std::max(1, transition.blocks_columns);
    state.blocks_rows = std::max(1, transition.blocks_rows);
    state.random_seed = transition.random_seed;
    state.image_channel = std::clamp(transition.image_channel, 0, 4);
    state.image_path = transition.image_path;
    state.invert = transition.invert;
    state.clockwise = transition.clockwise;
    state.center_x = std::clamp(transition.center_x, 0.0, 1.0);
    state.center_y = std::clamp(transition.center_y, 0.0, 1.0);
    state.rotation = transition.rotation;
    state.aspect = std::max(0.01, transition.aspect);
    state.profile = transition.profile;
    state.progress = layer_transition_progress(transition, layer_in_time, layer_out_time, title_time);
    const double hidden = 1.0 - state.progress;

    switch (transition.type) {
    case LayerTransitionType::Dissolve:
    case LayerTransitionType::Opacity:
        state.opacity = state.progress;
        break;
    case LayerTransitionType::OpacityBlur:
        state.opacity = state.progress;
        state.blur = std::max(0.0, transition.blur_amount) * hidden;
        break;
    case LayerTransitionType::Scale:
        state.opacity = state.progress;
        state.scale = transition.scale_from + (1.0 - transition.scale_from) * state.progress;
        break;
    case LayerTransitionType::Wipe:
        state.wipe = state.progress;
        state.wipe_direction = transition.direction == LayerTransitionDirection::None
            ? LayerTransitionDirection::Left : transition.direction;
        state.wipe_softness = std::clamp(transition.softness, 0.0, 1.0);
        break;
    case LayerTransitionType::Slide:
    case LayerTransitionType::BlurSlide:
        state.opacity = state.progress;
        switch (transition.direction) {
        case LayerTransitionDirection::Right: state.translate_x = transition.offset * hidden; break;
        case LayerTransitionDirection::Up: state.translate_y = -transition.offset * hidden; break;
        case LayerTransitionDirection::Down: state.translate_y = transition.offset * hidden; break;
        case LayerTransitionDirection::Left:
        case LayerTransitionDirection::None:
        default: state.translate_x = -transition.offset * hidden; break;
        }
        if (transition.type == LayerTransitionType::BlurSlide)
            state.blur = std::max(0.0, transition.blur_amount) * hidden;
        break;
    case LayerTransitionType::ZoomBlur:
        state.opacity = state.progress;
        state.scale = transition.scale_from + (1.0 - transition.scale_from) * state.progress;
        state.blur = std::max(0.0, transition.blur_amount) * hidden;
        break;
    case LayerTransitionType::Blocks:
    case LayerTransitionType::ImageWipe:
    case LayerTransitionType::Clock:
    case LayerTransitionType::Iris:
    case LayerTransitionType::GradientWipe:
        state.wipe = state.progress;
        state.wipe_softness = std::clamp(transition.softness, 0.0, 1.0);
        break;
    default:
        break;
    }
    return state;
}

inline LayerTransitionVisualState evaluate_layer_general_transitions(const std::vector<LayerTransition> &transitions,
                                                                     double layer_in_time,
                                                                     double layer_out_time,
                                                                     double title_time)
{
    LayerTransitionVisualState result;
    for (const auto &transition : transitions) {
        const LayerTransitionVisualState state = evaluate_general_layer_transition(
            transition, layer_in_time, layer_out_time, title_time);
        if (!state.active)
            continue;
        result.active = true;
        result.progress = std::min(result.progress, state.progress);
        result.opacity *= state.opacity;
        result.scale *= state.scale;
        result.translate_x += state.translate_x;
        result.translate_y += state.translate_y;
        result.blur = std::max(result.blur, state.blur);
        if (state.wipe < result.wipe) {
            result.wipe = state.wipe;
            result.wipe_direction = state.wipe_direction;
            result.wipe_softness = state.wipe_softness;
            result.type = state.type;
            result.blocks_columns = state.blocks_columns;
            result.blocks_rows = state.blocks_rows;
            result.random_seed = state.random_seed;
            result.image_channel = state.image_channel;
            result.image_path = state.image_path;
            result.invert = state.invert;
            result.clockwise = state.clockwise;
            result.center_x = state.center_x;
            result.center_y = state.center_y;
            result.rotation = state.rotation;
            result.aspect = state.aspect;
            result.profile = state.profile;
        }
    }
    return result;
}
