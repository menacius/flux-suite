#pragma once

#include "title-data.h"

#include <algorithm>
#include <set>
#include <string>

/* Core-only tests intentionally do not link the editor-heavy title-data.cpp.
 * Keep the small normalization dependency shared between those tests. */
inline void prune_live_text_cue_style_overrides(Title &title)
{
    ensure_live_text_row_ids(title);
    const std::set<std::string> valid_rows(title.live_text_row_ids.begin(),
                                           title.live_text_row_ids.end());
    std::set<std::string> valid_layers;
    for (const auto &layer : title.layers) {
        if (layer && (layer->expose_fill_color || layer->expose_stroke_color))
            valid_layers.insert(layer->id);
    }
    title.live_text_cue_style_overrides.erase(
        std::remove_if(title.live_text_cue_style_overrides.begin(),
                       title.live_text_cue_style_overrides.end(),
                       [&](const LiveTextCueStyleOverride &entry) {
                           return entry.row_id.empty() || entry.layer_id.empty() ||
                                  valid_rows.find(entry.row_id) == valid_rows.end() ||
                                  valid_layers.find(entry.layer_id) == valid_layers.end() ||
                                  (!entry.fill_color_set &&
                                   !entry.stroke_color_set);
                       }),
        title.live_text_cue_style_overrides.end());
}
