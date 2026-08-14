#include "text-animator-preset-io.h"

#include <cassert>
#include <cstdio>
#include <iostream>

int main()
{
    TextAnimator animator;
    animator.id = "original";
    animator.name = "Editable Reveal";
    animator.granularity = TextAnimatorUnit::Word;
    animator.transform_as_unit = true;
    animator.clip_to_unit_bounds = true;
    animator.change_behaviour = TextChangeBehaviour::AnimateChangedOnly;

    TextAnimatorProperty opacity;
    opacity.id = "opacity";
    opacity.name = "Opacity";
    opacity.type = TextAnimatorPropertyType::Opacity;
    opacity.value.static_value = 0.0;
    Keyframe first; first.time = 0.0; first.value = 0.0;
    first.temporal_mode = TemporalInterpolationMode::ManualBezier;
    first.outgoing_influence = 42.0; first.outgoing_speed = 18.0;
    Keyframe second; second.time = 1.25; second.value = 100.0;
    second.temporal_mode = TemporalInterpolationMode::ManualBezier;
    second.incoming_influence = 27.0; second.incoming_speed = 8.0;
    opacity.value.keyframes = {first, second};
    animator.properties.push_back(opacity);

    TextSelector selector;
    selector.id = "range";
    selector.name = "Words";
    selector.type = TextSelectorType::Staggered;
    selector.based_on = TextAnimatorUnit::Sentence;
    selector.randomize_order = true;
    selector.random_seed = 7391;
    selector.direction = TextSelectorDirection::Reverse;
    selector.completion.keyframes = {first, second};
    selector.stagger_percent.static_value = 42.0;
    selector.unit_easing = EasingType::EaseOut;
    selector.stagger_mode = TextStaggerMode::Exit;
    selector.exclude_whitespace = true;
    animator.selectors.push_back(selector);

    TextAnimatorPresetMetadata metadata;
    metadata.name = "Editable Reveal";
    metadata.category = "Tests";
    metadata.description = "Round-trip contract";
    metadata.identifier = "test.editable-reveal";

    const std::string path = "text-animator-preset-roundtrip.fxmtextanim";
    std::string error;
    assert(save_text_animator_preset_file(path, metadata, animator, &error));
    assert(error.empty());

    TextAnimatorPresetMetadata loaded_metadata;
    TextAnimator loaded;
    assert(load_text_animator_preset_file(path, &loaded_metadata, &loaded, &error));
    assert(error.empty());
    assert(loaded_metadata.name == metadata.name);
    assert(loaded_metadata.identifier == metadata.identifier);
    assert(loaded.name == animator.name);
    assert(loaded.granularity == TextAnimatorUnit::Word);
    assert(loaded.transform_as_unit);
    assert(loaded.clip_to_unit_bounds);
    assert(loaded.change_behaviour == TextChangeBehaviour::AnimateChangedOnly);
    assert(loaded.properties.size() == 1);
    assert(loaded.properties.front().value.keyframes.size() == 2);
    assert(loaded.properties.front().value.keyframes.front().temporal_mode ==
           TemporalInterpolationMode::ManualBezier);
    assert(loaded.selectors.size() == 1);
    assert(loaded.selectors.front().type == TextSelectorType::Staggered);
    assert(loaded.selectors.front().based_on == TextAnimatorUnit::Sentence);
    assert(loaded.selectors.front().random_seed == 7391);
    assert(loaded.selectors.front().direction == TextSelectorDirection::Reverse);
    assert(loaded.selectors.front().completion.keyframes.size() == 2);
    assert(loaded.selectors.front().stagger_percent.static_value == 42.0);
    assert(loaded.selectors.front().unit_easing == EasingType::EaseOut);
    assert(loaded.selectors.front().stagger_mode == TextStaggerMode::Exit);
    assert(loaded.selectors.front().exclude_whitespace);

    reseed_text_animator_ids(loaded, "destination-layer", 3);
    assert(loaded.id != animator.id);
    assert(loaded.properties.front().id != opacity.id);
    assert(loaded.selectors.front().id != selector.id);

    std::remove(path.c_str());
    std::cout << "text animator preset IO round-trip passed\n";
    return 0;
}
