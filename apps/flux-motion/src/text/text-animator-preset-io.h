#pragma once

#include "text-animator.h"

#include <string>

struct TextAnimatorPresetMetadata {
    std::string name;
    std::string category = "Custom";
    std::string description;
    std::string identifier;
    int schema_version = kTextAnimatorSchemaVersion;
};

/* Standalone presets store only generic Text Animator model data. No renderer
 * commands, legacy transition state, thumbnails, or cached frames are stored. */
bool save_text_animator_preset_file(const std::string &path,
                                    const TextAnimatorPresetMetadata &metadata,
                                    const TextAnimator &animator,
                                    std::string *error = nullptr);

bool load_text_animator_preset_file(const std::string &path,
                                    TextAnimatorPresetMetadata *metadata,
                                    TextAnimator *animator,
                                    std::string *error = nullptr);

/* Imported/duplicated presets receive fresh stable IDs so timeline keys cannot
 * collide with structures already present on the destination layer. */
void reseed_text_animator_ids(TextAnimator &animator,
                              const std::string &destination_layer_id,
                              size_t animator_ordinal);
