            if (result_layer->fill_type == 1) {
                editor_remap_gradient_to_canvas_bounds(
                    result_layer->gradient_type, style_source_rect, style_source_world, bounds,
                    result_layer->gradient_center_x, result_layer->gradient_center_y,
                    result_layer->gradient_focal_x, result_layer->gradient_focal_y,
                    result_layer->gradient_scale, result_layer->gradient_angle);
            }
            if (result_layer->stroke_fill_type == 2) {
                editor_remap_gradient_to_canvas_bounds(
                    result_layer->stroke_gradient_type, style_source_rect, style_source_world, bounds,
                    result_layer->stroke_gradient_center_x, result_layer->stroke_gradient_center_y,
                    result_layer->stroke_gradient_focal_x, result_layer->stroke_gradient_focal_y,
                    result_layer->stroke_gradient_scale, result_layer->stroke_gradient_angle);
            }
            if (result_layer->background_fill_type == 1) {
                editor_remap_gradient_to_canvas_bounds(
                    result_layer->background_gradient_type, style_source_rect, style_source_world, bounds,
                    result_layer->background_gradient_center_x, result_layer->background_gradient_center_y,
                    result_layer->background_gradient_focal_x, result_layer->background_gradient_focal_y,
                    result_layer->background_gradient_scale, result_layer->background_gradient_angle);
            }
            for (LayerEffect &effect : result_layer->effects) {
                const bool uses_gradient =
                    (effect.type == LayerEffectType::BackgroundColor && effect.effect_fill_type == 1) ||
                    (effect.type == LayerEffectType::Outline && effect.effect_fill_type == 2);
                if (!effect.effect_owned_style_loaded || !uses_gradient)
                    continue;
                editor_remap_gradient_to_canvas_bounds(
                    effect.effect_gradient_type, style_source_rect, style_source_world, bounds,
                    effect.effect_gradient_center_x, effect.effect_gradient_center_y,
                    effect.effect_gradient_focal_x, effect.effect_gradient_focal_y,
                    effect.effect_gradient_scale, effect.effect_gradient_angle);
            }
        }
    }

    std::vector<std::shared_ptr<Layer>> next_layers;
    next_layers.reserve(title_->layers.size() - shapes.size() + (result_layer ? 1 : 0));
    const std::string topmost_selected_id = shapes.back()->id;
    for (const auto &layer : title_->layers) {
        if (!layer)
            continue;
        if (selected_ids.find(layer->id) == selected_ids.end()) {
            next_layers.push_back(layer);
            continue;
        }
        if (result_layer && layer->id == topmost_selected_id)
            next_layers.push_back(result_layer);
    }
    title_->layers = std::move(next_layers);

    for (const auto &layer : title_->layers) {
        if (!layer)
            continue;
        if (selected_ids.find(layer->parent_id) != selected_ids.end())
            layer->parent_id = result_layer ? result_layer->id : std::string();
        if (selected_ids.find(layer->transform_parent_id) != selected_ids.end())
            layer->transform_parent_id = result_layer ? result_layer->id : std::string();
        if (selected_ids.find(layer->mask_source_id) != selected_ids.end()) {
            layer->mask_source_id = result_layer ? result_layer->id : std::string();
            if (!result_layer)
                layer->mask_mode = MaskMode::None;
        }
    }

    const std::vector<std::string> selection = result_layer
        ? std::vector<std::string>{result_layer->id}
        : std::vector<std::string>{};
    layers_->refresh();
    /* Never let the pre-undo QTextDocument commit into the restored title.
     * set_title(..., commit_active_text=false) closes that adapter without a
     * model write, then we optionally reopen editing from the restored
     * canonical rich-text document below. */
    canvas_->set_title(title_, true, false);
    timeline_->set_title(title_);
    synchronize_layer_selection(selection);
    force_next_title_visual_update();
    on_title_modified();
}

std::vector<std::shared_ptr<Layer>> TitleEditor::clone_layers_for_insert(const std::vector<std::shared_ptr<Layer>> &layers,
                                                                         bool suffix_name) const
{
    std::map<std::string, std::string> cloned_ids_by_original;
    std::vector<std::shared_ptr<Layer>> clones;
    clones.reserve(layers.size());
    std::set<std::string> reserved_names;

    for (const auto &layer : layers) {
        if (!layer) continue;
        auto clone = std::make_shared<Layer>(*layer);
        clone->id = TitleDataStore::make_uuid();
        if (clone->type == LayerType::TransitionInput)
            clone->transition_input_required = false;
        if (suffix_name)
            clone->name = clone->name.empty() ? editor_text_std("OBSTitles.LayerCopy")
                                              : clone->name + editor_text_std("OBSTitles.CopySuffix");
        clone->name = unique_layer_name(clone->name, {}, &reserved_names);
        cloned_ids_by_original[layer->id] = clone->id;
        clones.push_back(clone);
    }

    for (auto &clone : clones) {
        auto parent_clone = cloned_ids_by_original.find(clone->parent_id);
        if (parent_clone != cloned_ids_by_original.end()) {
            clone->parent_id = parent_clone->second;
        } else if (!clone->parent_id.empty() && (!title_ || !title_->find_layer(clone->parent_id))) {
            clone->parent_id.clear();
        }

        auto transform_parent_clone = cloned_ids_by_original.find(clone->transform_parent_id);
        if (transform_parent_clone != cloned_ids_by_original.end()) {
            clone->transform_parent_id = transform_parent_clone->second;
        } else if (!clone->transform_parent_id.empty() &&
                   (!title_ || !title_->find_layer(clone->transform_parent_id))) {
            clone->transform_parent_id.clear();
        }

        auto mask_clone = cloned_ids_by_original.find(clone->mask_source_id);
        if (mask_clone != cloned_ids_by_original.end()) {
            clone->mask_source_id = mask_clone->second;
        } else if (!clone->mask_source_id.empty() && (!title_ || !title_->find_layer(clone->mask_source_id))) {
            clone->mask_source_id.clear();
            clone->mask_mode = MaskMode::None;
        }

        auto asset_owner_clone = cloned_ids_by_original.find(clone->asset_owner_id);
        if (asset_owner_clone != cloned_ids_by_original.end()) {
            clone->asset_owner_id = asset_owner_clone->second;
        } else if (!clone->asset_owner_id.empty() &&
                   (!title_ || !title_->find_layer(clone->asset_owner_id))) {
            clone->asset_owner_id.clear();
        }

        auto media_owner_clone = cloned_ids_by_original.find(clone->linked_media_layer_id);
        if (media_owner_clone != cloned_ids_by_original.end()) {
            clone->linked_media_layer_id = media_owner_clone->second;
        } else if (clone->linked_media_stream) {
            /* A stream is never allowed to become an independent copied layer. */
            clone->linked_media_layer_id.clear();
            clone->linked_media_stream = false;
            clone->parent_id.clear();
        }
    }

    return clones;
}

void TitleEditor::apply_picked_color_to_selection(const QColor &color, bool commit)
{
    if (!title_ || !color.isValid()) return;

    const auto ids = selected_layer_ids_for_operation();
    if (ids.empty()) return;

    const uint32_t argb = argb_from_color(color);
    std::shared_ptr<Layer> last_changed;
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer || layer->locked) continue;

        const double local_time = std::clamp(playhead_ - layer->in_time, 0.0,
                                             std::max(0.0, layer->out_time - layer->in_time));
        if (is_canvas_text_layer(*layer)) {
            const bool apply_inline_range = active_text_edit_layer_id_ == layer->id;
            RichTextCharFormat fmt;
            if (apply_inline_range) {
                /* The Color Swatches dock is outside the QTextEdit, but an
                 * active inline selection remains the edit target. Use the
                 * canonical selection summary instead of the layer mirrors;
                 * the latter represent the whole-text defaults and previously
                 * caused a swatch click to recolor the complete textbox. */
                const RichTextCharFormatSummary summary =
                    summarize_rich_text_char_format(*layer, true, local_time);
                fmt = summary.valid ? summary.format
                                    : layer_char_format_for_editor(*layer);
            } else {
                fmt = layer_char_format_for_editor(*layer);
            }
            fmt.fill.type = 0;
            fmt.fill.color = argb;
            apply_rich_text_format_to_layer_range(
                *layer, fmt, RichTextCharFillColor, apply_inline_range);
            if (apply_inline_range) {
                if (canvas_)
                    canvas_->apply_active_text_char_format(
                        layer->id, fmt, RichTextCharFillColor);
            } else {
                set_color_channels_at(*layer, true, local_time, argb);
            }
            last_changed = layer;
        } else if (layer->type == LayerType::Shape || layer->type == LayerType::SolidRect) {
            layer->fill_type = 0;
            layer->fill_color = argb;
            set_color_channels_at(*layer, false, local_time, argb);
            last_changed = layer;
        }
    }

    if (!last_changed) return;
    force_next_title_visual_update();
    if (canvas_)
        canvas_->refresh_preview();
    if (commit) {
        on_title_modified();
        update_layer_panels(last_changed, playhead_);
    }
    if (reopen_color_tab_after_canvas_pick_) {
        reopen_color_tab_after_canvas_pick_ = false;
        if (props_) props_->open_foreground_color_selector();
    }
}

void TitleEditor::duplicate_selected_layers()
{
    if (!title_) return;
    const auto requested_ids = selected_layer_ids_for_operation();
    if (requested_ids.empty()) return;
    auto ids = editor_selection_with_group_descendants(title_, requested_ids);
    if (ids.empty()) return;

    std::set<std::string> selected_ids(ids.begin(), ids.end());
    std::vector<std::shared_ptr<Layer>> originals;
    originals.reserve(ids.size());
    for (const auto &layer : title_->layers) {
        if (layer && selected_ids.find(layer->id) != selected_ids.end())
            originals.push_back(layer);
    }

    auto clones = clone_layers_for_insert(originals, true);
    if (clones.empty()) return;

    std::map<std::string, std::shared_ptr<Layer>> clones_by_original;
    for (size_t i = 0; i < originals.size() && i < clones.size(); ++i)
        clones_by_original[originals[i]->id] = clones[i];

    std::vector<std::shared_ptr<Layer>> next_layers;
    next_layers.reserve(title_->layers.size() + clones.size());
    for (const auto &layer : title_->layers) {
        next_layers.push_back(layer);
        if (!layer) continue;
        auto clone = clones_by_original.find(layer->id);
        if (clone != clones_by_original.end())
            next_layers.push_back(clone->second);
    }
    title_->layers = std::move(next_layers);
    synchronize_video_audio_streams(*title_);

    std::vector<std::string> clone_ids;
    clone_ids.reserve(requested_ids.size());
    for (const auto &requested_id : requested_ids) {
        const auto found = clones_by_original.find(requested_id);
        if (found != clones_by_original.end() && found->second)
            clone_ids.push_back(found->second->id);
    }

    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection(clone_ids);
    on_title_modified();
}


void TitleEditor::group_selected_layers()
{
    const auto operation_started = std::chrono::steady_clock::now();
    if (!title_)
        return;
    const auto selected_ids = selected_layer_ids_for_operation();
    auto roots = editor_top_level_selected_layers(title_, selected_ids);
    if (roots.size() < 2)
        return;
    std::size_t transform_keyframes = 0;
    for (const auto &layer : roots) {
        if (layer)
            transform_keyframes += editor_layer_transform_keyframe_count(*layer);
    }
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Group command begin selected=%1 roots=%2 transformKeyframes=%3 playhead=%4")
        .arg(static_cast<qulonglong>(selected_ids.size()))
        .arg(static_cast<qulonglong>(roots.size()))
        .arg(static_cast<qulonglong>(transform_keyframes))
        .arg(playhead_, 0, 'f', 6));

    std::string common_parent = roots.front()->parent_id;
    for (const auto &layer : roots) {
        if (!layer || layer->parent_id != common_parent) {
            common_parent.clear();
            break;
        }
    }

    QRectF canvas_bounds;
    bool have_bounds = false;
    int insertion_index = -1;
    for (const auto &layer : roots) {
        if (!layer)
            continue;
        const QRectF bounds = editor_layer_canvas_bounds_for_grouping(
            title_, *layer, playhead_);
        if (bounds.isValid() && !bounds.isEmpty()) {
            canvas_bounds = have_bounds ? canvas_bounds.united(bounds) : bounds;
            have_bounds = true;
        }
        auto it = std::find(title_->layers.begin(), title_->layers.end(), layer);
        if (it != title_->layers.end())
            insertion_index = std::max(insertion_index,
                static_cast<int>(std::distance(title_->layers.begin(), it)));
    }
    if (!have_bounds)
        return;

    auto group = std::make_shared<Layer>();
    group->id = TitleDataStore::make_uuid();
    group->name = unique_layer_name(editor_text_std("OBSTitles.Group"));
    group->type = LayerType::Group;
    /* A newly-created group starts closed.  The user can advance the caret to
     * group-keyframes-only or to the full child-editing state explicitly. */
    group->group_collapsed = true;
    group->properties_expanded = false;
    group->parent_id = common_parent;
    group->in_time = 0.0;
    group->out_time = title_->duration;
    group->origin_x = 0.5f;
    group->origin_y = 0.5f;
    group->origin_prop.static_value = {0.5, 0.5};
    group->rect_width = static_cast<float>(std::max(1.0, canvas_bounds.width()));
    group->rect_height = static_cast<float>(std::max(1.0, canvas_bounds.height()));
    group->size.static_value = {group->rect_width, group->rect_height};

    QPointF group_local_center = canvas_bounds.center();
    if (!common_parent.empty()) {
        if (const Layer *parent = editor_layer_by_id_for_parenting(
                title_, common_parent)) {
            bool invertible = false;
            const QTransform inverse = editor_layer_world_transform_for_parenting(
                title_, *parent, playhead_).inverted(&invertible);
            if (invertible)
                group_local_center = inverse.map(canvas_bounds.center());
        }
    }
    group->position.static_value = {group_local_center.x(),
                                    group_local_center.y()};

    if (insertion_index < 0 ||
        insertion_index >= static_cast<int>(title_->layers.size()))
        title_->layers.push_back(group);
    else
        title_->layers.insert(title_->layers.begin() + insertion_index + 1,
                              group);

    std::size_t bind_reparents = 0;
    for (const auto &layer : roots) {
        if (!layer)
            continue;
        if (editor_reparent_layer_with_parent_bind(
                title_, *layer, group->id, layer->transform_parent_id,
                playhead_)) {
            ++bind_reparents;
        } else {
            FXM_LOG_WARNING("Grouping", QStringLiteral(
                "Group command skipped layer=%1 reason=bind-reparent-failed")
                .arg(QString::fromStdString(layer->id)));
        }
    }

    const uint64_t mutation_us = editor_elapsed_us(operation_started);
    const auto refresh_started = std::chrono::steady_clock::now();
    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection({group->id});
    force_next_title_visual_update();
    const uint64_t refresh_us = editor_elapsed_us(refresh_started);
    const auto commit_started = std::chrono::steady_clock::now();
    on_title_modified();
    const uint64_t commit_us = editor_elapsed_us(commit_started);
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Group command complete group=%1 children=%2 bindReparents=%3 "
        "mutationUs=%4 refreshUs=%5 commitUs=%6 totalUs=%7")
        .arg(QString::fromStdString(group->id))
        .arg(static_cast<qulonglong>(roots.size()))
        .arg(static_cast<qulonglong>(bind_reparents))
        .arg(static_cast<qulonglong>(mutation_us))
        .arg(static_cast<qulonglong>(refresh_us))
        .arg(static_cast<qulonglong>(commit_us))
        .arg(static_cast<qulonglong>(editor_elapsed_us(operation_started))));
}

void TitleEditor::ungroup_selected_layers()
{
    const auto operation_started = std::chrono::steady_clock::now();
    if (!title_)
        return;
    const auto selected_ids = selected_layer_ids_for_operation();
    std::vector<std::shared_ptr<Layer>> groups;
    for (const auto &id : selected_ids) {
        auto layer = title_->find_layer(id);
        if (layer && layer->type == LayerType::Group)
            groups.push_back(layer);
    }
    if (groups.empty())
        return;
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Ungroup command begin selected=%1 groups=%2 playhead=%3")
        .arg(static_cast<qulonglong>(selected_ids.size()))
        .arg(static_cast<qulonglong>(groups.size()))
        .arg(playhead_, 0, 'f', 6));

    std::set<std::string> removed_group_ids;
    for (const auto &group : groups)
        if (group)
            removed_group_ids.insert(group->id);

    auto surviving_group_parent = [&](std::string parent_id) {
        std::set<std::string> visited;
        while (!parent_id.empty() &&
               removed_group_ids.find(parent_id) != removed_group_ids.end() &&
               visited.insert(parent_id).second) {
            auto parent = title_->find_layer(parent_id);
            parent_id = parent ? parent->parent_id : std::string();
        }
        return parent_id;
    };

    std::vector<std::string> next_selection;
    std::size_t bind_reparents = 0;
    for (const auto &layer : title_->layers) {
        if (!layer || removed_group_ids.find(layer->id) != removed_group_ids.end())
            continue;
        const bool group_changes =
            removed_group_ids.find(layer->parent_id) != removed_group_ids.end();
        const bool transform_parent_changes =
            removed_group_ids.find(layer->transform_parent_id) != removed_group_ids.end();
        if (!group_changes && !transform_parent_changes)
            continue;

        const std::string next_group_parent =
            group_changes ? surviving_group_parent(layer->parent_id)
                          : layer->parent_id;
        const std::string next_transform_parent =
            transform_parent_changes ? std::string()
                                     : layer->transform_parent_id;
        if (editor_reparent_layer_with_parent_bind(
                title_, *layer, next_group_parent, next_transform_parent,
                playhead_)) {
            ++bind_reparents;
            if (group_changes)
                next_selection.push_back(layer->id);
        } else {
            FXM_LOG_WARNING("Grouping", QStringLiteral(
                "Ungroup skipped dependent layer=%1 reason=bind-reparent-failed")
                .arg(QString::fromStdString(layer->id)));
        }
    }

    title_->layers.erase(std::remove_if(title_->layers.begin(),
                                        title_->layers.end(),
        [&](const std::shared_ptr<Layer> &layer) {
            return !layer || removed_group_ids.find(layer->id) !=
                             removed_group_ids.end();
        }), title_->layers.end());

    for (auto &layer : title_->layers) {
        if (!layer)
            continue;
        if (removed_group_ids.find(layer->mask_source_id) !=
            removed_group_ids.end()) {
            layer->mask_source_id.clear();
            layer->mask_mode = MaskMode::None;
        }
    }

    const uint64_t mutation_us = editor_elapsed_us(operation_started);
    const auto refresh_started = std::chrono::steady_clock::now();
    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection(next_selection);
    force_next_title_visual_update();
    const uint64_t refresh_us = editor_elapsed_us(refresh_started);
    const auto commit_started = std::chrono::steady_clock::now();
    on_title_modified();
    const uint64_t commit_us = editor_elapsed_us(commit_started);
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Ungroup command complete groups=%1 selectedChildren=%2 bindReparents=%3 "
        "mutationUs=%4 refreshUs=%5 commitUs=%6 totalUs=%7")
        .arg(static_cast<qulonglong>(groups.size()))
        .arg(static_cast<qulonglong>(next_selection.size()))
        .arg(static_cast<qulonglong>(bind_reparents))
        .arg(static_cast<qulonglong>(mutation_us))
        .arg(static_cast<qulonglong>(refresh_us))
        .arg(static_cast<qulonglong>(commit_us))
        .arg(static_cast<qulonglong>(editor_elapsed_us(operation_started))));
}

void TitleEditor::add_selected_layers_to_group(const std::string &group_id)
{
    const auto operation_started = std::chrono::steady_clock::now();
    if (!title_ || group_id.empty())
        return;
    auto group = title_->find_layer(group_id);
    if (!group || group->type != LayerType::Group || group->locked)
        return;

    const auto selected_ids = selected_layer_ids_for_operation();
    auto roots = editor_top_level_selected_layers(title_, selected_ids);
    std::size_t transform_keyframes = 0;
    for (const auto &layer : roots) {
        if (layer)
            transform_keyframes += editor_layer_transform_keyframe_count(*layer);
    }
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Add-to-group command begin group=%1 selected=%2 roots=%3 "
        "transformKeyframes=%4 playhead=%5")
        .arg(QString::fromStdString(group_id))
        .arg(static_cast<qulonglong>(selected_ids.size()))
        .arg(static_cast<qulonglong>(roots.size()))
        .arg(static_cast<qulonglong>(transform_keyframes))
        .arg(playhead_, 0, 'f', 6));
    bool changed = false;
    std::size_t bind_reparents = 0;
    for (const auto &layer : roots) {
        if (!layer || layer->id == group_id || layer->locked ||
            layer->parent_id == group_id ||
            editor_layer_has_ancestor_for_grouping(title_, *group,
                                                   layer->id))
            continue;
        if (editor_reparent_layer_with_parent_bind(
                title_, *layer, group_id, layer->transform_parent_id,
                playhead_)) {
            ++bind_reparents;
            changed = true;
        } else {
            FXM_LOG_WARNING("Grouping", QStringLiteral(
                "Add-to-group skipped layer=%1 reason=bind-reparent-failed")
                .arg(QString::fromStdString(layer->id)));
        }
    }
    if (!changed)
        return;

    /* Adding artwork to an existing group must not silently change its
     * presentation/editing state. */
    const uint64_t mutation_us = editor_elapsed_us(operation_started);
    const auto refresh_started = std::chrono::steady_clock::now();
    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection({group_id});
    force_next_title_visual_update();
    const uint64_t refresh_us = editor_elapsed_us(refresh_started);
    const auto commit_started = std::chrono::steady_clock::now();
    on_title_modified();
    const uint64_t commit_us = editor_elapsed_us(commit_started);
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Add-to-group command complete group=%1 moved=%2 bindReparents=%3 "
        "mutationUs=%4 refreshUs=%5 commitUs=%6 totalUs=%7")
        .arg(QString::fromStdString(group_id))
        .arg(static_cast<qulonglong>(bind_reparents))
        .arg(static_cast<qulonglong>(bind_reparents))
        .arg(static_cast<qulonglong>(mutation_us))
        .arg(static_cast<qulonglong>(refresh_us))
        .arg(static_cast<qulonglong>(commit_us))
        .arg(static_cast<qulonglong>(editor_elapsed_us(operation_started))));
}

void TitleEditor::drop_layer_list_rows(
    const std::vector<std::string> &layer_ids,
    const std::string &target_layer_id, int placement)
{
    constexpr int kDropIntoGroup = 2;
    if (!title_ || layer_ids.empty() || placement != kDropIntoGroup)
        return;

    const auto target = title_->find_layer(target_layer_id);
    if (!target || target->type != LayerType::Group || target->locked)
        return;

    auto roots = editor_top_level_selected_layers(title_, layer_ids);
    std::set<std::string> root_ids;
    for (const auto &root : roots)
        if (root)
            root_ids.insert(root->id);
    if (roots.empty() ||
        (target && root_ids.find(target->id) != root_ids.end()))
        return;

    const std::string destination_parent_id = target->id;

    std::vector<std::shared_ptr<Layer>> moving;
    moving.reserve(roots.size());
    bool reparented = false;
    for (const auto &root : roots) {
        if (!root || root->linked_media_stream)
            continue;
        const bool changes_parent =
            root->parent_id != destination_parent_id;
        if (changes_parent &&
            (root->locked || root->id == destination_parent_id ||
             editor_parenting_would_cycle(title_, root->id,
                                          destination_parent_id)))
            continue;
        if (changes_parent &&
            !editor_reparent_layer_with_parent_bind(
                title_, *root, destination_parent_id,
                root->transform_parent_id, playhead_))
            continue;
        reparented = reparented || changes_parent;
        moving.push_back(root);
    }
    if (moving.empty())
        return;

    std::set<std::string> moving_ids;
    for (const auto &layer : moving)
        moving_ids.insert(layer->id);

    std::map<std::string, std::vector<std::shared_ptr<Layer>>> scopes;
    for (const auto &layer : title_->layers) {
        if (layer)
            scopes[editor_group_order_scope(title_, *layer)].push_back(layer);
    }
    for (auto &[scope, siblings] : scopes) {
        siblings.erase(std::remove_if(
                           siblings.begin(), siblings.end(),
                           [&moving_ids](const auto &layer) {
                               return layer &&
                                   moving_ids.find(layer->id) !=
                                       moving_ids.end();
                           }),
                       siblings.end());
    }

    /* Children use bottom-to-top model order. Appending the dropped block
     * therefore places it at the top of the Group's visible child list. */
    auto &destination = scopes[destination_parent_id];
    destination.insert(destination.end(), moving.begin(), moving.end());

    auto reordered = editor_canonical_group_order(title_, scopes);
    if (reordered.size() != title_->layers.size())
        return;
    const bool reordered_layers = reordered != title_->layers;
    if (!reparented && !reordered_layers)
        return;
    title_->layers = std::move(reordered);

    std::vector<std::string> next_selection;
    next_selection.reserve(moving.size());
    for (const auto &layer : moving)
        next_selection.push_back(layer->id);
    if (target->group_collapsed)
        next_selection = {target->id};

    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection(next_selection);
    force_next_title_visual_update();
    on_title_modified();
}

void TitleEditor::remove_selected_layers_from_group()
{
    const auto operation_started = std::chrono::steady_clock::now();
    if (!title_)
        return;
    const auto selected_ids = selected_layer_ids_for_operation();
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Remove-from-group command begin selected=%1 playhead=%2")
        .arg(static_cast<qulonglong>(selected_ids.size()))
        .arg(playhead_, 0, 'f', 6));
    bool changed = false;
    std::size_t bind_reparents = 0;
    for (const auto &id : selected_ids) {
        auto layer = title_->find_layer(id);
        if (!layer || layer->locked || layer->parent_id.empty())
            continue;
        auto parent = title_->find_layer(layer->parent_id);
        if (!parent || parent->type != LayerType::Group)
            continue;
        if (editor_reparent_layer_with_parent_bind(
                title_, *layer, parent->parent_id,
                layer->transform_parent_id, playhead_)) {
            ++bind_reparents;
            changed = true;
        } else {
            FXM_LOG_WARNING("Grouping", QStringLiteral(
                "Remove-from-group skipped layer=%1 reason=bind-reparent-failed")
                .arg(QString::fromStdString(layer->id)));
        }
    }
    if (!changed)
        return;

    const uint64_t mutation_us = editor_elapsed_us(operation_started);
    const auto refresh_started = std::chrono::steady_clock::now();
    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection(selected_ids);
    force_next_title_visual_update();
    const uint64_t refresh_us = editor_elapsed_us(refresh_started);
    const auto commit_started = std::chrono::steady_clock::now();
    on_title_modified();
    const uint64_t commit_us = editor_elapsed_us(commit_started);
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Remove-from-group command complete moved=%1 bindReparents=%2 "
        "mutationUs=%3 refreshUs=%4 commitUs=%5 totalUs=%6")
        .arg(static_cast<qulonglong>(bind_reparents))
        .arg(static_cast<qulonglong>(bind_reparents))
        .arg(static_cast<qulonglong>(mutation_us))
        .arg(static_cast<qulonglong>(refresh_us))
        .arg(static_cast<qulonglong>(commit_us))
        .arg(static_cast<qulonglong>(editor_elapsed_us(operation_started))));
}

void TitleEditor::copy_selected_layer()
{
    if (!title_) return;
    const auto requested_ids = selected_layer_ids_for_operation();
    if (requested_ids.empty()) return;
    const auto ids = editor_selection_with_group_descendants(title_, requested_ids);

    layer_clipboard_.clear();
    layer_clipboard_cameras_.clear();
    layer_clipboard_source_title_id_ = title_->id;
    layer_clipboard_.reserve(ids.size());
    std::set<std::string> referenced_camera_ids;
    auto remember_camera_id = [&referenced_camera_ids](const std::string &camera_id) {
        if (!camera_id.empty()) referenced_camera_ids.insert(camera_id);
    };
    for (const auto &id : ids) {
        auto layer = title_->find_layer(id);
        if (!layer) continue;
        layer_clipboard_.push_back(std::make_shared<Layer>(*layer));
        remember_camera_id(layer->camera_id);
        remember_camera_id(layer->camera_assignment.static_value);
        for (const DiscreteKeyframe &key : layer->camera_assignment.keyframes)
            remember_camera_id(key.value);
    }
    for (const TitleCamera &camera : title_->cameras) {
        if (referenced_camera_ids.find(camera.id) != referenced_camera_ids.end())
            layer_clipboard_cameras_.push_back(camera);
    }

    if (!layer_clipboard_.empty() && QApplication::clipboard()) {
        static constexpr const char *kLayerClipboardMimeType =
            "application/x-fxm-layer-clipboard+json";
        const std::string payload = serialize_layer_clipboard_json(
            layer_clipboard_, layer_clipboard_cameras_,
            layer_clipboard_source_title_id_);
        auto *mime = new QMimeData();
        mime->setData(kLayerClipboardMimeType,
                      QByteArray::fromStdString(payload));
        QApplication::clipboard()->setMimeData(mime);
    }
    if (layers_) layers_->set_layer_clipboard_available(!layer_clipboard_.empty());
}

void TitleEditor::paste_layer_from_clipboard()
{
    if (!title_) return;

    static constexpr const char *kLayerClipboardMimeType =
        "application/x-fxm-layer-clipboard+json";
    const QMimeData *system_mime = QApplication::clipboard()
        ? QApplication::clipboard()->mimeData() : nullptr;
    if (system_mime && system_mime->hasFormat(kLayerClipboardMimeType)) {
        std::vector<std::shared_ptr<Layer>> decoded_layers;
        std::vector<TitleCamera> decoded_cameras;
        std::string decoded_source_title_id;
        std::string error;
        if (deserialize_layer_clipboard_json(
                system_mime->data(kLayerClipboardMimeType).toStdString(),
                &decoded_layers, &decoded_cameras,
                &decoded_source_title_id, &error)) {
            layer_clipboard_ = std::move(decoded_layers);
            layer_clipboard_cameras_ = std::move(decoded_cameras);
            layer_clipboard_source_title_id_ =
                std::move(decoded_source_title_id);
        }
    }
    if (layer_clipboard_.empty()) return;

    std::vector<std::shared_ptr<Layer>> clipboard_layers;
    clipboard_layers.reserve(layer_clipboard_.size());
    for (const auto &layer : layer_clipboard_)
        if (layer)
            clipboard_layers.push_back(std::make_shared<Layer>(*layer));

    /* Camera IDs are document-local. Copying layers to another open title must
     * carry every explicitly assigned camera and rewrite static/keyframed
     * assignments, otherwise the loader silently falls back to the active
     * camera and the pasted 3D result changes. */
    if (!layer_clipboard_cameras_.empty()) {
        std::map<std::string, std::string> camera_id_remap;
        std::set<std::string> used_camera_names;
        std::set<std::string> destination_camera_ids;
        for (const TitleCamera &camera : title_->cameras) {
            used_camera_names.insert(camera.name);
            destination_camera_ids.insert(camera.id);
        }

        auto unique_camera_name = [&used_camera_names](const std::string &source_name) {
            const std::string stem = source_name.empty() ? std::string("Camera") : source_name;
            std::string candidate = stem + " Copy";
            if (used_camera_names.insert(candidate).second)
                return candidate;
            for (int suffix = 2; suffix < 10000; ++suffix) {
                candidate = stem + " Copy " + std::to_string(suffix);
                if (used_camera_names.insert(candidate).second)
                    return candidate;
            }
            candidate = stem + " " + TitleDataStore::make_uuid().substr(0, 8);
            used_camera_names.insert(candidate);
            return candidate;
        };

        const bool copying_between_titles =
            !layer_clipboard_source_title_id_.empty() &&
            layer_clipboard_source_title_id_ != title_->id;
        for (const TitleCamera &source_camera : layer_clipboard_cameras_) {
            if (source_camera.id.empty()) continue;
            /* Same-title paste reuses a live camera. If that camera was deleted
             * after Copy, or the paste targets another title, materialize an
             * independent camera and remap every static/keyframed assignment. */
            if (!copying_between_titles &&
                destination_camera_ids.find(source_camera.id) !=
                    destination_camera_ids.end())
                continue;
            TitleCamera camera = source_camera;
            const std::string source_id = camera.id;
            camera.id = TitleDataStore::make_uuid();
            camera.name = unique_camera_name(camera.name);
            camera_id_remap[source_id] = camera.id;
            destination_camera_ids.insert(camera.id);
            title_->cameras.push_back(std::move(camera));
        }

        auto remap_camera_id = [&camera_id_remap](std::string &camera_id) {
            const auto it = camera_id_remap.find(camera_id);
            if (it != camera_id_remap.end()) camera_id = it->second;
        };
        for (auto &layer : clipboard_layers) {
            if (!layer) continue;
            remap_camera_id(layer->camera_id);
            remap_camera_id(layer->camera_assignment.static_value);
            for (DiscreteKeyframe &key : layer->camera_assignment.keyframes)
                remap_camera_id(key.value);
            layer->camera_id = layer->camera_assignment.static_value;
        }
    }

    auto pasted_layers = clone_layers_for_insert(clipboard_layers, true);
    if (pasted_layers.empty()) return;

    auto insert_pos = title_->layers.end();
    if (!sel_layer_id_.empty()) {
        auto anchor = std::find_if(title_->layers.begin(), title_->layers.end(),
                                   [this](const auto &layer) { return layer && layer->id == sel_layer_id_; });
        if (anchor != title_->layers.end()) insert_pos = anchor + 1;
    }
    title_->layers.insert(insert_pos, pasted_layers.begin(), pasted_layers.end());
    synchronize_video_audio_streams(*title_);

    std::set<std::string> clipboard_ids;
    for (const auto &layer : clipboard_layers)
        if (layer) clipboard_ids.insert(layer->id);
    std::vector<std::string> pasted_ids;
    pasted_ids.reserve(pasted_layers.size());
    for (size_t index = 0; index < pasted_layers.size() &&
                           index < clipboard_layers.size(); ++index) {
        const auto &source = clipboard_layers[index];
        const auto &pasted = pasted_layers[index];
        if (!source || !pasted)
            continue;
        if (source->parent_id.empty() ||
            clipboard_ids.find(source->parent_id) == clipboard_ids.end())
            pasted_ids.push_back(pasted->id);
    }
    if (pasted_ids.empty()) {
        for (const auto &layer : pasted_layers)
            if (layer) pasted_ids.push_back(layer->id);
    }

    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection(pasted_ids);
    on_title_modified();
}



bool TitleEditor::paste_external_clipboard_to_canvas()
{
    if (!title_)
        return false;
    const QMimeData *mime = QApplication::clipboard() ? QApplication::clipboard()->mimeData() : nullptr;
    if (!mime)
        return false;

    const QPointF canvas_pt = canvas_ ? canvas_->view_center_canvas_point()
                                      : QPointF(title_->width * 0.5, title_->height * 0.5);

    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            const QString path = url.toLocalFile();
            QImageReader reader(path);
            if (!reader.canRead())
                continue;
            create_image_layer_from_external_source(path, canvas_pt);
            return true;
        }
    }

    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            QString base_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            if (base_dir.isEmpty())
                base_dir = QDir::tempPath();
            QDir dir(base_dir + QStringLiteral("/obs-fxm-pasted-assets"));
            if (!dir.exists())
                dir.mkpath(QStringLiteral("."));
            const QString path = dir.filePath(QStringLiteral("pasted-image-%1.png")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
            if (image.save(path, "PNG")) {
                create_image_layer_from_external_source(path, canvas_pt);
                return true;
            }
        }
    }

    if (mime->hasText()) {
        const QString text = mime->text().trimmed();
        if (!text.isEmpty()) {
            create_text_layer_from_external_source(text, canvas_pt);
            return true;
        }
    }

    return false;
}

void TitleEditor::delete_selected_layer()
{
    if (!title_) return;
    const auto requested_ids = selected_layer_ids_for_operation();
    if (requested_ids.empty()) return;
    auto ids = editor_selection_with_group_descendants(title_, requested_ids);
    ids.erase(std::remove_if(ids.begin(), ids.end(), [this](const std::string &id) {
        auto layer = title_->find_layer(id);
        return layer && stinger_transition_input_layer_is_protected(*layer);
    }), ids.end());
    if (ids.empty()) return;

    std::set<std::string> removed_ids(ids.begin(), ids.end());
    auto surviving_group_parent = [&](std::string parent_id) {
        std::set<std::string> visited;
        while (!parent_id.empty() &&
               removed_ids.find(parent_id) != removed_ids.end() &&
               visited.insert(parent_id).second) {
            auto parent = title_->find_layer(parent_id);
            parent_id = parent ? parent->parent_id : std::string();
        }
        return parent_id;
    };
    for (const auto &layer : title_->layers) {
        if (!layer || removed_ids.find(layer->id) != removed_ids.end())
            continue;
        const bool group_changes =
            removed_ids.find(layer->parent_id) != removed_ids.end();
        const bool transform_parent_changes =
            removed_ids.find(layer->transform_parent_id) != removed_ids.end();
        if (!group_changes && !transform_parent_changes)
            continue;
        const std::string next_group_parent =
            group_changes ? surviving_group_parent(layer->parent_id)
                          : layer->parent_id;
        const std::string next_transform_parent =
            transform_parent_changes ? std::string()
                                     : layer->transform_parent_id;
        if (!editor_reparent_layer_with_parent_bind(
                title_, *layer, next_group_parent, next_transform_parent,
                playhead_)) {
            FXM_LOG_WARNING("Grouping", QStringLiteral(
                "Delete dependent reparent failed layer=%1")
                .arg(QString::fromStdString(layer->id)));
        }
    }

    int first_removed_index = (int)title_->layers.size();
    std::vector<std::shared_ptr<Layer>> remaining;
    remaining.reserve(title_->layers.size());
    for (int i = 0; i < (int)title_->layers.size(); ++i) {
        auto &layer = title_->layers[(size_t)i];
        if (!layer || removed_ids.find(layer->id) != removed_ids.end()) {
            first_removed_index = std::min(first_removed_index, i);
            continue;
        }
        remaining.push_back(layer);
    }

    if (remaining.size() == title_->layers.size()) return;

    for (const std::string &id : removed_ids)
        fxm::video::FrameRuntime::instance().forget_layer(id);

    title_->layers = std::move(remaining);
    std::set<std::string> referenced_camera_ids;
    for (const auto &layer : title_->layers) {
        if (!layer) continue;
        if (!layer->camera_id.empty())
            referenced_camera_ids.insert(layer->camera_id);
        if (!layer->camera_assignment.static_value.empty())
            referenced_camera_ids.insert(layer->camera_assignment.static_value);
        for (const DiscreteKeyframe &key : layer->camera_assignment.keyframes)
            if (!key.value.empty()) referenced_camera_ids.insert(key.value);
    }
    title_->cameras.erase(std::remove_if(
        title_->cameras.begin(), title_->cameras.end(),
        [&referenced_camera_ids](const TitleCamera &camera) {
            return !camera.asset_space_owner_id.empty() &&
                   referenced_camera_ids.find(camera.id) ==
                       referenced_camera_ids.end();
        }), title_->cameras.end());
    for (auto &layer : title_->layers) {
        if (!layer) continue;
        if (removed_ids.find(layer->parent_id) != removed_ids.end())
            layer->parent_id.clear();
        if (removed_ids.find(layer->transform_parent_id) != removed_ids.end())
            layer->transform_parent_id.clear();
        if (removed_ids.find(layer->mask_source_id) != removed_ids.end()) {
            layer->mask_source_id.clear();
            layer->mask_mode = MaskMode::None;
        }
    }
    sel_layer_id_.clear();
    layers_->refresh();

    if (!title_->layers.empty()) {
        const int select_index = std::clamp(first_removed_index, 0, (int)title_->layers.size() - 1);
        on_layer_selected(title_->layers[(size_t)select_index]->id);
    } else {
        timeline_->set_title(title_);
        synchronize_layer_selection({});

        /* Deleting the final layer is a special hard-empty transition.  Clear
         * the editor artwork synchronously and discard every RAM/disk frame for
         * this title before any delayed invalidation or repaint can reuse the
         * last valid cached image.  The CanvasPreview itself stays alive and
         * continues painting the checkerboard/editor chrome. */
        canvas_->clear_rendered_frame();
        CacheManager::instance().removeTitleCache(QString::fromStdString(title_->id), true);
    }

    force_next_title_visual_update();
    on_title_modified();
}

void TitleEditor::cut_selected_layer()
{
    copy_selected_layer();
    delete_selected_layer();
}

void TitleEditor::begin_text_property_transaction(const std::string &layer_id)
{
    if (restoring_undo_ || !title_ || layer_id.empty() ||
        active_text_edit_layer_id_ != layer_id ||
        !title_->find_layer(layer_id)) {
        return;
    }
    if (text_property_transaction_active_ &&
        text_property_transaction_layer_id_ == layer_id) {
        return;
    }

    /* The canonical document is updated continuously while typing. Before a
     * property operation starts, commit that current typed state as the true
     * pre-property history boundary. */
    if (inline_text_changed_since_undo_snapshot_) {
        push_undo_snapshot();
        inline_text_changed_since_undo_snapshot_ = false;
    }
    text_property_transaction_active_ = true;
    text_property_transaction_layer_id_ = layer_id;
}

void TitleEditor::commit_text_property_transaction(const std::string &layer_id)
{
    if (restoring_undo_ || !title_ || layer_id.empty() ||
        !title_->find_layer(layer_id)) {
        text_property_transaction_active_ = false;
        text_property_transaction_layer_id_.clear();
        return;
    }

    /* Keyboard formatting inside the transparent QTextEdit mutates the
     * RichTextDocument directly and intentionally clears adapter-only undo.
     * Record its canonical post-state in title history exactly once. */
    force_next_title_visual_update();
    on_title_modified(true);
    inline_text_changed_since_undo_snapshot_ = false;
    text_property_transaction_active_ = false;
    text_property_transaction_layer_id_.clear();
    if (layers_)
        layers_->refresh();
    if (auto layer = title_->find_layer(layer_id))
        update_layer_panels(layer, playhead_);
}

void TitleEditor::push_undo_snapshot()
{
    if (!title_ || restoring_undo_) return;
    if (undo_index_ + 1 < (int)undo_stack_.size())
        undo_stack_.erase(undo_stack_.begin() + undo_index_ + 1, undo_stack_.end());
    undo_stack_.push_back(clone_title(*title_));
    if (undo_stack_.size() > 30)
        undo_stack_.erase(undo_stack_.begin());
    undo_index_ = (int)undo_stack_.size() - 1;
    update_undo_redo_actions();
}

void TitleEditor::restore_undo_snapshot(int index)
{
    if (!title_ || index < 0 || index >= (int)undo_stack_.size()) return;
    restoring_undo_ = true;
    auto snapshot = undo_stack_[(size_t)index];
    const std::string text_edit_layer_before_restore =
        active_text_edit_layer_id_;
    inline_text_changed_since_undo_snapshot_ = false;
    text_property_transaction_active_ = false;
    text_property_transaction_layer_id_.clear();
    restore_title_authoring_snapshot(*title_, *snapshot);
    undo_index_ = index;
    if (!sel_layer_id_.empty() && !title_->find_layer(sel_layer_id_))
        sel_layer_id_.clear();
    if (sel_layer_id_.empty() && !title_->layers.empty())
        sel_layer_id_ = title_->layers.back()->id;
    update_title_bar();
    /* Undo/redo is an authoritative document replacement. Force the editor GPU
     * session and the deferred RAM/disk cache invalidation path to observe the
     * restored model before lightweight transform-only presentation resumes. */
    force_next_title_visual_update();
    schedule_cache_invalidation();
    /* Discard the pre-restore QTextEdit adapter. Committing it here would
     * immediately overwrite the canonical RichTextDocument restored from the
     * undo snapshot, especially for mixed inline properties. */
    canvas_->set_title(title_, true, false);
    layers_->set_title(title_);
    timeline_->set_title(title_);
    props_->set_title(title_);
    title_props_->set_title(title_);
    if (!sel_layer_id_.empty())
        synchronize_layer_selection({sel_layer_id_});
    else
        synchronize_layer_selection({});
    /* restore_title_authoring_snapshot() replaces the Layer instances while
     * retaining their IDs. If selection did not change, the normal selection
     * synchronizer intentionally skips its expensive panel rebuild and the
     * Properties/Effects panels would keep pointers to the pre-undo objects.
     * Rebind explicitly so every restored value, keyframe diamond and effect
     * is visible on the same event turn as Undo/Redo. */
    update_layer_panels(
        !sel_layer_id_.empty() ? title_->find_layer(sel_layer_id_) : nullptr,
        playhead_);
    active_text_edit_layer_id_.clear();
    if (props_)
        props_->set_active_text_edit_layer(std::string());
    if (!text_edit_layer_before_restore.empty() &&
        title_->find_layer(text_edit_layer_before_restore)) {
        active_text_edit_layer_id_ = text_edit_layer_before_restore;
        if (props_)
            props_->set_active_text_edit_layer(active_text_edit_layer_id_);
        canvas_->begin_text_edit_for_layer(active_text_edit_layer_id_);
    }
    on_playhead_changed(std::clamp(playhead_, 0.0, title_->duration));
    restoring_undo_ = false;
    update_undo_redo_actions();
    set_dirty(true);
    save_live_edit();
    /* Audio transport may not receive a playhead callback when undo restores at
     * the same time. Publish the restored audio model and force one seek/sync
     * so forward, reverse, fades and mixer state cannot remain stale. */
    publish_editor_audio_runtime_state();
    sync_editor_audio_preview(true);
}

void TitleEditor::update_undo_redo_actions()
{
    const bool undo_available = undo_index_ > 0;
    const bool redo_available = undo_index_ >= 0 &&
        undo_index_ + 1 < (int)undo_stack_.size();
    if (act_undo_) act_undo_->setEnabled(undo_available);
    if (act_redo_) act_redo_->setEnabled(redo_available);
    if (props_) props_->set_undo_redo_available(undo_available, redo_available);
}

void TitleEditor::update_title_bar()
{
    QString document_name;
    if (!current_project_path_.isEmpty())
        document_name = QFileInfo(current_project_path_).completeBaseName();
    else if (execution_context_.host ==
             fxm::editor::EditorHostKind::Standalone)
        document_name = QStringLiteral("Untitled");
    else if (title_)
        document_name = QString::fromStdString(title_->name);
    if (document_name.trimmed().isEmpty())
        document_name = QStringLiteral("Untitled");

    if (title_ && title_lbl_) {
        /* This label identifies the active project item, not the project file.
         * Keep it synchronized when the user switches between titles and
         * graphics in a multi-title project. */
        const QString title_text = QString::fromStdString(title_->name);
        if (title_lbl_->text() != title_text)
            title_lbl_->setText(title_text);
    }
    if (graphic_props_dock_) {
        const QString dock_title = title_
            ? fxm_tr("OBSTitles.PropertiesNamed").arg(QString::fromStdString(title_->name))
            : fxm_tr("OBSTitles.Properties");
        if (graphic_props_dock_->windowTitle() != dock_title)
            graphic_props_dock_->setWindowTitle(dock_title);
    }
    if (dirty_indicator_ && dirty_indicator_->isVisible() != dirty_)
        dirty_indicator_->setVisible(dirty_);

    const QString caption = fxm_editor_caption(
        QStringLiteral("%1 — Flux Motion").arg(document_name));
    if (windowTitle() != caption)
        setWindowTitle(caption);
    QWidget *top = window();
    if (top && top != this && top->windowTitle() != caption)
        top->setWindowTitle(caption);
}

void TitleEditor::set_project_file_path(const QString &path)
{
    const QFileInfo info(path);
    current_project_path_ = path.trimmed().isEmpty()
        ? QString() : info.absoluteFilePath();
    current_project_requires_save_as_ = !current_project_path_.isEmpty() &&
        info.suffix().compare(QStringLiteral("fxmproj"),
                              Qt::CaseInsensitive) != 0;
    if (!current_project_path_.isEmpty() && title_) {
        fxm::editor::RecentProjects::recordOpened(
            current_project_path_, info.completeBaseName(),
            QByteArray::fromStdString(title_->preview_screenshot_png_base64));
        emit recent_projects_changed();
    }
    update_title_bar();
}

void TitleEditor::set_project_context(const TitleProject &project,
                                      const QString &path)
{
    current_project_ = project;
    project_title_ids_.clear();
    for (const auto &title : project.titles)
        if (title) project_title_ids_.push_back(title->id);
    if (project_title_ids_.empty() && !editing_title_id_.empty())
        project_title_ids_.push_back(editing_title_id_);
    current_project_.active_title_id = editing_title_id_;
    project_dirty_ = false;
    set_project_file_path(path);
    set_dirty(false);
    refresh_project_panel();
}

void TitleEditor::begin_unsaved_project()
{
    current_project_path_.clear();
    current_project_requires_save_as_ = false;
    if (current_project_.id.empty())
        current_project_.id = TitleDataStore::make_uuid();
    current_project_.name = "Untitled Project";
    current_project_.active_title_id = editing_title_id_;
    if (project_title_ids_.empty() && !editing_title_id_.empty())
        project_title_ids_.push_back(editing_title_id_);
    project_dirty_ = true;
    set_dirty(true);
    refresh_project_panel();
}

void TitleEditor::begin_title_name_edit()
{
    if (!title_ || !title_lbl_ || !title_name_edit_)
        return;

    title_name_edit_->setText(QString::fromStdString(title_->name));
    title_lbl_->hide();
    title_name_edit_->show();
    title_name_edit_->setFocus(Qt::MouseFocusReason);
    title_name_edit_->selectAll();
}

void TitleEditor::commit_title_name_edit(bool accept)
{
    if (!title_name_edit_ || !title_name_edit_->isVisible())
        return;

    const QString next_name = title_name_edit_->text().trimmed();
    title_name_edit_->hide();
    if (title_lbl_)
        title_lbl_->show();

    if (accept && title_ && !next_name.isEmpty() && next_name.toStdString() != title_->name) {
        title_->name = next_name.toStdString();
        update_title_bar();
        if (title_props_)
            title_props_->set_title(title_);
        set_dirty(true);
        push_undo_snapshot();
        save_live_edit();
    } else {
        update_title_bar();
    }
}

void TitleEditor::set_dirty(bool dirty)
{
    if (dirty && execution_context_.host ==
                     fxm::editor::EditorHostKind::Standalone)
        project_dirty_ = true;
    dirty_ = dirty || (execution_context_.host ==
                           fxm::editor::EditorHostKind::Standalone &&
                       project_dirty_);
    if (dirty_indicator_ && dirty_indicator_->isVisible() != dirty_)
        dirty_indicator_->setVisible(dirty_);
    update_title_bar();
}

void TitleEditor::show_project_settings()
{
    if (!graphic_props_dock_)
        return;
    set_editor_dock_visible(graphic_props_dock_, true);
    graphic_props_dock_->show();
    graphic_props_dock_->raise();
    if (title_props_)
        title_props_->setFocus(Qt::OtherFocusReason);
}

void TitleEditor::exit_application()
{
    QWidget *top = window();
    if (top && top != this)
        top->close();
    else
        close();
}

bool TitleEditor::confirm_save_before_close()
{
    if (!dirty_)
        return true;

    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(fxm_tr("OBSTitles.UnsavedChangesTitle"));
    dialog.setText(fxm_tr("OBSTitles.UnsavedChangesPrompt"));
    dialog.setStandardButtons(QMessageBox::Save | QMessageBox::Discard |
                              QMessageBox::Cancel);
    dialog.setDefaultButton(QMessageBox::Save);
    dialog.setEscapeButton(QMessageBox::Cancel);

    const auto result = static_cast<QMessageBox::StandardButton>(dialog.exec());
    if (result == QMessageBox::Save)
        return save_title();
    if (result == QMessageBox::Discard)
        return true;
    return false;
}

/* ── Transport ───────────────────────────────────────────────────── */
void TitleEditor::try_start_playback_after_prerender()
{
    if (!pending_play_after_prerender_ || !title_ || playing_)
        return;
    CacheManager &cache = CacheManager::instance();
    if (!cache.titleFullyReadyForPlayback(title_))
        return;
    pending_play_after_prerender_ = false;
    show_editor_activity(QStringLiteral(
        "Prerender complete. Starting playback."));
    play_pause();
}

void TitleEditor::play_pause()
{
    if (!title_) return;
    if (pending_play_after_prerender_) {
        pending_play_after_prerender_ = false;
        show_editor_activity(QStringLiteral(
            "Playback request cancelled."));
        return;
    }
    if (!playing_) {
        CacheManager &cache = CacheManager::instance();
        const CachePlaybackSettings settings = cache.playbackSettings();
        if (settings.cached_frames_only && cache.cacheEnabled() &&
            cache.titleCacheability(title_) != TitleCacheability::NonCacheable &&
            !cache.titleFullyReadyForPlayback(title_)) {
            pending_play_after_prerender_ = true;
            cache.resumePrerender();
            cache.queueWholeTimeline(title_);
            show_editor_activity(QStringLiteral(
                "Prerendering the entire timeline before playback…"), 0);
            return;
        }
    }
    if (!playing_) {
        full_loop_playback_ = false;
        manual_reverse_playback_ = false;
        playback_reverse_ = false;
    }
    playing_ = !playing_;
    if (canvas_) canvas_->set_playback_active(playing_);
    if (playing_) {
        playback_transport_ui_clock_.invalidate();
        playback_inspector_ui_clock_.invalidate();
        playback_diagnostics_clock_.invalidate();
        playback_pointer_ui_clock_.invalidate();
        const CachePlaybackSettings cache_settings = CacheManager::instance().playbackSettings();
        if (cache_settings.from_beginning) {
            /* Start is independent of Mode.  "From Beginning" always means
             * timeline zero; an authored loop still plays its intro before it
             * enters the configured Loop Start/End region. */
            on_playhead_changed(0.0);
        } else if (playhead_ >= title_->duration) {
            /* Restart only at the physical end of the title. Being after a
             * Pause/Loop zone is a valid current-playhead start when Playback
             * and Cache is set to start from the current frame. */
            on_playhead_changed(0.0);
        }
        act_play_->setText("⏸");
        act_play_->setIcon(obs_icon("pause.svg"));
        playback_clock_.restart();
        reset_playback_timer_cadence();
        play_timer_->start();
        sync_editor_audio_preview(true);
    } else {
        act_play_->setText("▶");
        act_play_->setIcon(obs_icon("play.svg"));
        play_timer_->stop();
        sync_editor_audio_preview(false);
        refresh_playback_ui(playhead_, true);
    }
}

void TitleEditor::play_full_loop()
{
    if (!title_) return;
    full_loop_playback_ = true;
    playback_reverse_ = false;
    if (!playing_ || playhead_ >= title_->duration)
        on_playhead_changed(0.0);
    playing_ = true;
    playback_transport_ui_clock_.invalidate();
    playback_inspector_ui_clock_.invalidate();
    playback_diagnostics_clock_.invalidate();
    playback_pointer_ui_clock_.invalidate();
    if (canvas_) canvas_->set_playback_active(true);
    act_play_->setText("⏸");
    act_play_->setIcon(obs_icon("pause.svg"));
    playback_clock_.restart();
    reset_playback_timer_cadence();
    play_timer_->start();
    sync_editor_audio_preview(true);
}

void TitleEditor::reverse_play()
{
    if (!title_) return;
    if (playing_ && manual_reverse_playback_) {
        playing_ = false;
        if (canvas_) canvas_->set_playback_active(false);
        manual_reverse_playback_ = false;
        play_timer_->stop();
        act_play_->setText("▶");
        act_play_->setIcon(obs_icon("play.svg"));
        sync_editor_audio_preview(false);
        refresh_playback_ui(playhead_, true);
        return;
    }
    full_loop_playback_ = false;
    manual_reverse_playback_ = true;
    playback_reverse_ = true;
    if (playhead_ <= 0.0)
        on_playhead_changed(title_->duration);
    playing_ = true;
    playback_transport_ui_clock_.invalidate();
    playback_inspector_ui_clock_.invalidate();
    playback_diagnostics_clock_.invalidate();
    playback_pointer_ui_clock_.invalidate();
    if (canvas_) canvas_->set_playback_active(true);
    act_play_->setText("⏸");
    act_play_->setIcon(obs_icon("pause.svg"));
    playback_clock_.restart();
    reset_playback_timer_cadence();
    play_timer_->start();
    sync_editor_audio_preview(true);
}

void TitleEditor::go_to_start()
{
    manual_reverse_playback_ = false;
    playback_reverse_ = false;
    on_playhead_changed(0.0);
}

void TitleEditor::go_to_end()
{
    if (!title_) return;
    manual_reverse_playback_ = false;
    playback_reverse_ = false;
    on_playhead_changed(title_->duration);
}

void TitleEditor::step_backward()
{
    if (!title_) return;
    on_playhead_changed(std::max(0.0, snap_to_obs_frame(playhead_ - obs_frame_duration())));
}


void TitleEditor::insert_asset_layer(const std::string &asset_id,
                                     const QPointF &canvas_pt)
{
    if (!title_ || asset_id.empty())
        return;
    auto asset_title = TitleDataStore::instance().get_title_snapshot(asset_id);
    if (!asset_title || asset_title->layers.empty())
        return;
    if (asset_title->id == title_->id) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.Assets"),
                             fxm_tr("OBSTitles.AssetRecursiveInsertError"));
        return;
    }
    std::set<std::string> visiting;
    std::function<bool(const std::string &)> references_current =
        [&](const std::string &candidate_id) {
            if (candidate_id == title_->id)
                return true;
            if (!visiting.insert(candidate_id).second)
                return false;
            auto candidate = TitleDataStore::instance().get_title_snapshot(candidate_id);
            if (candidate) {
                for (const auto &layer : candidate->layers) {
                    if (layer && layer->type == LayerType::Asset &&
                        !layer->asset_title_id.empty() &&
                        references_current(layer->asset_title_id))
                        return true;
                }
            }
            return false;
        };
    if (references_current(asset_title->id)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.Assets"),
                             fxm_tr("OBSTitles.AssetRecursiveInsertError"));
        return;
    }

    auto bounds_title = clone_title(*asset_title);
    QRectF source_bounds = editor_title_animation_bounds(bounds_title);
    if (!source_bounds.isValid() || source_bounds.isEmpty()) {
        const double center_x = static_cast<double>(asset_title->width) * 0.5;
        const double center_y = static_cast<double>(asset_title->height) * 0.5;
        source_bounds = QRectF(center_x - 16.0, center_y - 16.0, 32.0, 32.0);
    }

    push_undo_snapshot();

    auto asset_layer = std::make_shared<Layer>();
    asset_layer->id = TitleDataStore::make_uuid();
    asset_layer->name = unique_layer_name(asset_title->name);
    asset_layer->type = LayerType::Asset;
    asset_layer->group_collapsed = true;
    asset_layer->properties_expanded = false;
    asset_layer->asset_title_id = asset_title->id;
    asset_layer->asset_category = asset_title->asset_category;
    asset_layer->asset_animated = fxm::asset_runtime::title_has_timeline_animation(*asset_title);
    asset_layer->asset_playback_mode = 0;
    asset_layer->asset_playback_offset = 0.0;
    asset_layer->asset_duration = std::max(0.1, asset_title->duration);
    asset_layer->asset_source_playback_mode = std::clamp(asset_title->playback_mode, 0, 2);
    asset_layer->asset_source_loop_type = std::clamp(asset_title->loop_type, 0, 1);
    asset_layer->asset_source_loop_start = std::clamp(asset_title->loop_start, 0.0, asset_layer->asset_duration);
    asset_layer->asset_source_loop_end = std::clamp(asset_title->loop_end,
        asset_layer->asset_source_loop_start, asset_layer->asset_duration);
    asset_layer->asset_source_pause_time = std::clamp(asset_title->pause_time,
        0.0, asset_layer->asset_duration);
    asset_layer->asset_pause_duration = 1.0;
    asset_layer->asset_loop_count = 1;
    asset_layer->asset_loop = false;
    asset_layer->asset_isolated_3d_space = true;
    asset_layer->asset_space_width = std::max(1, asset_title->width);
    asset_layer->asset_space_height = std::max(1, asset_title->height);
    asset_layer->asset_space_center_x = source_bounds.center().x();
    asset_layer->asset_space_center_y = source_bounds.center().y();
    asset_layer->in_time = 0.0;
    asset_layer->out_time = title_->duration;
    asset_layer->origin_x = 0.5f;
    asset_layer->origin_y = 0.5f;
    asset_layer->origin_prop.static_value = {0.5, 0.5};
    asset_layer->rect_width = static_cast<float>(std::max(1.0, source_bounds.width()));
    asset_layer->rect_height = static_cast<float>(std::max(1.0, source_bounds.height()));
    asset_layer->size.static_value = {asset_layer->rect_width,
                                      asset_layer->rect_height};
    asset_layer->position.static_value = {canvas_pt.x(), canvas_pt.y()};

    std::map<std::string, std::string> id_map;
    for (const auto &source : asset_title->layers) {
        if (source)
            id_map[source->id] = TitleDataStore::make_uuid();
    }

    /* Cameras are document-local. Snapshot every authored source camera into
     * the host with an Asset Layer owner, including cameras belonging to a
     * nested Asset Layer, then make every cloned descendant's assignment
     * explicit. The host active camera can therefore never leak into the
     * asset's internal 3D projection. */
    std::map<std::string, std::string> camera_id_map;
    for (const TitleCamera &source_camera : asset_title->cameras) {
        if (source_camera.id.empty())
            continue;
        TitleCamera camera = source_camera;
        const std::string source_camera_id = camera.id;
        camera.id = TitleDataStore::make_uuid();
        const auto nested_owner = id_map.find(source_camera.asset_space_owner_id);
        camera.asset_space_owner_id = nested_owner != id_map.end()
            ? nested_owner->second : asset_layer->id;
        camera.timeline_expanded = false;
        camera_id_map[source_camera_id] = camera.id;
        title_->cameras.push_back(std::move(camera));
    }

    auto remapped_asset_camera = [&](std::string camera_id,
                                     double source_time) {
        if (camera_id.empty()) {
            camera_id = asset_title->active_camera.evaluate(
                std::max(0.0, source_time));
            if (camera_id.empty())
                camera_id = asset_title->active_camera_id;
        }
        const auto mapped = camera_id_map.find(camera_id);
        if (mapped != camera_id_map.end())
            return mapped->second;
        const auto fallback = camera_id_map.find(asset_title->active_camera_id);
        return fallback != camera_id_map.end() ? fallback->second
                                                : std::string();
    };

    std::vector<std::shared_ptr<Layer>> clones;
    clones.reserve(asset_title->layers.size());
    const QPointF source_center = source_bounds.center();
    for (const auto &source : asset_title->layers) {
        if (!source)
            continue;
        auto clone = std::make_shared<Layer>(*source);
        clone->id = id_map[source->id];
        const auto owner_it = id_map.find(source->asset_owner_id);
        clone->asset_owner_id = owner_it != id_map.end()
            ? owner_it->second : asset_layer->id;
        clone->asset_source_layer_id = source->id;
        clone->group_collapsed = source->type == LayerType::Asset
            ? true : source->group_collapsed;

        const auto parent_it = id_map.find(source->parent_id);
        clone->parent_id = parent_it != id_map.end()
            ? parent_it->second : asset_layer->id;
        const auto media_owner_it = id_map.find(source->linked_media_layer_id);
        if (media_owner_it != id_map.end()) {
            clone->linked_media_layer_id = media_owner_it->second;
        } else if (clone->linked_media_stream) {
            clone->linked_media_layer_id.clear();
            clone->linked_media_stream = false;
        }
        const auto transform_it = id_map.find(source->transform_parent_id);
        clone->transform_parent_id = transform_it != id_map.end()
            ? transform_it->second : std::string();
        const auto mask_it = id_map.find(source->mask_source_id);
        if (mask_it != id_map.end()) {
            clone->mask_source_id = mask_it->second;
        } else {
            clone->mask_source_id.clear();
            clone->mask_mode = MaskMode::None;
        }

        const bool follows_source_active_camera =
            source->camera_id.empty() &&
            source->camera_assignment.static_value.empty() &&
            source->camera_assignment.keyframes.empty();
        if (follows_source_active_camera) {
            clone->camera_assignment = asset_title->active_camera;
            clone->camera_assignment.name = "camera_assignment";
            clone->asset_camera_uses_owner_time = true;
        }
        clone->camera_assignment.static_value = remapped_asset_camera(
            clone->camera_assignment.static_value.empty()
                ? source->camera_id
                : clone->camera_assignment.static_value,
            0.0);
        for (DiscreteKeyframe &key : clone->camera_assignment.keyframes)
            key.value = remapped_asset_camera(key.value, key.time);
        clone->camera_id = clone->camera_assignment.static_value;

        if (source->parent_id.empty()) {
            editor_offset_layer_position_track(*clone,
                                               -source_center.x(),
                                               -source_center.y());
        }
        clones.push_back(clone);
    }

    for (auto &clone : clones)
        title_->layers.push_back(std::move(clone));
    title_->layers.push_back(asset_layer);
    synchronize_video_audio_streams(*title_);

    layers_->refresh();
    timeline_->set_title(title_);
    synchronize_layer_selection({asset_layer->id});
    force_next_title_visual_update();
    on_title_modified(false);
}

void TitleEditor::edit_asset(const std::string &asset_id)
{
    if (asset_id.empty())
        return;
    auto asset = TitleDataStore::instance().get_title(asset_id);
    if (!asset || !asset->is_asset) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.EditAsset"),
                             fxm_tr("OBSTitles.AssetNotFound"));
        return;
    }
    if (editing_title_id_ == asset_id)
        return;

    /* Switching the editor to an asset must never silently discard the title
     * currently being edited. Ask only when the current title is dirty. */
    if (dirty_) {
        QMessageBox dialog(this);
        dialog.setIcon(QMessageBox::Warning);
        dialog.setWindowTitle(fxm_tr("OBSTitles.UnsavedChangesTitle"));
        dialog.setText(fxm_tr("OBSTitles.SaveBeforeEditAssetPrompt"));
        dialog.setStandardButtons(
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        dialog.setDefaultButton(QMessageBox::Save);
        dialog.setEscapeButton(QMessageBox::Cancel);
        const auto result = static_cast<QMessageBox::StandardButton>(
            dialog.exec());
        if (result == QMessageBox::Save) {
            if (!save_title())
                return;
        } else if (result != QMessageBox::Discard) {
            return;
        }
    }

    open_title(asset_id);
    show();
    raise();
    activateWindow();
}

void TitleEditor::open_asset_overrides_dialog(
    const std::string &asset_layer_id)
{
    if (!title_)
        return;
    auto asset_layer = title_->find_layer(asset_layer_id);
    if (!asset_layer || asset_layer->type != LayerType::Asset)
        return;

    QDialog dialog(this);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.AssetLayerProperties"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();

    auto *playback = new QComboBox(&dialog);
    playback->addItem(fxm_tr("OBSTitles.AssetPlaybackSynchronized"), 0);
    playback->addItem(fxm_tr("OBSTitles.AssetPlaybackIndependent"), 1);
    playback->setCurrentIndex(std::clamp(asset_layer->asset_playback_mode, 0, 1));
    form->addRow(fxm_tr("OBSTitles.AssetPlayback"), playback);

    auto *offset = new QDoubleSpinBox(&dialog);
    offset->setRange(-86400.0, 86400.0);
    offset->setDecimals(3);
    offset->setSuffix(QStringLiteral(" s"));
    offset->setValue(asset_layer->asset_playback_offset);
    form->addRow(fxm_tr("OBSTitles.AssetPlaybackOffset"), offset);

    auto *pause_duration = new TimecodeSpinBox(&dialog);
    pause_duration->setRange(0.0, 86400.0);
    pause_duration->setValue(asset_layer->asset_pause_duration);
    form->addRow(fxm_tr("OBSTitles.AssetPauseFor"), pause_duration);

    auto *loop_count = new QSpinBox(&dialog);
    loop_count->setRange(1, 1000000);
    loop_count->setSuffix(fxm_tr("OBSTitles.AssetLoopTimesSuffix"));
    loop_count->setValue(std::max(1, asset_layer->asset_loop_count));
    form->addRow(fxm_tr("OBSTitles.AssetLoopCount"), loop_count);

    auto *loop = new FxmSwitch(fxm_tr("OBSTitles.AssetLoop"), &dialog);
    loop->setChecked(asset_layer->asset_loop);
    form->addRow(QString(), loop);
    layout->addLayout(form);

    auto set_form_field_visible = [form](QWidget *field, bool visible) {
        if (!field) return;
        field->setVisible(visible);
        if (auto *label = qobject_cast<QWidget *>(form->labelForField(field)))
            label->setVisible(visible);
    };
    auto refresh_playback_controls = [=]() {
        const bool has_animation = asset_layer->asset_animated;
        const bool independent = has_animation && playback->currentData().toInt() == 1;
        set_form_field_visible(playback, has_animation);
        set_form_field_visible(offset, independent);
        set_form_field_visible(pause_duration,
            independent && asset_layer->asset_source_playback_mode == 2);
        set_form_field_visible(loop_count,
            independent && asset_layer->asset_source_playback_mode == 1);
        set_form_field_visible(loop, independent);
    };
    connect(playback, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, [refresh_playback_controls](int) {
                refresh_playback_controls();
            });
    refresh_playback_controls();

    auto *overrides_label = new QLabel(fxm_tr("OBSTitles.AssetExposedOverrides"), &dialog);
    QFont label_font = overrides_label->font();
    label_font.setBold(true);
    overrides_label->setFont(label_font);
    layout->addWidget(overrides_label);

    auto *overrides_form = new QFormLayout();
    std::vector<std::function<void()>> apply_overrides;
    int exposed_count = 0;
    for (const auto &child : title_->layers) {
        if (!child || !child->expose_text ||
            !editor_layer_has_ancestor_for_grouping(title_, *child,
                                                    asset_layer->id))
            continue;
        if (child->type == LayerType::Text || child->type == LayerType::Ticker ||
            child->type == LayerType::Clock) {
            auto *edit = new QLineEdit(QString::fromStdString(child->text_content), &dialog);
            overrides_form->addRow(QString::fromStdString(child->name), edit);
            apply_overrides.emplace_back([child, edit]() {
                const std::string text = edit->text().toStdString();
                rich_text_document_ensure_canonical(*child);
                rich_text_document_replace_text(child->rich_text, text);
                child->text_content = text;
            });
            ++exposed_count;
        } else if (child->type == LayerType::Image) {
            auto *row = new QWidget(&dialog);
            auto *row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);
            auto *edit = new QLineEdit(QString::fromStdString(child->image_path), row);
            auto *browse = new QToolButton(row);
            browse->setText(QStringLiteral("…"));
            connect(browse, &QToolButton::clicked, &dialog, [edit, &dialog]() {
                const QString path = QFileDialog::getOpenFileName(
                    &dialog, fxm_tr("OBSTitles.SelectImage"), edit->text(),
                    fxm_tr("OBSTitles.ImageFileFilter"));
                if (!path.isEmpty())
                    edit->setText(path);
            });
            row_layout->addWidget(edit, 1);
            row_layout->addWidget(browse);
            overrides_form->addRow(QString::fromStdString(child->name), row);
            apply_overrides.emplace_back([child, edit]() {
                fxm::apply_exposed_image_cue_value(*child,
                                                    edit->text().toStdString());
            });
            ++exposed_count;
        }
    }
    if (exposed_count == 0) {
        auto *none = new QLabel(fxm_tr("OBSTitles.AssetNoExposedProperties"), &dialog);
        none->setWordWrap(true);
        layout->addWidget(none);
    } else {
        layout->addLayout(overrides_form);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    push_undo_snapshot();
    asset_layer->asset_playback_mode = asset_layer->asset_animated
        ? playback->currentData().toInt() : 0;
    asset_layer->asset_playback_offset = offset->value();
    asset_layer->asset_pause_duration = pause_duration->value();
    asset_layer->asset_loop_count = loop_count->value();
    asset_layer->asset_loop = loop->isChecked();
    fxm::asset_runtime::reset_layer(title_->id, asset_layer->id);
    for (const auto &apply : apply_overrides)
        apply();
    update_layer_panels(asset_layer, playhead_);
    layers_->refresh();
    canvas_->refresh_preview();
    timeline_->set_title(title_);
    on_title_modified(false);
}
