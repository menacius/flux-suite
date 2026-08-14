#pragma once

#include "text-animator.h"
#include "layer-transition.h"

#include <string>
#include <vector>

struct TextAnimatorPresetMigrationRecord {
    std::string legacy_preset_id;
    std::string legacy_type;
    std::string new_animator_name;
    std::vector<TextAnimatorPropertyType> properties;
    TextAnimatorUnit selector_unit = TextAnimatorUnit::Grapheme;
    TextAnimatorPlaybackRole playback_role = TextAnimatorPlaybackRole::General;
    std::string notes;
};

TextAnimator make_text_animator_from_legacy_transition(
    const LayerTransition &transition, double layer_in_time,
    double layer_out_time, std::string *warning = nullptr);

bool migrate_legacy_text_transitions(
    std::vector<LayerTransition> &transitions,
    TextAnimatorStack &stack,
    double layer_in_time,
    double layer_out_time,
    std::vector<std::string> *warnings = nullptr);

/* Keeps timeline/transition-editor metadata bound to the generic runtime
 * animator. This is the only supported runtime path for text transitions. */
bool synchronize_text_transition_animators(
    const std::vector<LayerTransition> &transitions,
    TextAnimatorStack &stack,
    double layer_in_time,
    double layer_out_time,
    std::vector<std::string> *warnings = nullptr,
    bool rebuild_existing = true);

TextAnimator *find_managed_text_transition_animator(
    TextAnimatorStack &stack, const std::string &transition_id);
const TextAnimator *find_managed_text_transition_animator(
    const TextAnimatorStack &stack, const std::string &transition_id);


/* Build the runtime TextAnimator stack from the serialized editable stack and
 * the authoritative transition descriptors. This is intentionally a pure
 * conversion layer: rendering still consumes only TextAnimator structures.
 * It makes editor/source rendering self-healing when an older/in-memory title
 * contains text-transition descriptors but its managed animator binding is
 * missing or stale. */
TextAnimatorStack resolved_text_transition_animator_stack(
    const std::vector<LayerTransition> &transitions,
    const TextAnimatorStack &stack,
    double layer_in_time,
    double layer_out_time,
    bool *changed = nullptr,
    std::vector<std::string> *warnings = nullptr);

bool text_animator_stack_has_managed_transition(
    const TextAnimatorStack &stack);

const std::vector<TextAnimatorPresetMigrationRecord> &
text_animator_legacy_preset_migration_table();

/* New preset files store this generic configuration directly. */
TextAnimator make_builtin_text_animator_preset(const std::string &preset_id,
                                               double duration,
                                               bool exit_animation = false,
                                               double layer_duration = 0.0);
