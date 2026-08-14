#include "text-animator-presets.h"

#include <algorithm>
#include <cmath>

namespace {
uint64_t transition_hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename T>
uint64_t transition_hash_value(uint64_t hash, const T &value)
{
    return transition_hash_bytes(hash, &value, sizeof(value));
}

uint64_t transition_hash_string(uint64_t hash, const std::string &value)
{
    hash = transition_hash_value(hash, value.size());
    return value.empty() ? hash
                         : transition_hash_bytes(hash, value.data(), value.size());
}

uint64_t text_transition_binding_signature(const LayerTransition &transition,
                                            double layer_in_time,
                                            double layer_out_time)
{
    uint64_t hash = 1469598103934665603ULL;
    hash = transition_hash_string(hash, transition.id);
    hash = transition_hash_string(hash, transition.preset_id);
    hash = transition_hash_value(hash, transition.enabled);
    hash = transition_hash_value(hash, transition.type);
    hash = transition_hash_value(hash, transition.edge);
    hash = transition_hash_value(hash, transition.unit);
    hash = transition_hash_value(hash, transition.direction);
    hash = transition_hash_value(hash, transition.easing);
    hash = transition_hash_value(hash, transition.blur_amount);
    hash = transition_hash_value(hash, transition.scale_from);
    hash = transition_hash_value(hash, transition.offset);
    hash = transition_hash_value(hash, transition.stagger);
    hash = transition_hash_value(hash, transition.softness);
    hash = transition_hash_value(hash, transition.reverse_order);
    hash = transition_hash_value(hash, transition.text_slide_fade);
    hash = transition_hash_value(
        hash, transition.text_slide_crop_to_unit_bounds);
    const double layer_duration = std::max(1.0 / 240.0,
                                           layer_out_time - layer_in_time);
    const double effective_duration = std::clamp(
        transition.duration, 1.0 / 240.0, layer_duration);
    const double timeline_start = transition.edge == LayerTransitionEdge::Out
        ? std::max(0.0, layer_duration - effective_duration) : 0.0;
    hash = transition_hash_value(hash, effective_duration);
    return transition_hash_value(hash, timeline_start);
}

TextAnimatorUnit unit_from_legacy(LayerTransitionUnit unit)
{
    switch (unit) {
    case LayerTransitionUnit::Word: return TextAnimatorUnit::Word;
    case LayerTransitionUnit::Sentence: return TextAnimatorUnit::Sentence;
    case LayerTransitionUnit::Character:
    default: return TextAnimatorUnit::Grapheme;
    }
}

Keyframe key(double time, double value, EasingType easing)
{
    Keyframe result;
    result.time = time;
    result.value = value;
    result.easing = easing;
    result.temporal_mode = easing == EasingType::Hold
        ? TemporalInterpolationMode::Hold
        : easing == EasingType::Linear
            ? TemporalInterpolationMode::Linear
            : TemporalInterpolationMode::AutoBezier;
    return result;
}

TextAnimatorProperty property(TextAnimatorPropertyType type,
                              const std::string &name,
                              double a, double b = 0.0,
                              double c = 0.0, double d = 0.0)
{
    TextAnimatorProperty result;
    result.type = type;
    result.name = name;
    result.id = make_text_animator_id("property", name, static_cast<size_t>(type));
    result.value.name = name + ".value";
    result.secondary.name = name + ".secondary";
    result.tertiary.name = name + ".tertiary";
    result.quaternary.name = name + ".quaternary";
    result.value.static_value = a;
    result.secondary.static_value = b;
    result.tertiary.static_value = c;
    result.quaternary.static_value = d;
    return result;
}

TextSelector progressive_selector(const std::string &seed,
                                  TextAnimatorUnit unit,
                                  double timeline_start,
                                  double duration,
                                  double stagger,
                                  EasingType easing,
                                  bool exit_animation,
                                  bool reverse)
{
    TextSelector selector;
    selector.id = make_text_animator_id("selector", seed, 0);
    selector.name = "Staggered Transition Selector";
    selector.type = TextSelectorType::Staggered;
    selector.combination = TextSelectorCombinationMode::Intersect;
    selector.based_on = unit;
    selector.direction = reverse ? TextSelectorDirection::Reverse
                                 : TextSelectorDirection::Forward;
    selector.randomize_order = false;
    selector.exclude_whitespace = true;
    selector.invert = false;
    selector.amount.static_value = 100.0;
    selector.stagger_percent.static_value =
        std::clamp(stagger * 100.0, 0.0, 95.0);
    selector.unit_easing = easing;
    selector.stagger_mode = exit_animation
        ? TextStaggerMode::Exit : TextStaggerMode::Entrance;

    const double safe_duration = std::max(1.0 / 240.0, duration);
    selector.completion.static_value = 0.0;
    /* Completion is only the shared timeline clock. Easing belongs to the
     * local phase calculated after each Animate By unit's stagger delay. */
    selector.completion.keyframes = {
        key(timeline_start, 0.0, EasingType::Linear),
        key(timeline_start + safe_duration, 100.0, EasingType::Linear)};
    return selector;
}

void add_transition_properties(TextAnimator &animator,
                               const LayerTransition &transition)
{
    switch (transition.type) {
    case LayerTransitionType::TextFade:
        animator.properties.push_back(property(TextAnimatorPropertyType::Opacity,
                                               "Opacity", 0.0));
        break;
    case LayerTransitionType::TextSlide: {
        double x = 0.0, y = 0.0;
        switch (transition.direction) {
        case LayerTransitionDirection::Right: x = transition.offset; break;
        case LayerTransitionDirection::Up: y = -transition.offset; break;
        case LayerTransitionDirection::Down: y = transition.offset; break;
        case LayerTransitionDirection::Left:
        case LayerTransitionDirection::None:
        default: x = -transition.offset; break;
        }
        animator.properties.push_back(property(TextAnimatorPropertyType::Position,
                                               "Position", x, y));
        if (transition.text_slide_fade) {
            animator.properties.push_back(property(
                TextAnimatorPropertyType::Opacity, "Opacity", 0.0));
        }
        break;
    }
    case LayerTransitionType::TextScale:
        animator.properties.push_back(property(TextAnimatorPropertyType::Scale,
                                               "Scale", transition.scale_from,
                                               transition.scale_from));
        animator.properties.push_back(property(TextAnimatorPropertyType::Opacity,
                                               "Opacity", 0.0));
        break;
    case LayerTransitionType::TextBlur:
        animator.properties.push_back(property(TextAnimatorPropertyType::Blur,
                                               "Blur", transition.blur_amount));
        animator.properties.push_back(property(TextAnimatorPropertyType::Opacity,
                                               "Opacity", 0.0));
        break;
    case LayerTransitionType::TextWipe: {
        const LayerTransitionDirection direction =
            transition.direction == LayerTransitionDirection::None
                ? LayerTransitionDirection::Left : transition.direction;
        animator.properties.push_back(property(
            TextAnimatorPropertyType::CharacterReveal,
            "Character Reveal", 0.0, static_cast<double>(direction)));
        /* The legacy transition faded the isolated unit while clipping it. */
        animator.properties.push_back(property(TextAnimatorPropertyType::Opacity,
                                               "Opacity", 0.0));
        break;
    }
    case LayerTransitionType::TextBlurSlide: {
        double x = 0.0, y = 0.0;
        switch (transition.direction) {
        case LayerTransitionDirection::Right: x = transition.offset; break;
        case LayerTransitionDirection::Up: y = -transition.offset; break;
        case LayerTransitionDirection::Down: y = transition.offset; break;
        case LayerTransitionDirection::Left:
        case LayerTransitionDirection::None:
        default: x = -transition.offset; break;
        }
        animator.properties.push_back(property(TextAnimatorPropertyType::Position,
                                               "Position", x, y));
        animator.properties.push_back(property(TextAnimatorPropertyType::Blur,
                                               "Blur", transition.blur_amount));
        if (transition.text_slide_fade) {
            animator.properties.push_back(property(
                TextAnimatorPropertyType::Opacity, "Opacity", 0.0));
        }
        break;
    }
    default:
        break;
    }
}

std::string legacy_type_name(LayerTransitionType type)
{
    switch (type) {
    case LayerTransitionType::TextFade: return "text-fade";
    case LayerTransitionType::TextSlide: return "text-slide";
    case LayerTransitionType::TextScale: return "text-scale";
    case LayerTransitionType::TextBlur: return "text-blur";
    case LayerTransitionType::TextWipe: return "text-wipe";
    case LayerTransitionType::TextBlurSlide: return "text-blur-slide";
    default: return "unsupported";
    }
}

bool legacy_transition_type_from_preset(const std::string &preset_id,
                                        LayerTransitionType &type)
{
    if (preset_id == "text.fade") type = LayerTransitionType::TextFade;
    else if (preset_id == "text.slide-in") type = LayerTransitionType::TextSlide;
    else if (preset_id == "text.scale") type = LayerTransitionType::TextScale;
    else if (preset_id == "text.blur") type = LayerTransitionType::TextBlur;
    else if (preset_id == "text.wipe") type = LayerTransitionType::TextWipe;
    else if (preset_id == "text.blur-slide-in") type = LayerTransitionType::TextBlurSlide;
    else return false;
    return true;
}

LayerTransitionUnit legacy_unit_from_animator(TextAnimatorUnit unit)
{
    switch (unit) {
    case TextAnimatorUnit::Word: return LayerTransitionUnit::Word;
    case TextAnimatorUnit::Sentence:
    case TextAnimatorUnit::Line:
    case TextAnimatorUnit::Paragraph:
        return LayerTransitionUnit::Sentence;
    default:
        return LayerTransitionUnit::Character;
    }
}

const TextAnimatorProperty *find_property(
    const TextAnimator &animator, TextAnimatorPropertyType type)
{
    const auto found = std::find_if(
        animator.properties.begin(), animator.properties.end(),
        [type](const TextAnimatorProperty &property) {
            return property.type == type;
        });
    return found == animator.properties.end() ? nullptr : &*found;
}

LayerTransition recovered_transition_from_v1_animator(
    const TextAnimator &animator, double layer_in_time,
    double layer_out_time, std::string *warning)
{
    LayerTransition transition;
    transition.id = animator.id + "-transition";
    transition.preset_id = animator.preset_id;
    transition.display_name = animator.name;
    transition.enabled = animator.enabled;
    transition.kind = LayerTransitionKind::Text;
    legacy_transition_type_from_preset(animator.preset_id, transition.type);
    transition.edge = animator.playback_role == TextAnimatorPlaybackRole::Exit
        ? LayerTransitionEdge::Out : LayerTransitionEdge::In;
    transition.unit = legacy_unit_from_animator(animator.granularity);

    const TextSelector *selector = animator.selectors.empty()
        ? nullptr : &animator.selectors.front();
    if (selector) {
        transition.reverse_order =
            selector->direction == TextSelectorDirection::Reverse;
        const bool staggered =
            selector->type == TextSelectorType::Staggered;
        transition.stagger = staggered
            ? std::clamp(
                  selector->stagger_percent.static_value / 100.0, 0.0, 0.95)
            : std::clamp(
                  1.0 - selector->smoothness.static_value / 100.0, 0.0, 0.95);
        const AnimatedProperty &track = staggered
            ? selector->completion
            : (transition.edge == LayerTransitionEdge::Out
                   ? selector->end : selector->start);
        if (track.keyframes.size() >= 2) {
            transition.duration = std::max(
                1.0 / 240.0,
                track.keyframes.back().time - track.keyframes.front().time);
            transition.easing = staggered
                ? selector->unit_easing
                : track.keyframes.front().easing;
        }
    }
    const double layer_duration =
        std::max(1.0 / 240.0, layer_out_time - layer_in_time);
    transition.duration = std::clamp(
        transition.duration, 1.0 / 240.0, layer_duration);

    if (const TextAnimatorProperty *position =
            find_property(animator, TextAnimatorPropertyType::Position)) {
        const double x = position->value.static_value;
        const double y = position->secondary.static_value;
        transition.offset = std::max(std::abs(x), std::abs(y));
        if (std::abs(y) > std::abs(x))
            transition.direction = y < 0.0
                ? LayerTransitionDirection::Up
                : LayerTransitionDirection::Down;
        else
            transition.direction = x > 0.0
                ? LayerTransitionDirection::Right
                : LayerTransitionDirection::Left;
    }
    if (const TextAnimatorProperty *scale =
            find_property(animator, TextAnimatorPropertyType::Scale))
        transition.scale_from = scale->value.static_value;
    if (const TextAnimatorProperty *blur =
            find_property(animator, TextAnimatorPropertyType::Blur))
        transition.blur_amount = std::max(0.0, blur->value.static_value);
    if (transition.type == LayerTransitionType::TextSlide ||
        transition.type == LayerTransitionType::TextBlurSlide) {
        transition.text_slide_fade =
            find_property(animator, TextAnimatorPropertyType::Opacity) != nullptr;
        transition.text_slide_crop_to_unit_bounds =
            animator.clip_to_unit_bounds;
    }
    if (transition.type == LayerTransitionType::TextWipe) {
        transition.direction = LayerTransitionDirection::Left;
        if (warning) {
            *warning = "project contains a v134/v135 text.wipe animator without "
                       "a retained direction field; fallback=Left.";
        }
    }
    return transition;
}
} // namespace

TextAnimator make_text_animator_from_legacy_transition(
    const LayerTransition &transition, double layer_in_time,
    double layer_out_time, std::string *warning)
{
    TextAnimator animator;
    animator.id = make_text_animator_id(
        "animator", transition.id.empty() ? transition.preset_id : transition.id,
        static_cast<size_t>(transition.edge));
    animator.name = transition.display_name.empty() ? "Migrated Text Transition"
                                                     : transition.display_name;
    animator.enabled = transition.enabled;
    animator.expanded = true;
    animator.blend_mode = TextAnimatorBlendMode::Add;
    animator.granularity = unit_from_legacy(transition.unit);
    animator.transform_as_unit = true;
    animator.clip_to_unit_bounds =
        transition.text_slide_crop_to_unit_bounds &&
        (transition.type == LayerTransitionType::TextSlide ||
         transition.type == LayerTransitionType::TextBlurSlide);
    animator.change_behaviour = TextChangeBehaviour::ReevaluateFullText;
    animator.playback_role = transition.edge == LayerTransitionEdge::Out
        ? TextAnimatorPlaybackRole::Exit : TextAnimatorPlaybackRole::Entrance;
    animator.preset_id = transition.preset_id;
    animator.preset_schema_version = kTextAnimatorSchemaVersion;
    animator.transition_managed = !transition.id.empty();
    animator.transition_id = transition.id;
    animator.transition_binding_signature = text_transition_binding_signature(
        transition, layer_in_time, layer_out_time);

    const double layer_duration = std::max(1.0 / 240.0, layer_out_time - layer_in_time);
    const double duration = std::clamp(transition.duration, 1.0 / 240.0, layer_duration);
    /* Keep animator keyframes in the same layer-local timeline domain used by
     * every existing FXM property. This makes the generated selector tracks
     * immediately editable by the shared timeline without a hidden time warp. */
    animator.local_time_offset = 0.0;
    const double timeline_start = transition.edge == LayerTransitionEdge::Out
        ? std::max(0.0, layer_duration - duration)
        : 0.0;
    animator.selectors.push_back(progressive_selector(
        transition.preset_id.empty() ? legacy_type_name(transition.type)
                                     : transition.preset_id,
        animator.granularity, timeline_start, duration, transition.stagger,
        transition.easing, transition.edge == LayerTransitionEdge::Out,
        transition.reverse_order));
    add_transition_properties(animator, transition);

    if (warning)
        warning->clear();
    return animator;
}

TextAnimator *find_managed_text_transition_animator(
    TextAnimatorStack &stack, const std::string &transition_id)
{
    auto found = std::find_if(stack.animators.begin(), stack.animators.end(),
        [&](const TextAnimator &animator) {
            return animator.transition_managed &&
                   animator.transition_id == transition_id;
        });
    return found == stack.animators.end() ? nullptr : &*found;
}

const TextAnimator *find_managed_text_transition_animator(
    const TextAnimatorStack &stack, const std::string &transition_id)
{
    auto found = std::find_if(stack.animators.begin(), stack.animators.end(),
        [&](const TextAnimator &animator) {
            return animator.transition_managed &&
                   animator.transition_id == transition_id;
        });
    return found == stack.animators.end() ? nullptr : &*found;
}

bool synchronize_text_transition_animators(
    const std::vector<LayerTransition> &transitions,
    TextAnimatorStack &stack,
    double layer_in_time,
    double layer_out_time,
    std::vector<std::string> *warnings,
    bool rebuild_existing)
{
    bool changed = false;
    std::vector<std::string> live_ids;
    for (const LayerTransition &transition : transitions) {
        if (!layer_transition_type_is_text(transition.type))
            continue;
        live_ids.push_back(transition.id);
        TextAnimator *existing = find_managed_text_transition_animator(
            stack, transition.id);
        const uint64_t expected_signature =
            text_transition_binding_signature(
                transition, layer_in_time, layer_out_time);
        /* Rebuild only when the authored descriptor or the layer-local timing
         * changed. This keeps generated animators fully editable: arbitrary
         * property/selector/keyframe edits survive unrelated refreshes. The
         * legacy rebuild flag remains as a migration hint for unversioned
         * bindings, not as permission to overwrite current editable data. */
        if (existing &&
            existing->transition_binding_signature == expected_signature &&
            (!rebuild_existing ||
             existing->transition_binding_signature != 0))
            continue;

        std::string warning;
        TextAnimator replacement = make_text_animator_from_legacy_transition(
            transition, layer_in_time, layer_out_time, &warning);
        if (existing)
            *existing = std::move(replacement);
        else
            stack.animators.push_back(std::move(replacement));
        if (!warning.empty() && warnings)
            warnings->push_back(warning);
        changed = true;
    }

    const auto old_size = stack.animators.size();
    stack.animators.erase(std::remove_if(stack.animators.begin(),
        stack.animators.end(), [&](const TextAnimator &animator) {
            return animator.transition_managed &&
                std::find(live_ids.begin(), live_ids.end(),
                          animator.transition_id) == live_ids.end();
        }), stack.animators.end());
    changed = changed || stack.animators.size() != old_size;

    if (changed || !live_ids.empty()) {
        stack.schema_version = kTextAnimatorSchemaVersion;
        stack.legacy_migration_version =
            kTextAnimatorLegacyMigrationVersion;
    }
    return changed;
}

bool migrate_legacy_text_transitions(
    std::vector<LayerTransition> &transitions,
    TextAnimatorStack &stack,
    double layer_in_time,
    double layer_out_time,
    std::vector<std::string> *warnings)
{
    bool has_text_transition = false;
    for (const LayerTransition &transition : transitions)
        has_text_transition = has_text_transition ||
                              layer_transition_type_is_text(transition.type);

    /* Development versions 134/135 converted presets to generic animators but
     * removed the timeline descriptor. Recover it once from the concrete
     * generated data before upgrading the selector schema. */
    bool recovered_descriptor = false;
    if (!has_text_transition &&
        (stack.schema_version < kTextAnimatorSchemaVersion ||
         stack.legacy_migration_version <
             kTextAnimatorLegacyMigrationVersion)) {
        for (TextAnimator &animator : stack.animators) {
            LayerTransitionType ignored = LayerTransitionType::TextFade;
            if (animator.transition_managed ||
                (animator.playback_role != TextAnimatorPlaybackRole::Entrance &&
                 animator.playback_role != TextAnimatorPlaybackRole::Exit) ||
                !legacy_transition_type_from_preset(animator.preset_id, ignored))
                continue;
            std::string warning;
            LayerTransition recovered = recovered_transition_from_v1_animator(
                animator, layer_in_time, layer_out_time, &warning);
            /* FXM permits one authored transition per edge. Development
             * versions 134/135 could leave an older general descriptor behind
             * after deleting the text descriptor, so restore the concrete text
             * transition by replacing that edge instead of creating two
             * competing timeline handles. */
            const std::string recovered_id = recovered.id;
            if (LayerTransition *existing = find_layer_transition(
                    transitions, recovered.edge))
                *existing = std::move(recovered);
            else
                transitions.push_back(std::move(recovered));
            /* Bind the concrete v1 animator before rebuilding so the upgrade
             * replaces it in place instead of leaving an unmanaged duplicate
             * beside the v2 transition animator. */
            animator.transition_managed = true;
            animator.transition_id = recovered_id;
            if (!warning.empty() && warnings)
                warnings->push_back(warning);
            recovered_descriptor = true;
        }
        has_text_transition = recovered_descriptor;
    }
    if (!has_text_transition)
        return false;

    /* v134/v135 generated deterministic animators and removed the authoring
     * descriptor. When a descriptor is still present, replace any matching
     * pre-v2 generated animator with an explicitly bound v2 animator. The
     * descriptor itself is intentionally retained for timeline handles and the
     * transition editor; runtime rendering never executes it. */
    const bool requires_rebuild =
        stack.schema_version < kTextAnimatorSchemaVersion ||
        stack.legacy_migration_version <
            kTextAnimatorLegacyMigrationVersion;
    for (const LayerTransition &transition : transitions) {
        if (!layer_transition_type_is_text(transition.type))
            continue;
        const std::string old_id = make_text_animator_id(
            "animator", transition.id.empty() ? transition.preset_id
                                               : transition.id,
            static_cast<size_t>(transition.edge));
        for (TextAnimator &animator : stack.animators) {
            if (!animator.transition_managed && animator.id == old_id) {
                animator.transition_managed = true;
                animator.transition_id = transition.id;
            }
        }
    }
    const bool changed = synchronize_text_transition_animators(
        transitions, stack, layer_in_time, layer_out_time,
        warnings, requires_rebuild);
    stack.schema_version = kTextAnimatorSchemaVersion;
    stack.legacy_migration_version =
        kTextAnimatorLegacyMigrationVersion;
    return changed || requires_rebuild || recovered_descriptor;
}

TextAnimatorStack resolved_text_transition_animator_stack(
    const std::vector<LayerTransition> &transitions,
    const TextAnimatorStack &stack,
    double layer_in_time,
    double layer_out_time,
    bool *changed,
    std::vector<std::string> *warnings)
{
    TextAnimatorStack resolved = stack;
    const bool synchronized = synchronize_text_transition_animators(
        transitions, resolved, layer_in_time, layer_out_time,
        warnings, true);
    if (changed)
        *changed = synchronized;
    return resolved;
}

bool text_animator_stack_has_managed_transition(
    const TextAnimatorStack &stack)
{
    return std::any_of(stack.animators.begin(), stack.animators.end(),
        [](const TextAnimator &animator) {
            return animator.enabled && animator.transition_managed &&
                   !animator.properties.empty();
        });
}

const std::vector<TextAnimatorPresetMigrationRecord> &
text_animator_legacy_preset_migration_table()
{
    static const std::vector<TextAnimatorPresetMigrationRecord> table = {
        {"text.fade", "text-fade", "Fade Text", {TextAnimatorPropertyType::Opacity},
         TextAnimatorUnit::Grapheme, TextAnimatorPlaybackRole::Entrance,
         "Opacity 0 with progressive Range Selector."},
        {"text.slide-in", "text-slide", "Slide In/Out",
         {TextAnimatorPropertyType::Position, TextAnimatorPropertyType::Opacity},
         TextAnimatorUnit::Grapheme, TextAnimatorPlaybackRole::Entrance,
         "Position offset and opacity use the same selector."},
        {"text.scale", "text-scale", "Scale Text",
         {TextAnimatorPropertyType::Scale, TextAnimatorPropertyType::Opacity},
         TextAnimatorUnit::Grapheme, TextAnimatorPlaybackRole::Entrance,
         "Scale-from value is preserved."},
        {"text.blur", "text-blur", "Blur Text",
         {TextAnimatorPropertyType::Blur, TextAnimatorPropertyType::Opacity},
         TextAnimatorUnit::Grapheme, TextAnimatorPlaybackRole::Entrance,
         "Blur amount is preserved and evaluated per cluster."},
        {"text.wipe", "text-wipe", "Wipe Text",
         {TextAnimatorPropertyType::CharacterReveal,
          TextAnimatorPropertyType::Opacity},
         TextAnimatorUnit::Grapheme, TextAnimatorPlaybackRole::Entrance,
         "Directional unit clipping and opacity use the shared glyph compositor."},
        {"text.blur-slide-in", "text-blur-slide", "Blur Slide In/Out",
         {TextAnimatorPropertyType::Position, TextAnimatorPropertyType::Blur,
          TextAnimatorPropertyType::Opacity},
         TextAnimatorUnit::Grapheme, TextAnimatorPlaybackRole::Entrance,
         "Position, blur and opacity share one progressive selector."},
    };
    return table;
}

TextAnimator make_builtin_text_animator_preset(const std::string &preset_id,
                                               double duration,
                                               bool exit_animation,
                                               double layer_duration)
{
    LayerTransition transition;
    transition.id = preset_id;
    transition.preset_id = preset_id;
    transition.display_name = preset_id;
    transition.duration = std::max(1.0 / 240.0, duration);
    transition.edge = exit_animation ? LayerTransitionEdge::Out
                                     : LayerTransitionEdge::In;
    transition.kind = LayerTransitionKind::Text;
    if (preset_id.find("slide") != std::string::npos)
        transition.type = preset_id.find("blur") != std::string::npos
            ? LayerTransitionType::TextBlurSlide : LayerTransitionType::TextSlide;
    else if (preset_id.find("scale") != std::string::npos)
        transition.type = LayerTransitionType::TextScale;
    else if (preset_id.find("blur") != std::string::npos)
        transition.type = LayerTransitionType::TextBlur;
    else if (preset_id.find("wipe") != std::string::npos ||
             preset_id.find("typewriter") != std::string::npos ||
             preset_id.find("reveal") != std::string::npos)
        transition.type = LayerTransitionType::TextWipe;
    else
        transition.type = LayerTransitionType::TextFade;
    const double timeline_duration = std::max(transition.duration, layer_duration);
    TextAnimator animator = make_text_animator_from_legacy_transition(
        transition, 0.0, timeline_duration, nullptr);
    animator.transition_managed = false;
    animator.transition_id.clear();
    animator.transition_binding_signature = 0;
    return animator;
}
