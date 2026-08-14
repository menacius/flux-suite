#include "text-animator.h"
#include "text-animator-presets.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool near(double actual, double expected, double epsilon = 1.0e-6)
{
    return std::abs(actual - expected) <= epsilon;
}

double legacy_transition_ease(double x, EasingType easing)
{
    x = std::clamp(x, 0.0, 1.0);
    switch (easing) {
    case EasingType::Linear: return x;
    case EasingType::EaseIn: return x * x;
    case EasingType::EaseOut: return x * (2.0 - x);
    case EasingType::EaseInOut:
    case EasingType::Bezier:
        return x < 0.5 ? 2.0 * x * x
                       : -1.0 + (4.0 - 2.0 * x) * x;
    case EasingType::Hold: return x >= 1.0 ? 1.0 : 0.0;
    }
    return x;
}

double per_unit_hidden_influence(bool exit_animation,
                                 double timeline_fraction,
                                 size_t rank, size_t count, double stagger,
                                 EasingType easing)
{
    const double delay = count <= 1 ? 0.0
        : stagger * static_cast<double>(rank) /
          static_cast<double>(count - 1);
    const double span = std::max(0.05, 1.0 - stagger);
    const double local = std::clamp(
        (timeline_fraction - delay) / span, 0.0, 1.0);
    return exit_animation
        ? 1.0 - legacy_transition_ease(1.0 - local, easing)
        : 1.0 - legacy_transition_ease(local, easing);
}

TextLayoutCluster cluster(size_t byte_start, size_t byte_length,
                          uint32_t run, uint32_t line)
{
    TextLayoutCluster value;
    value.byte_start = byte_start;
    value.byte_length = byte_length;
    value.run_index = run;
    value.line_index = line;
    value.width = 10.0f;
    value.height = 20.0f;
    return value;
}

TextLayoutData representative_layout()
{
    TextLayoutData layout;
    /* The shaped clusters intentionally include a combining sequence and one
     * complete ZWJ emoji as indivisible units. The animator must trust these
     * boundaries rather than split UTF-8 bytes/code points. */
    const std::vector<std::string> units = {
        "A", " ", "Ε\u0301", " ", "👩‍💻", "\n", "ש", "ל", "ו", "ם"};
    for (const std::string &unit : units)
        layout.text += unit;

    size_t offset = 0;
    for (size_t i = 0; i < units.size(); ++i) {
        const uint32_t line = i <= 4 ? 0u : 1u;
        const uint32_t run = i <= 2 ? 0u : (i <= 5 ? 1u : 2u);
        layout.clusters.push_back(cluster(offset, units[i].size(), run, line));
        offset += units[i].size();
    }

    TextLayoutLine first;
    first.byte_start = 0;
    first.byte_length = units[0].size() + units[1].size() + units[2].size() +
                        units[3].size() + units[4].size();
    first.cluster_begin = 0;
    first.cluster_count = 5;
    first.paragraph_index = 0;
    TextLayoutLine second;
    second.byte_start = first.byte_length + units[5].size();
    second.byte_length = layout.text.size() - second.byte_start;
    second.cluster_begin = 6;
    second.cluster_count = 4;
    second.paragraph_index = 1;
    layout.lines = {first, second};

    TextLayoutRun run0;
    run0.byte_start = 0;
    run0.byte_length = units[0].size() + units[1].size() + units[2].size();
    run0.cluster_begin = 0;
    run0.cluster_count = 3;
    TextLayoutRun run1;
    run1.byte_start = run0.byte_length;
    run1.byte_length = units[3].size() + units[4].size() + units[5].size();
    run1.cluster_begin = 3;
    run1.cluster_count = 3;
    TextLayoutRun run2;
    run2.byte_start = run0.byte_length + run1.byte_length;
    run2.byte_length = layout.text.size() - run2.byte_start;
    run2.cluster_begin = 6;
    run2.cluster_count = 4;
    layout.runs = {run0, run1, run2};
    layout.width = 100.0f;
    layout.height = 40.0f;
    layout.valid = true;
    return layout;
}

TextAnimatorProperty opacity_property(double value)
{
    TextAnimatorProperty property;
    property.id = "opacity";
    property.name = "Opacity";
    property.type = TextAnimatorPropertyType::Opacity;
    property.value.static_value = value;
    return property;
}

TextLayoutData layout_from_clusters(const std::vector<std::string> &clusters)
{
    TextLayoutData layout;
    size_t offset = 0;
    for (size_t index = 0; index < clusters.size(); ++index) {
        const std::string &value = clusters[index];
        layout.text += value;
        TextLayoutCluster shaped = cluster(offset, value.size(), 0, 0);
        shaped.x = static_cast<float>(index * 10.0);
        shaped.y = 0.0f;
        layout.clusters.push_back(shaped);
        offset += value.size();
    }
    TextLayoutLine line;
    line.byte_start = 0;
    line.byte_length = layout.text.size();
    line.cluster_begin = 0;
    line.cluster_count = static_cast<uint32_t>(layout.clusters.size());
    layout.lines.push_back(line);
    TextLayoutRun run;
    run.byte_start = 0;
    run.byte_length = layout.text.size();
    run.cluster_begin = 0;
    run.cluster_count = static_cast<uint32_t>(layout.clusters.size());
    layout.runs.push_back(run);
    layout.valid = true;
    return layout;
}

TextSelector all_selector(TextAnimatorUnit unit = TextAnimatorUnit::Grapheme)
{
    TextSelector selector;
    selector.id = "all";
    selector.name = "All";
    selector.type = TextSelectorType::Range;
    selector.based_on = unit;
    selector.range_units = TextRangeUnits::Percentage;
    selector.start.static_value = 0.0;
    selector.end.static_value = 100.0;
    selector.smoothness.static_value = 0.0;
    return selector;
}
} // namespace

int main()
{
    const TextLayoutData layout = representative_layout();
    const TextAnimatorUnitMap map = build_text_animator_unit_map(layout);
    assert(map.graphemes.size() == 10);
    assert(map.characters_excluding_spaces.size() == 7);
    assert(map.words.size() == 4);
    assert(map.sentences.size() == 2);
    assert(!map.sentences[0].whitespace);
    assert(!map.sentences[1].whitespace);
    assert(map.lines.size() == 2);
    assert(map.paragraphs.size() == 2);
    assert(map.rich_text_runs.size() == 3);
    assert(map.graphemes[2].byte_length == std::string("Ε\u0301").size());
    assert(map.graphemes[4].byte_length == std::string("👩‍💻").size());

    /* A soft-wrapped line may not expose its break as a shaped whitespace
     * cluster. The final word of one visual line and the first word of the
     * next must still be separate Animate By units. */
    TextLayoutData wrapped_words = layout_from_clusters({"LAST", "NEXT"});
    wrapped_words.clusters[1].line_index = 1;
    const TextAnimatorUnitMap wrapped_word_map =
        build_text_animator_unit_map(wrapped_words);
    assert(wrapped_word_map.words.size() == 2);
    assert(wrapped_word_map.words[0].byte_start == 0);
    assert(wrapped_word_map.words[1].byte_start == 4);

    /* Mixed font runs can arrive from the shaping backend in allocation order
     * rather than logical order. Word construction and stagger rank must use
     * byte order, while retaining the real non-contiguous cluster membership. */
    TextLayoutData scrambled_runs =
        layout_from_clusters({"ONE", " ", "TWO", " ", "THREE"});
    const std::vector<TextLayoutCluster> logical_run_clusters =
        scrambled_runs.clusters;
    scrambled_runs.clusters = {
        logical_run_clusters[4], logical_run_clusters[3],
        logical_run_clusters[0], logical_run_clusters[1],
        logical_run_clusters[2]};
    const TextAnimatorUnitMap scrambled_word_map =
        build_text_animator_unit_map(scrambled_runs);
    assert(scrambled_word_map.words.size() == 3);
    assert(scrambled_word_map.words[0].byte_start == 0);
    assert(scrambled_word_map.words[1].byte_start == 4);
    assert(scrambled_word_map.words[2].byte_start == 8);
    assert(scrambled_word_map.words[0].cluster_indices ==
           std::vector<size_t>{2});
    assert(scrambled_word_map.words[1].cluster_indices ==
           std::vector<size_t>{4});
    assert(scrambled_word_map.words[2].cluster_indices ==
           std::vector<size_t>{0});
    TextSelector logical_word_stagger =
        all_selector(TextAnimatorUnit::Word);
    logical_word_stagger.type = TextSelectorType::Staggered;
    logical_word_stagger.exclude_whitespace = true;
    logical_word_stagger.completion.static_value = 50.0;
    logical_word_stagger.stagger_percent.static_value = 50.0;
    logical_word_stagger.unit_easing = EasingType::Linear;
    assert(near(evaluate_text_selector_for_cluster(
        logical_word_stagger, scrambled_runs, scrambled_word_map, 2, 0.0),
        0.0));
    assert(near(evaluate_text_selector_for_cluster(
        logical_word_stagger, scrambled_runs, scrambled_word_map, 4, 0.0),
        0.5));
    assert(near(evaluate_text_selector_for_cluster(
        logical_word_stagger, scrambled_runs, scrambled_word_map, 0, 0.0),
        1.0));

    /* Text-based selection uses Unicode-safe shaped units and recognises Greek
     * uppercase without treating continuation bytes as characters. */
    TextSelector greek_upper = all_selector();
    greek_upper.type = TextSelectorType::TextBased;
    greek_upper.match_mode = TextMatchMode::Uppercase;
    assert(near(evaluate_text_selector_for_cluster(
        greek_upper, layout, map, 2, 0.0), 0.0));
    /* Combining marks mean an all-code-point predicate is intentionally strict.
     * Exact matching still selects the complete grapheme. */
    greek_upper.match_mode = TextMatchMode::ExactText;
    greek_upper.match_text = "Ε\u0301";
    assert(near(evaluate_text_selector_for_cluster(
        greek_upper, layout, map, 2, 0.0), 1.0));

    TextSelector whitespace = all_selector();
    whitespace.type = TextSelectorType::TextBased;
    whitespace.match_mode = TextMatchMode::Whitespace;
    assert(near(evaluate_text_selector_for_cluster(
        whitespace, layout, map, 1, 0.0), 1.0));
    assert(near(evaluate_text_selector_for_cluster(
        whitespace, layout, map, 0, 0.0), 0.0));

    /* Seeded procedural and wiggly selectors are stable under repeated
     * playback/scrubbing evaluations. */
    TextSelector random = all_selector();
    random.type = TextSelectorType::Procedural;
    random.procedural_mode = TextProceduralMode::Random;
    random.random_seed = 73;
    const double random_a = evaluate_text_selector_for_cluster(
        random, layout, map, 4, 12.5);
    const double random_b = evaluate_text_selector_for_cluster(
        random, layout, map, 4, 12.5);
    assert(near(random_a, random_b, 1.0e-12));

    TextSelector wiggly = all_selector();
    wiggly.type = TextSelectorType::Wiggly;
    wiggly.wiggly_seed = 99;
    wiggly.wiggly_frequency.static_value = 2.0;
    const double wiggle_a = evaluate_text_selector_for_cluster(
        wiggly, layout, map, 6, 1.25);
    const double wiggle_b = evaluate_text_selector_for_cluster(
        wiggly, layout, map, 6, 1.25);
    assert(near(wiggle_a, wiggle_b, 1.0e-12));

    TextSelector sentence_stagger = all_selector(TextAnimatorUnit::Sentence);
    sentence_stagger.type = TextSelectorType::Staggered;
    sentence_stagger.exclude_whitespace = true;
    sentence_stagger.completion.static_value = 50.0;
    sentence_stagger.stagger_percent.static_value = 50.0;
    sentence_stagger.unit_easing = EasingType::Linear;
    assert(near(evaluate_text_selector_for_cluster(
        sentence_stagger, layout, map, 0, 0.0), 0.0));
    assert(near(evaluate_text_selector_for_cluster(
        sentence_stagger, layout, map, 6, 0.0), 1.0));

    /* Multiple animators compose in stack order over the same clusters. */
    TextAnimatorStack stack;
    TextAnimator fade;
    fade.id = "fade";
    fade.properties.push_back(opacity_property(0.5));
    fade.selectors.push_back(all_selector());
    stack.animators.push_back(fade);
    TextAnimator move;
    move.id = "move";
    TextAnimatorProperty position;
    position.id = "position";
    position.name = "Position";
    position.type = TextAnimatorPropertyType::Position;
    position.value.static_value = 20.0;
    position.secondary.static_value = -5.0;
    move.properties.push_back(position);
    move.selectors.push_back(all_selector(TextAnimatorUnit::WholeLayer));
    stack.animators.push_back(move);
    const TextAnimatorEvaluation composed = evaluate_text_animators(stack, layout, 0.0);
    assert(composed.clusters.size() == layout.clusters.size());
    for (const TextAnimatorClusterState &state : composed.clusters) {
        assert(near(state.opacity, 0.5));
        assert(near(state.position_x, 20.0));
        assert(near(state.position_y, -5.0));
    }

    /* Legacy descriptors remain as timeline/transition-editor metadata, while
     * a bound generic animator is the sole runtime implementation. */
    LayerTransition legacy;
    legacy.id = "legacy-slide";
    legacy.preset_id = "text.slide-in";
    legacy.display_name = "Slide In";
    legacy.kind = LayerTransitionKind::Text;
    legacy.type = LayerTransitionType::TextSlide;
    legacy.edge = LayerTransitionEdge::In;
    legacy.direction = LayerTransitionDirection::Left;
    legacy.duration = 1.0;
    legacy.offset = 80.0;
    legacy.easing = EasingType::Linear;
    LayerTransition general;
    general.id = "general";
    general.kind = LayerTransitionKind::General;
    general.type = LayerTransitionType::Dissolve;
    std::vector<LayerTransition> transitions{legacy, general};
    TextAnimatorStack migrated_stack;
    std::vector<std::string> warnings;
    assert(migrate_legacy_text_transitions(
        transitions, migrated_stack, 5.0, 10.0, &warnings));
    assert(transitions.size() == 2);
    assert(find_layer_transition(transitions, LayerTransitionEdge::In) != nullptr);
    assert(migrated_stack.animators.size() == 1);
    const TextAnimator &migrated = migrated_stack.animators.front();
    assert(migrated.local_time_offset == 0.0); /* layer-local, not title time */
    assert(migrated.properties.size() == 2);
    assert(migrated.selectors.size() == 1);
    assert(migrated.selectors.front().type == TextSelectorType::Staggered);
    assert(!migrated.selectors.front().completion.keyframes.empty());
    for (const Keyframe &completion_key :
         migrated.selectors.front().completion.keyframes) {
        assert(completion_key.easing == EasingType::Linear);
        assert(completion_key.temporal_mode ==
               TemporalInterpolationMode::Linear);
    }
    assert(migrated.transition_managed);
    assert(migrated.transition_id == legacy.id);
    assert(migrated.transform_as_unit);
    assert(migrated.preset_id == "text.slide-in");

    const TextAnimatorEvaluation hidden = evaluate_text_animators(
        migrated_stack, layout, 0.0);
    for (size_t i = 0; i < hidden.clusters.size(); ++i) {
        if (map.graphemes[i].whitespace)
            continue;
        assert(near(hidden.clusters[i].opacity, 0.0));
        assert(near(hidden.clusters[i].position_x, -80.0));
    }
    const TextAnimatorEvaluation revealed = evaluate_text_animators(
        migrated_stack, layout, 1.0);
    for (const TextAnimatorClusterState &state : revealed.clusters) {
        assert(near(state.opacity, 1.0));
        assert(near(state.position_x, 0.0));
    }

    LayerTransition cropped_slide = legacy;
    cropped_slide.id = "cropped-slide";
    cropped_slide.text_slide_fade = false;
    cropped_slide.text_slide_crop_to_unit_bounds = true;
    const TextAnimator cropped_slide_animator =
        make_text_animator_from_legacy_transition(
            cropped_slide, 0.0, 1.0, nullptr);
    assert(cropped_slide_animator.clip_to_unit_bounds);
    assert(std::none_of(
        cropped_slide_animator.properties.begin(),
        cropped_slide_animator.properties.end(),
        [](const TextAnimatorProperty &property) {
            return property.type == TextAnimatorPropertyType::Opacity;
        }));
    TextAnimatorStack cropped_slide_stack;
    cropped_slide_stack.animators.push_back(cropped_slide_animator);
    const TextAnimatorEvaluation cropped_slide_eval =
        evaluate_text_animators(cropped_slide_stack, layout, 0.5);
    assert(cropped_slide_eval.clusters.front().has_unit_clip_bounds);

    /* A word or sentence may span more than one shaped visual line. Its
     * animation remains one timing unit, but each cluster must be clipped to
     * the unit fragment on its own line; one union rectangle would expose or
     * suppress content from the other lines during a slide. */
    TextLayoutData multiline_unit =
        layout_from_clusters({"A", "B", "C", "D"});
    multiline_unit.clusters[0].x = 100.0f;
    multiline_unit.clusters[1].x = 110.0f;
    multiline_unit.clusters[2].x = 20.0f;
    multiline_unit.clusters[3].x = 30.0f;
    multiline_unit.clusters[2].y = 30.0f;
    multiline_unit.clusters[3].y = 30.0f;
    multiline_unit.clusters[2].line_index = 1;
    multiline_unit.clusters[3].line_index = 1;
    multiline_unit.lines.clear();
    TextLayoutLine multiline_first;
    multiline_first.cluster_begin = 0;
    multiline_first.cluster_count = 2;
    TextLayoutLine multiline_second;
    multiline_second.cluster_begin = 2;
    multiline_second.cluster_count = 2;
    multiline_unit.lines = {multiline_first, multiline_second};

    for (const TextAnimatorUnit unit :
         {TextAnimatorUnit::Word, TextAnimatorUnit::Sentence}) {
        TextAnimatorStack multiline_crop_stack;
        TextAnimator multiline_crop;
        multiline_crop.id = "multiline-crop";
        multiline_crop.granularity = unit;
        multiline_crop.clip_to_unit_bounds = true;
        multiline_crop_stack.animators.push_back(multiline_crop);
        const TextAnimatorEvaluation multiline_crop_eval =
            evaluate_text_animators(multiline_crop_stack, multiline_unit, 0.0);
        const TextAnimatorClusterState &first_line =
            multiline_crop_eval.clusters[0];
        const TextAnimatorClusterState &second_line =
            multiline_crop_eval.clusters[2];
        assert(first_line.has_unit_clip_bounds);
        assert(second_line.has_unit_clip_bounds);
        assert(near(first_line.unit_clip_x0, 100.0));
        assert(near(first_line.unit_clip_y0, 0.0));
        assert(near(first_line.unit_clip_x1, 120.0));
        assert(near(first_line.unit_clip_y1, 20.0));
        assert(near(second_line.unit_clip_x0, 20.0));
        assert(near(second_line.unit_clip_y0, 30.0));
        assert(near(second_line.unit_clip_x1, 40.0));
        assert(near(second_line.unit_clip_y1, 50.0));
    }

    /* Crop-enabled slide distance is run-aware. With one large and one small
     * rich-text line, the authored offset remains a minimum, while the large
     * glyph travels far enough to leave its own stationary crop completely. */
    TextLayoutData mixed_size_unit = layout_from_clusters({"A", "B"});
    mixed_size_unit.clusters[0].x = 100.0f;
    mixed_size_unit.clusters[0].y = 0.0f;
    mixed_size_unit.clusters[0].width = 50.0f;
    mixed_size_unit.clusters[0].height = 200.0f;
    mixed_size_unit.clusters[0].glyph_begin = 0;
    mixed_size_unit.clusters[0].glyph_count = 1;
    mixed_size_unit.clusters[1].x = 20.0f;
    mixed_size_unit.clusters[1].y = 220.0f;
    mixed_size_unit.clusters[1].width = 20.0f;
    mixed_size_unit.clusters[1].height = 80.0f;
    mixed_size_unit.clusters[1].line_index = 1;
    mixed_size_unit.clusters[1].glyph_begin = 1;
    mixed_size_unit.clusters[1].glyph_count = 1;
    TextLayoutGlyph large_glyph;
    large_glyph.cluster_index = 0;
    large_glyph.ink_x = 100.0f;
    large_glyph.ink_y = 10.0f;
    large_glyph.ink_width = 50.0f;
    large_glyph.ink_height = 180.0f;
    TextLayoutGlyph small_glyph;
    small_glyph.cluster_index = 1;
    small_glyph.ink_x = 20.0f;
    small_glyph.ink_y = 230.0f;
    small_glyph.ink_width = 20.0f;
    small_glyph.ink_height = 60.0f;
    mixed_size_unit.glyphs = {large_glyph, small_glyph};

    for (const TextAnimatorUnit unit :
         {TextAnimatorUnit::Word, TextAnimatorUnit::Sentence}) {
        for (const double direction : {-1.0, 1.0}) {
            TextAnimatorStack mixed_size_stack;
            TextAnimator mixed_size_slide;
            mixed_size_slide.id = "mixed-size-crop";
            mixed_size_slide.granularity = unit;
            mixed_size_slide.clip_to_unit_bounds = true;
            TextAnimatorProperty mixed_size_position;
            mixed_size_position.type = TextAnimatorPropertyType::Position;
            mixed_size_position.secondary.static_value = direction * 80.0;
            mixed_size_slide.properties.push_back(mixed_size_position);
            mixed_size_stack.animators.push_back(mixed_size_slide);
            const TextAnimatorEvaluation mixed_size_eval =
                evaluate_text_animators(
                    mixed_size_stack, mixed_size_unit, 0.0);
            assert(near(
                mixed_size_eval.clusters[0].position_y,
                direction * 191.0));
            assert(near(
                mixed_size_eval.clusters[1].position_y,
                direction * 80.0));
        }
    }

    /* Grouped unit transforms use one common shaped-unit origin. Without
     * this, a word-level Scale Text transition would shrink each glyph around
     * itself instead of shrinking the complete word like historical FXM. */
    const TextLayoutData two_glyph_word = layout_from_clusters({"A", "B"});
    TextAnimatorStack grouped_scale_stack;
    TextAnimator grouped_scale;
    grouped_scale.id = "grouped-scale";
    grouped_scale.granularity = TextAnimatorUnit::Word;
    grouped_scale.transform_as_unit = true;
    TextAnimatorProperty grouped_scale_property;
    grouped_scale_property.type = TextAnimatorPropertyType::Scale;
    grouped_scale_property.value.static_value = 0.5;
    grouped_scale_property.secondary.static_value = 0.5;
    grouped_scale.properties.push_back(grouped_scale_property);
    grouped_scale.selectors.push_back(all_selector(TextAnimatorUnit::Word));
    grouped_scale_stack.animators.push_back(grouped_scale);
    const TextAnimatorEvaluation grouped_scale_eval = evaluate_text_animators(
        grouped_scale_stack, two_glyph_word, 0.0);
    assert(grouped_scale_eval.clusters.size() == 2);
    assert(grouped_scale_eval.clusters[0].has_transform_origin);
    assert(grouped_scale_eval.clusters[1].has_transform_origin);
    assert(near(grouped_scale_eval.clusters[0].transform_origin_x, 10.0));
    assert(near(grouped_scale_eval.clusters[1].transform_origin_x, 10.0));
    assert(near(grouped_scale_eval.clusters[0].scale_x, 0.5));
    assert(near(grouped_scale_eval.clusters[1].scale_x, 0.5));

    LayerTransition exit_legacy = legacy;
    exit_legacy.id = "legacy-exit";
    exit_legacy.edge = LayerTransitionEdge::Out;
    TextAnimator exit_animator = make_text_animator_from_legacy_transition(
        exit_legacy, 5.0, 10.0, nullptr);
    assert(near(exit_animator.local_time_offset, 0.0));
    assert(near(exit_animator.selectors.front().completion.keyframes.front().time, 4.0));
    assert(near(exit_animator.selectors.front().completion.keyframes.back().time, 5.0));
    assert(exit_animator.selectors.front().stagger_mode == TextStaggerMode::Exit);

    /* The exact legacy stagger formula is now a generic selector. At 50%
     * completion with 50% stagger, the first unit is fully revealed while the
     * last unit is still hidden. */
    TextSelector staggered;
    staggered.type = TextSelectorType::Staggered;
    staggered.based_on = TextAnimatorUnit::CharacterExcludingSpaces;
    staggered.completion.static_value = 50.0;
    staggered.stagger_percent.static_value = 50.0;
    staggered.unit_easing = EasingType::Linear;
    staggered.stagger_mode = TextStaggerMode::Entrance;
    assert(near(evaluate_text_selector_for_cluster(
        staggered, layout, map, 0, 0.0), 0.0));
    assert(near(evaluate_text_selector_for_cluster(
        staggered, layout, map, 9, 0.0), 1.0));
    staggered.direction = TextSelectorDirection::Reverse;
    assert(near(evaluate_text_selector_for_cluster(
        staggered, layout, map, 0, 0.0), 1.0));
    assert(near(evaluate_text_selector_for_cluster(
        staggered, layout, map, 9, 0.0), 0.0));

    /* Completion stays linear; easing is applied independently after each
     * Animate By unit's stagger delay for both timeline edges. */
    const EasingType easing_modes[] = {
        EasingType::Linear, EasingType::EaseIn, EasingType::EaseOut,
        EasingType::EaseInOut, EasingType::Bezier, EasingType::Hold};
    const size_t visible_cluster_indices[] = {0, 2, 4, 6, 7, 8, 9};
    for (EasingType easing_mode : easing_modes) {
        for (bool exit_animation : {false, true}) {
            LayerTransition exact = legacy;
            exact.id = exit_animation ? "exact-exit" : "exact-in";
            exact.edge = exit_animation ? LayerTransitionEdge::Out
                                        : LayerTransitionEdge::In;
            exact.unit = LayerTransitionUnit::Character;
            exact.stagger = 0.35;
            exact.easing = easing_mode;
            TextAnimator exact_animator = make_text_animator_from_legacy_transition(
                exact, 0.0, 2.0, nullptr);
            const TextSelector &exact_selector = exact_animator.selectors.front();
            for (double fraction : {0.0, 0.15, 0.5, 0.85, 1.0}) {
                const double time = exit_animation ? 1.0 + fraction : fraction;
                for (size_t rank = 0; rank < 7; ++rank) {
                    const double actual = evaluate_text_selector_for_cluster(
                        exact_selector, layout, map,
                        visible_cluster_indices[rank], time);
                    const double expected = per_unit_hidden_influence(
                        exit_animation, fraction, rank, 7,
                        exact.stagger, easing_mode);
                    if (!near(actual, expected, 1.0e-6)) {
                        std::cerr << "stagger mismatch easing=" << (int)easing_mode
                                  << " exit=" << exit_animation
                                  << " fraction=" << fraction
                                  << " rank=" << rank
                                  << " actual=" << actual
                                  << " expected=" << expected << "\n";
                        assert(false);
                    }
                }
            }
        }
    }

    /* Migration is idempotent and an editor/timeline change rebuilds the same
     * bound animator instead of installing another runtime engine. */
    assert(!migrate_legacy_text_transitions(
        transitions, migrated_stack, 5.0, 10.0, &warnings));
    LayerTransition *edited_transition = find_layer_transition(
        transitions, LayerTransitionEdge::In);
    assert(edited_transition);
    edited_transition->duration = 2.0;
    assert(synchronize_text_transition_animators(
        transitions, migrated_stack, 5.0, 10.0, nullptr, true));
    const TextAnimator *rebound = find_managed_text_transition_animator(
        migrated_stack, edited_transition->id);
    assert(rebound);
    assert(rebound->transition_binding_signature != 0);
    assert(near(rebound->selectors.front().completion.keyframes.back().time, 2.0));
    /* A no-op authoring synchronization must not erase manual edits to the
     * concrete animator generated by the transition preset. */
    TextAnimator *editable_rebound = find_managed_text_transition_animator(
        migrated_stack, edited_transition->id);
    assert(editable_rebound);
    editable_rebound->properties.front().value.static_value = 321.0;
    assert(!synchronize_text_transition_animators(
        transitions, migrated_stack, 5.0, 10.0, nullptr, true));
    editable_rebound = find_managed_text_transition_animator(
        migrated_stack, edited_transition->id);
    assert(editable_rebound);
    assert(near(editable_rebound->properties.front().value.static_value, 321.0));
    auto orphan_transitions = transitions;
    TextAnimatorStack orphan_stack = migrated_stack;
    orphan_transitions.erase(std::remove_if(orphan_transitions.begin(), orphan_transitions.end(),
        [](const LayerTransition &value) { return layer_transition_type_is_text(value.type); }),
        orphan_transitions.end());
    assert(synchronize_text_transition_animators(
        orphan_transitions, orphan_stack, 5.0, 10.0, nullptr, false));
    assert(find_managed_text_transition_animator(
        orphan_stack, legacy.id) == nullptr);

    /* Development versions 134/135 removed the timeline descriptor after
     * generating a v1 Range-selector animator. Reconstruct the descriptor,
     * replace any conflicting general transition on the same edge, and then
     * upgrade to the exact v2 Staggered selector without user intervention. */
    TextAnimatorStack v1_stack;
    v1_stack.schema_version = 1;
    v1_stack.legacy_migration_version = 1;
    TextAnimator v1_animator;
    v1_animator.id = "v1-slide-animator";
    v1_animator.name = "Slide In";
    v1_animator.preset_id = "text.slide-in";
    v1_animator.preset_schema_version = 1;
    v1_animator.playback_role = TextAnimatorPlaybackRole::Entrance;
    v1_animator.granularity = TextAnimatorUnit::Word;
    TextAnimatorProperty v1_position;
    v1_position.type = TextAnimatorPropertyType::Position;
    v1_position.value.static_value = 64.0;
    v1_position.secondary.static_value = 0.0;
    v1_animator.properties.push_back(v1_position);
    v1_animator.properties.push_back(opacity_property(0.0));
    TextSelector v1_selector;
    v1_selector.type = TextSelectorType::Range;
    v1_selector.based_on = TextAnimatorUnit::Word;
    v1_selector.direction = TextSelectorDirection::Reverse;
    v1_selector.smoothness.static_value = 60.0; /* legacy stagger=0.4 */
    Keyframe v1_start;
    v1_start.time = 0.0;
    v1_start.value = 0.0;
    v1_start.easing = EasingType::EaseOut;
    Keyframe v1_end = v1_start;
    v1_end.time = 1.25;
    v1_end.value = 100.001;
    v1_selector.start.keyframes = {v1_start, v1_end};
    v1_animator.selectors.push_back(v1_selector);
    v1_stack.animators.push_back(v1_animator);

    LayerTransition conflicting_general;
    conflicting_general.id = "old-general-in";
    conflicting_general.kind = LayerTransitionKind::General;
    conflicting_general.type = LayerTransitionType::Dissolve;
    conflicting_general.edge = LayerTransitionEdge::In;
    std::vector<LayerTransition> v1_transitions{conflicting_general};
    std::vector<std::string> v1_warnings;
    assert(migrate_legacy_text_transitions(
        v1_transitions, v1_stack, 2.0, 7.0, &v1_warnings));
    assert(v1_transitions.size() == 1);
    const LayerTransition *recovered_v1 = find_layer_transition(
        v1_transitions, LayerTransitionEdge::In);
    assert(recovered_v1);
    assert(recovered_v1->kind == LayerTransitionKind::Text);
    assert(recovered_v1->type == LayerTransitionType::TextSlide);
    assert(recovered_v1->unit == LayerTransitionUnit::Word);
    assert(recovered_v1->reverse_order);
    assert(near(recovered_v1->duration, 1.25));
    assert(near(recovered_v1->stagger, 0.4));
    assert(near(recovered_v1->offset, 64.0));
    assert(recovered_v1->direction == LayerTransitionDirection::Right);
    assert(v1_stack.schema_version == kTextAnimatorSchemaVersion);
    assert(v1_stack.legacy_migration_version ==
           kTextAnimatorLegacyMigrationVersion);
    assert(v1_stack.animators.size() == 1);
    assert(v1_stack.animators.front().transition_managed);
    assert(v1_stack.animators.front().transition_id == recovered_v1->id);
    assert(v1_stack.animators.front().selectors.front().type ==
           TextSelectorType::Staggered);
    assert(!migrate_legacy_text_transitions(
        v1_transitions, v1_stack, 2.0, 7.0, &v1_warnings));

    /* Directional wipe stores a visible fraction and shaped-unit bounds for
     * the shared glyph compositor rather than invoking the old image path. */
    LayerTransition wipe = legacy;
    wipe.id = "wipe";
    wipe.type = LayerTransitionType::TextWipe;
    wipe.direction = LayerTransitionDirection::Right;
    wipe.stagger = 0.0;
    TextAnimatorStack wipe_stack;
    wipe_stack.animators.push_back(make_text_animator_from_legacy_transition(
        wipe, 0.0, 2.0, nullptr));
    const TextAnimatorEvaluation wipe_half = evaluate_text_animators(
        wipe_stack, layout, 0.5);
    assert(wipe_half.clusters.front().reveal_direction == TextRevealDirection::Right);
    assert(wipe_half.clusters.front().has_reveal_bounds);
    assert(wipe_half.clusters.front().reveal > 0.0 &&
           wipe_half.clusters.front().reveal < 1.0);

    /* Range direction is shared by entrance/exit instead of having a separate
     * reverse renderer path. */
    TextSelector forward = all_selector();
    forward.start.static_value = 50.0;
    forward.end.static_value = 100.0;
    forward.direction = TextSelectorDirection::Forward;
    TextSelector reverse = forward;
    reverse.direction = TextSelectorDirection::Reverse;
    assert(evaluate_text_selector_for_cluster(forward, layout, map, 0, 0.0) <
           evaluate_text_selector_for_cluster(forward, layout, map, 9, 0.0));
    assert(evaluate_text_selector_for_cluster(reverse, layout, map, 0, 0.0) >
           evaluate_text_selector_for_cluster(reverse, layout, map, 9, 0.0));

    assert(text_animator_stack_has_enabled_animators(migrated_stack));
    assert(text_animator_stack_is_time_dependent(migrated_stack));
    const uint64_t signature_before = text_animator_stack_signature(migrated_stack);
    migrated_stack.animators.front().properties.front().value.static_value = 123.0;
    const uint64_t signature_after = text_animator_stack_signature(migrated_stack);
    assert(signature_before != signature_after);
    assert(text_animator_stack_visual_padding(migrated_stack, 200.0, 80.0) >= 80.0);

    /* Dynamic content is remapped by shaped clusters, including a complete
     * emoji ZWJ sequence. No stale glyph index survives the revision. */
    const TextLayoutData previous_content = layout_from_clusters(
        {"A", " ", "👩‍💻", " ", "B"});
    const TextLayoutData inserted_content = layout_from_clusters(
        {"A", " ", "Ν", "έ", "ο", " ", "👩‍💻", " ", "B"});
    const TextAnimatorContentChange insertion =
        map_text_animator_content_change(previous_content, inserted_content);
    assert(!insertion.identical);
    assert(insertion.added_byte_ranges.size() == 1);
    assert(insertion.changed_byte_ranges.empty());
    assert(insertion.current_to_previous[6] == 2); /* emoji remains mapped */

    const TextLayoutData replaced_content = layout_from_clusters(
        {"A", " ", "👩‍💻", " ", "Γ"});
    const TextAnimatorContentChange replacement =
        map_text_animator_content_change(previous_content, replaced_content);
    assert(replacement.added_byte_ranges.empty());
    assert(replacement.changed_byte_ranges.size() == 1);
    assert(replacement.removed_byte_ranges.size() == 1);

    TextAnimatorStack changed_only_stack;
    TextAnimator changed_only;
    changed_only.id = "changed-only";
    changed_only.change_behaviour = TextChangeBehaviour::AnimateChangedOnly;
    changed_only.properties.push_back(opacity_property(0.0));
    changed_only.selectors.push_back(all_selector());
    changed_only_stack.animators.push_back(changed_only);
    const TextAnimatorStack changed_derived = text_animator_stack_for_content_change(
        changed_only_stack, replacement, 3.0);
    assert(changed_derived.animators.front().selectors.size() == 2);
    assert(near(changed_derived.animators.front().local_time_offset, 3.0));
    const TextAnimatorEvaluation changed_eval = evaluate_text_animators(
        changed_derived, replaced_content, 3.0);
    assert(near(changed_eval.clusters[0].opacity, 1.0));
    assert(near(changed_eval.clusters.back().opacity, 0.0));

    /* Runtime rendering must self-heal from the authoritative transition
     * descriptor even when an in-memory/older layer has no serialized managed
     * animator. The resolved stack still contains only generic TextAnimator
     * data and must produce a visible entrance transition immediately. */
    LayerTransition descriptor_only;
    descriptor_only.id = "runtime-self-heal";
    descriptor_only.preset_id = "text.fade";
    descriptor_only.display_name = "Fade Text";
    descriptor_only.kind = LayerTransitionKind::Text;
    descriptor_only.type = LayerTransitionType::TextFade;
    descriptor_only.edge = LayerTransitionEdge::In;
    descriptor_only.duration = 1.0;
    descriptor_only.stagger = 0.0;
    bool runtime_resolved_changed = false;
    const TextAnimatorStack runtime_resolved =
        resolved_text_transition_animator_stack(
            {descriptor_only}, TextAnimatorStack{}, 0.0, 3.0,
            &runtime_resolved_changed);
    assert(runtime_resolved_changed);
    assert(text_animator_stack_has_managed_transition(runtime_resolved));
    assert(runtime_resolved.animators.size() == 1);
    const TextAnimatorEvaluation runtime_hidden = evaluate_text_animators(
        runtime_resolved, layout, 0.0);
    const TextAnimatorEvaluation runtime_visible = evaluate_text_animators(
        runtime_resolved, layout, 1.0);
    assert(near(runtime_hidden.clusters.front().opacity, 0.0));
    assert(near(runtime_visible.clusters.front().opacity, 1.0));

    /* Every historical text-transition descriptor must independently resolve
     * into a time-dependent managed animator. This catches a renderer that
     * only repairs Fade while leaving the remaining catalog as static text. */
    const std::array<LayerTransitionType, 6> runtime_transition_types = {
        LayerTransitionType::TextFade,
        LayerTransitionType::TextSlide,
        LayerTransitionType::TextScale,
        LayerTransitionType::TextBlur,
        LayerTransitionType::TextWipe,
        LayerTransitionType::TextBlurSlide};
    for (size_t transition_index = 0;
         transition_index < runtime_transition_types.size();
         ++transition_index) {
        LayerTransition descriptor = descriptor_only;
        descriptor.id = "runtime-transition-" +
                        std::to_string(transition_index);
        descriptor.type = runtime_transition_types[transition_index];
        descriptor.direction = LayerTransitionDirection::Left;
        descriptor.offset = 72.0;
        descriptor.scale_from = 0.25;
        descriptor.blur_amount = 18.0;
        const TextAnimatorStack resolved =
            resolved_text_transition_animator_stack(
                {descriptor}, TextAnimatorStack{}, 0.0, 3.0);
        assert(text_animator_stack_has_managed_transition(resolved));
        assert(resolved.animators.size() == 1);
        assert(!resolved.animators.front().properties.empty());
        assert(!resolved.animators.front().selectors.empty());
        assert(text_animator_stack_is_time_dependent(resolved));
        const TextAnimatorEvaluation begin = evaluate_text_animators(
            resolved, layout, 0.0);
        const TextAnimatorEvaluation end = evaluate_text_animators(
            resolved, layout, 1.0);
        const TextAnimatorClusterState &begin_state = begin.clusters.front();
        const TextAnimatorClusterState &end_state = end.clusters.front();
        assert(near(end_state.opacity, 1.0));
        switch (descriptor.type) {
        case LayerTransitionType::TextSlide:
            assert(std::abs(begin_state.position_x) > 1.0);
            assert(near(end_state.position_x, 0.0));
            break;
        case LayerTransitionType::TextScale:
            assert(begin_state.scale_x < 0.9);
            assert(near(end_state.scale_x, 1.0));
            break;
        case LayerTransitionType::TextBlur:
            assert(begin_state.blur > 1.0);
            assert(near(end_state.blur, 0.0));
            break;
        case LayerTransitionType::TextWipe:
            assert(begin_state.reveal < 0.1);
            assert(end_state.reveal > 0.9);
            break;
        case LayerTransitionType::TextBlurSlide:
            assert(std::abs(begin_state.position_x) > 1.0);
            assert(begin_state.blur > 1.0);
            assert(near(end_state.position_x, 0.0));
            assert(near(end_state.blur, 0.0));
            break;
        case LayerTransitionType::TextFade:
            assert(near(begin_state.opacity, 0.0));
            break;
        default:
            assert(false);
        }
    }

    /* Static text remains cheap: 1,200 shaped clusters and ten animators are
     * evaluated without allocating a renderer pass per character. This is a
     * broad guardrail, not a machine-specific benchmark threshold. */
    TextLayoutData long_layout;
    long_layout.valid = true;
    long_layout.text.assign(1200, 'A');
    long_layout.clusters.reserve(1200);
    for (size_t i = 0; i < 1200; ++i)
        long_layout.clusters.push_back(cluster(i, 1, 0, 0));
    TextLayoutLine long_line;
    long_line.byte_start = 0;
    long_line.byte_length = 1200;
    long_line.cluster_begin = 0;
    long_line.cluster_count = 1200;
    long_layout.lines.push_back(long_line);
    TextLayoutRun long_run;
    long_run.byte_start = 0;
    long_run.byte_length = 1200;
    long_run.cluster_begin = 0;
    long_run.cluster_count = 1200;
    long_layout.runs.push_back(long_run);
    TextAnimatorStack stress;
    for (int i = 0; i < 10; ++i) {
        TextAnimator animator;
        animator.id = "stress-" + std::to_string(i);
        animator.properties.push_back(opacity_property(0.99));
        animator.selectors.push_back(all_selector());
        stress.animators.push_back(animator);
    }
    const auto started = std::chrono::steady_clock::now();
    const TextAnimatorEvaluation stress_result = evaluate_text_animators(
        stress, long_layout, 0.0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    assert(stress_result.clusters.size() == 1200);
    assert(elapsed.count() < 2000);

    const auto &migration_table = text_animator_legacy_preset_migration_table();
    assert(migration_table.size() == 6);
    for (const auto &record : migration_table) {
        assert(!record.legacy_preset_id.empty());
        assert(!record.properties.empty());

        const TextAnimator entrance = make_builtin_text_animator_preset(
            record.legacy_preset_id, 0.75, false, 2.0);
        assert(entrance.preset_id == record.legacy_preset_id);
        assert(entrance.playback_role == TextAnimatorPlaybackRole::Entrance);
        assert(entrance.properties.size() == record.properties.size());
        assert(entrance.selectors.size() == 1);
        assert(!entrance.selectors.front().completion.keyframes.empty());
        for (size_t i = 0; i < record.properties.size(); ++i)
            assert(entrance.properties[i].type == record.properties[i]);

        const TextAnimator exit = make_builtin_text_animator_preset(
            record.legacy_preset_id, 0.75, true, 2.0);
        assert(exit.preset_id == record.legacy_preset_id);
        assert(exit.playback_role == TextAnimatorPlaybackRole::Exit);
        assert(exit.properties.size() == record.properties.size());
        assert(exit.selectors.size() == 1);
        assert(near(exit.selectors.front().completion.keyframes.front().time, 1.25));
        assert(near(exit.selectors.front().completion.keyframes.back().time, 2.0));
    }

    std::cout << "text animator system tests passed in "
              << elapsed.count() << " ms stress time\n";
    return 0;
}
