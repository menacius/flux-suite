#pragma once

#include "title-data.h"

#include <algorithm>
#include <memory>
#include <utility>

/* Reloading authored data is not a transport command. In particular, keep the
 * cue counter unchanged so OBS does not replay an intro/outro after a save. */
inline void copy_title_cue_runtime_state(Title &target, const Title &source)
{
    target.current_cue_row = source.current_cue_row;
    target.pending_cue_row = source.pending_cue_row;
    target.last_cue_row = source.last_cue_row;
    target.cue_uncue_requested = source.cue_uncue_requested;
    target.cue_revision = source.cue_revision;
    target.playlist_active = source.playlist_active;
    target.playlist_next_row = source.playlist_next_row;
    target.playlist_next_due_ms = source.playlist_next_due_ms;
    target.playlist_stop_after_due = source.playlist_stop_after_due;
    target.cue_background_persistence = source.cue_background_persistence;
    target.cue_text_persistence = source.cue_text_persistence;
    target.cue_persistence_transition = source.cue_persistence_transition;
    target.cue_persistent_text_columns = source.cue_persistent_text_columns;
}

/* Title contains shared layer pointers. Rendering/cache jobs require an
 * immutable deep snapshot so UI edits cannot mutate a frame while it is being
 * rendered. Keep this copy contract in one place for cache workers, GPU
 * presentation sessions, editor duplication, and undo/redo. */
inline Title clone_title_snapshot(const Title &title)
{
    Title snapshot = title;
    snapshot.layers.clear();
    snapshot.layers.reserve(title.layers.size());
    for (const auto &layer : title.layers) {
        if (layer)
            snapshot.layers.push_back(std::make_shared<Layer>(*layer));
    }
    return snapshot;
}

inline std::shared_ptr<Title> clone_title_snapshot(
    const std::shared_ptr<Title> &title)
{
    return title ? std::make_shared<Title>(clone_title_snapshot(*title))
                 : nullptr;
}

inline bool title_cue_runtime_equal(const Title &a, const Title &b)
{
    return a.current_cue_row == b.current_cue_row &&
        a.pending_cue_row == b.pending_cue_row && a.last_cue_row == b.last_cue_row &&
        a.cue_uncue_requested == b.cue_uncue_requested && a.cue_revision == b.cue_revision &&
        a.playlist_active == b.playlist_active && a.playlist_next_row == b.playlist_next_row &&
        a.playlist_next_due_ms == b.playlist_next_due_ms &&
        a.playlist_stop_after_due == b.playlist_stop_after_due &&
        a.cue_background_persistence == b.cue_background_persistence &&
        a.cue_text_persistence == b.cue_text_persistence &&
        a.cue_persistence_transition == b.cue_persistence_transition &&
        a.cue_persistent_text_columns == b.cue_persistent_text_columns;
}

/* Program owns the authored document sent by the last cue. Runtime commands
 * remain live, but saves and even an uncue must not replace outgoing artwork. */
struct TitleProgramSnapshot {
    std::shared_ptr<Title> title;
    std::string authored_identity;
    uint64_t command_revision = 0;
    bool pending_changes = false;

    bool update(const std::shared_ptr<Title> &authored, const std::string &identity,
                bool on_air = true)
    {
        if (!authored) return false;
        const bool new_cue = authored->cue_revision != command_revision &&
            !authored->cue_uncue_requested &&
            (authored->current_cue_row >= 0 || authored->pending_cue_row >= 0);
        // A source can remain in the Program scene while its title is uncued.
        // Edits made in that state are ordinary authoring updates, not changes
        // withheld from an on-air cue. Keep a previously pending outgoing
        // snapshot through its uncue/end frame until the next positive cue.
        const bool uncued_edit = !on_air && !pending_changes &&
            identity != authored_identity;
        const bool capture = !title || title->id != authored->id || new_cue || uncued_edit;
        if (capture) {
            title = clone_title_snapshot(authored);
            authored_identity = identity;
        }
        command_revision = authored->cue_revision;
        pending_changes = identity != authored_identity;
        copy_title_cue_runtime_state(*title, *authored);
        return capture;
    }
};

/* Restore the complete authored document instead of maintaining an error-prone
 * field-by-field undo list. Runtime source/cue state and advisory proxy state
 * belong to the currently open title instance, not to an historical editor
 * snapshot, so they are retained explicitly. This guarantees that newly added
 * title-level features (notably cameras and camera-switch tracks) become
 * undoable automatically. */
inline void restore_title_authoring_snapshot(Title &target,
                                             const Title &snapshot)
{
    const std::string identity = target.id;
    const std::string render_camera_override = target.render_camera_override_id;
    const TitleProxyMetadata proxy_metadata = target.proxy_metadata;

    const int current_cue_row = target.current_cue_row;
    const int pending_cue_row = target.pending_cue_row;
    const int last_cue_row = target.last_cue_row;
    const bool cue_uncue_requested = target.cue_uncue_requested;
    const uint64_t cue_revision = target.cue_revision;
    const bool playlist_active = target.playlist_active;
    const int playlist_next_row = target.playlist_next_row;
    const int64_t playlist_next_due_ms = target.playlist_next_due_ms;
    const bool playlist_stop_after_due = target.playlist_stop_after_due;
    const bool cue_background_persistence = target.cue_background_persistence;
    const bool cue_text_persistence = target.cue_text_persistence;
    const bool cue_persistence_transition = target.cue_persistence_transition;
    const std::vector<bool> cue_persistent_text_columns =
        target.cue_persistent_text_columns;

    target = clone_title_snapshot(snapshot);
    target.id = identity;
    target.render_camera_override_id = render_camera_override;
    target.proxy_metadata = proxy_metadata;

    const int cue_row_count = static_cast<int>(target.live_text_rows.size());
    auto valid_runtime_row = [cue_row_count](int row) {
        return row >= 0 && row < cue_row_count ? row : -1;
    };
    target.current_cue_row = valid_runtime_row(current_cue_row);
    target.pending_cue_row = valid_runtime_row(pending_cue_row);
    target.last_cue_row = valid_runtime_row(last_cue_row);
    target.cue_uncue_requested = cue_uncue_requested &&
        target.current_cue_row >= 0;
    target.cue_revision = cue_revision + 1;
    target.playlist_active = playlist_active && cue_row_count > 0;
    target.playlist_next_row = cue_row_count > 0
        ? std::clamp(playlist_next_row, 0, cue_row_count - 1) : 0;
    target.playlist_next_due_ms = playlist_next_due_ms;
    target.playlist_stop_after_due = playlist_stop_after_due;
    target.cue_background_persistence = cue_background_persistence;
    target.cue_text_persistence = cue_text_persistence;
    target.cue_persistence_transition = cue_persistence_transition &&
        target.current_cue_row >= 0;
    target.cue_persistent_text_columns = cue_persistent_text_columns;
    size_t cue_column_count = target.live_text_column_order.size();
    if (cue_column_count == 0 && !target.live_text_rows.empty())
        cue_column_count = target.live_text_rows.front().size();
    target.cue_persistent_text_columns.resize(cue_column_count, false);
    if (target.current_cue_row < 0)
        target.cue_persistent_text_columns.clear();
}
