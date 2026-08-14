#include "layer-transition.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
bool near(double actual, double expected, double epsilon = 1e-9)
{
    return std::abs(actual - expected) <= epsilon;
}
}

int main()
{
    static_assert(static_cast<int>(LayerTransitionType::TextWipe) == 11,
                  "Persisted transition type values must remain backward compatible");
    static_assert(static_cast<int>(LayerTransitionType::BlurSlide) == 12);
    static_assert(static_cast<int>(LayerTransitionType::TextBlurSlide) == 13);
    static_assert(static_cast<int>(LayerTransitionType::Blocks) == 14);
    static_assert(static_cast<int>(LayerTransitionType::ImageWipe) == 15);
    static_assert(static_cast<int>(LayerTransitionType::Clock) == 16);
    static_assert(static_cast<int>(LayerTransitionType::Iris) == 17);
    static_assert(static_cast<int>(LayerTransitionType::GradientWipe) == 18);
    LayerTransition fade_in;
    fade_in.kind = LayerTransitionKind::General;
    fade_in.type = LayerTransitionType::Opacity;
    fade_in.edge = LayerTransitionEdge::In;
    fade_in.easing = EasingType::Linear;
    fade_in.duration = 1.0;

    assert(near(layer_transition_progress(fade_in, 2.0, 8.0, 2.0), 0.0));
    assert(near(layer_transition_progress(fade_in, 2.0, 8.0, 2.5), 0.5));
    assert(near(layer_transition_progress(fade_in, 2.0, 8.0, 3.0), 1.0));

    LayerTransition fade_out = fade_in;
    fade_out.edge = LayerTransitionEdge::Out;
    assert(near(layer_transition_progress(fade_out, 2.0, 8.0, 7.0), 1.0));
    assert(near(layer_transition_progress(fade_out, 2.0, 8.0, 7.5), 0.5));
    assert(near(layer_transition_progress(fade_out, 2.0, 8.0, 8.0), 0.0));

    LayerTransition blur_in = fade_in;
    blur_in.type = LayerTransitionType::OpacityBlur;
    blur_in.blur_amount = 20.0;
    const LayerTransitionVisualState blur_state =
        evaluate_general_layer_transition(blur_in, 2.0, 8.0, 2.25);
    assert(blur_state.active);
    assert(near(blur_state.opacity, 0.25));
    assert(near(blur_state.blur, 15.0));


    LayerTransition blur_slide = fade_in;
    blur_slide.type = LayerTransitionType::BlurSlide;
    blur_slide.direction = LayerTransitionDirection::Left;
    blur_slide.offset = 120.0;
    blur_slide.blur_amount = 24.0;
    const LayerTransitionVisualState blur_slide_state =
        evaluate_general_layer_transition(blur_slide, 2.0, 8.0, 2.25);
    assert(blur_slide_state.active);
    assert(near(blur_slide_state.opacity, 0.25));
    assert(near(blur_slide_state.translate_x, -90.0));
    assert(near(blur_slide_state.blur, 18.0));
    assert(!layer_transition_type_is_text(LayerTransitionType::BlurSlide));
    assert(layer_transition_type_is_text(LayerTransitionType::TextBlurSlide));

    LayerTransition wipe = fade_in;
    wipe.type = LayerTransitionType::Wipe;
    wipe.direction = LayerTransitionDirection::Right;
    wipe.softness = 0.2;
    const LayerTransitionVisualState wipe_state =
        evaluate_general_layer_transition(wipe, 2.0, 8.0, 2.5);
    assert(wipe_state.active);
    assert(near(wipe_state.wipe, 0.5));
    assert(wipe_state.wipe_direction == LayerTransitionDirection::Right);
    assert(near(wipe_state.wipe_softness, 0.2));

    LayerTransition blocks = fade_in;
    blocks.type = LayerTransitionType::Blocks;
    blocks.blocks_columns = 10;
    blocks.blocks_rows = 5;
    blocks.random_seed = 42;
    blocks.softness = 0.1;
    const LayerTransitionVisualState blocks_state =
        evaluate_general_layer_transition(blocks, 2.0, 8.0, 2.5);
    assert(blocks_state.active);
    assert(blocks_state.type == LayerTransitionType::Blocks);
    assert(blocks_state.blocks_columns == 10);
    assert(blocks_state.blocks_rows == 5);
    assert(blocks_state.random_seed == 42);
    assert(near(blocks_state.wipe, 0.5));

    LayerTransition iris = fade_in;
    iris.type = LayerTransitionType::Iris;
    iris.center_x = 0.25;
    iris.center_y = 0.75;
    iris.aspect = 1.5;
    iris.profile = 2;
    const LayerTransitionVisualState iris_state =
        evaluate_general_layer_transition(iris, 2.0, 8.0, 2.5);
    assert(iris_state.type == LayerTransitionType::Iris);
    assert(near(iris_state.center_x, 0.25));
    assert(near(iris_state.center_y, 0.75));
    assert(near(iris_state.aspect, 1.5));
    assert(iris_state.profile == 2);

    std::vector<LayerTransition> transitions{fade_in, fade_out};
    assert(find_layer_transition(transitions, LayerTransitionEdge::In) == &transitions[0]);
    assert(find_layer_transition(transitions, LayerTransitionEdge::Out) == &transitions[1]);

    std::cout << "transition in/out progress, blur slide, text type, wipe, and slot lookup passed\n";
    return 0;
}
