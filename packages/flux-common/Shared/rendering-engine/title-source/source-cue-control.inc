// Source-local transport. An outgoing Duplicate Source must not uncue its
// counterpart in the incoming scene. Only the video tick mutates playback.
bool title_source_cue(obs_source_t *source, int row)
{
    if (row < -1 || row == std::numeric_limits<int>::max()) return false;
    std::lock_guard<std::mutex> lock(g_title_source_instances_mutex);
    const auto it = g_title_source_instances.find(source);
    if (it == g_title_source_instances.end() || it->second->editor_transport_controlled)
        return false;
    it->second->external_cue_command.store(row < 0 ? -2 : row + 1);
    return true;
}

bool title_source_uncue(obs_source_t *source)
{
    std::lock_guard<std::mutex> lock(g_title_source_instances_mutex);
    const auto it = g_title_source_instances.find(source);
    if (it == g_title_source_instances.end() || it->second->editor_transport_controlled)
        return false;
    it->second->external_cue_command.store(-1);
    return true;
}

TitleSourceCueStatus title_source_cue_status(obs_source_t *source)
{
    std::lock_guard<std::mutex> lock(g_title_source_instances_mutex);
    const auto it = g_title_source_instances.find(source);
    if (it == g_title_source_instances.end() || it->second->editor_transport_controlled)
        return {};
    auto *data = it->second;
    return {true, data->external_cue_active.load(), data->external_cue_ending.load(),
            (data->external_cue_command.load() != 0 || data->external_command_in_flight.load()), data->external_cue_remaining.load()};
}

static std::shared_ptr<Title> source_external_cue_control(
    TitleSourceData *data, const std::shared_ptr<Title> &authored)
{
    if (!authored || data->editor_transport_controlled) return authored;
    data->external_command_in_flight.store(true);
    const int command = data->external_cue_command.exchange(0);
    if (!command) data->external_command_in_flight.store(false);
    if (data->local_cue_control &&
        (data->local_cue_control->id != authored->id ||
         data->local_cue_authored_revision != authored->cue_revision))
    {
        data->local_cue_control.reset(); // Dock/hotkey commands supersede local transport.
        data->force_cue_state_sync = true;
        // Local and title-wide commands have independent revision sequences.
        data->program_state.command_revision = authored->cue_revision - 1;
    }
    const uint64_t revision = source_model_revision(data);
    if (data->local_cue_control && revision != data->local_cue_model_revision) {
        auto refreshed = clone_title_snapshot(authored);
        copy_title_cue_runtime_state(*refreshed, *data->local_cue_control);
        data->local_cue_control = std::move(refreshed);
        data->local_cue_model_revision = revision;
    }
    if (command) {
        auto control = clone_title_snapshot(authored);
        auto previous = data->local_cue_control ? data->local_cue_control : data->program_state.title;
        if (previous) copy_title_cue_runtime_state(*control, *previous);
        if (command == -1) {
            // Repeated Uncue cannot skip or restart the outro.
            if (data->waiting_for_cue || data->manual_uncue)
                return data->local_cue_control ? data->local_cue_control : authored;
            control->pending_cue_row = -1;
            control->cue_uncue_requested = true;
        } else {
            int row = command == -2 ? control->last_cue_row : command - 1;
            const auto exposed = exposed_text_layers(control);
            if (!exposed.empty()) fxm::live_text::normalize_live_text_rows(control, exposed);
            const int count = live_text_playlist_row_count(*control);
            if (row < 0) row = 0;
            if (row >= count) return data->local_cue_control ? data->local_cue_control : authored;
            apply_live_text_row(control, row);
            control->current_cue_row = row;
            control->last_cue_row = row;
            control->pending_cue_row = -1;
            control->cue_uncue_requested = false;
        }
        control->cue_persistence_transition = false;
        control->cue_persistent_text_columns.clear();
        control->cue_revision = std::max(control->cue_revision, data->seen_cue_revision) + 1;
        data->local_cue_control = std::move(control);
        data->local_cue_authored_revision = authored->cue_revision;
        data->local_cue_model_revision = revision;
        data->force_cue_state_sync = true;
        data->dirty = data->first_frame_pending = true;
    }
    return data->local_cue_control ? data->local_cue_control : authored;
}

struct SourceCueStatusPublication {
    TitleSourceData *data;
    ~SourceCueStatusPublication()
    {
        data->external_cue_active.store(!data->waiting_for_cue);
        data->external_cue_ending.store(data->manual_uncue ||
            data->cue_phase == TitleSourceData::CuePhase::OutroOnly);
        const auto title = data->program_state.title;
        data->external_cue_remaining.store(title ? std::max(0.0, title->duration - data->playhead) : 0.0);
        data->external_command_in_flight.store(false);
    }
};

static void source_proc_cue(void *priv, calldata_t *params)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    long long row = -1;
    calldata_get_int(params, "cue_row", &row);
    const bool accepted = row >= -1 && row < std::numeric_limits<int>::max() &&
        title_source_cue(data->source, static_cast<int>(row));
    calldata_set_bool(params, "accepted", accepted);
}
static void source_proc_uncue(void *priv, calldata_t *params)
{
    calldata_set_bool(params, "accepted", title_source_uncue(static_cast<TitleSourceData *>(priv)->source));
}
static void source_proc_cue_state(void *priv, calldata_t *params)
{
    const auto state = title_source_cue_status(static_cast<TitleSourceData *>(priv)->source);
    auto *data = static_cast<TitleSourceData *>(priv);
    const auto program = std::atomic_load(&data->program_render_title);
    calldata_set_int(params, "cue_row", program ? program->current_cue_row : -1);
    calldata_set_bool(params, "pending_changes", state.active &&
        obs_source_active(data->source) && data->output_visible.load() &&
        data->pending_program_changes.load());
    calldata_set_bool(params, "active", state.active);
    calldata_set_bool(params, "ending", state.ending);
    calldata_set_bool(params, "pending", state.pending);
    calldata_set_float(params, "remaining_seconds", state.remaining_seconds);
}
