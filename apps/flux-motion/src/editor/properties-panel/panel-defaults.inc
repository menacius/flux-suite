void PropertiesPanel::reset_panel_defaults(const QString &panel_key)
{
    if (!layer_ || loading_values_)
        return;

    const QString key = panel_key.trimmed().toLower();
    const double t = std::max(0.0, playhead_ - layer_->in_time);
    Layer defaults;
    bool changed = true;

    auto reset_shape_size_to_intrinsic_image = [&]() {
        QSize intrinsic = editor_image_intrinsic_size(QString::fromStdString(layer_->image_path));
        if (!intrinsic.isValid() || intrinsic.isEmpty())
            intrinsic = QSize(std::max(1, (int)std::lround(layer_->image_width)),
                              std::max(1, (int)std::lround(layer_->image_height)));
        layer_->image_width = (float)intrinsic.width();
        layer_->image_height = (float)intrinsic.height();
        set_animated_x(layer_->image_size, t, layer_->image_width);
        set_animated_y(layer_->image_size, t, layer_->image_height);
        layer_->image_size_auto_fit = true;
    };

    const bool text_like = layer_->type == LayerType::Text ||
                           layer_->type == LayerType::Clock ||
                           layer_->type == LayerType::Ticker;
    const bool active_text_selection = text_like &&
        active_text_edit_layer_id_ == layer_->id;
    auto begin_text_property_change = [&]() {
        if (active_text_selection)
            emit text_property_change_started(layer_->id);
    };

    if (key.contains(QStringLiteral("character"))) {
        if (!text_like) {
            changed = false;
        } else {
            begin_text_property_change();
            RichTextCharFormat format = rich_text_char_format_from_layer(defaults);
            const uint32_t mask = RichTextCharFontFamily | RichTextCharFontStyle |
                RichTextCharFontSize | RichTextCharBold | RichTextCharItalic |
                RichTextCharKerning | RichTextCharTracking |
                RichTextCharScaleX | RichTextCharScaleY |
                RichTextCharBaselineShift;
            apply_rich_text_format_to_layer_range(
                *layer_, format, mask, active_text_selection);
            RichTextParagraphFormat paragraph =
                defaults.rich_text.empty()
                    ? rich_text_document_from_layer_defaults(defaults)
                          .default_paragraph_format
                    : defaults.rich_text.default_paragraph_format;
            apply_rich_text_paragraph_format_to_layer(
                *layer_, paragraph, RichTextParagraphLineSpacing,
                active_text_selection);
            if (!active_text_selection) {
                set_animated_value(layer_->font_size_prop, t, format.font_size);
                set_animated_value(layer_->char_tracking_prop, t, format.tracking);
                set_animated_value(layer_->char_scale_x_prop, t, format.scale_x);
                set_animated_value(layer_->char_scale_y_prop, t, format.scale_y);
                set_animated_value(layer_->baseline_shift_prop, t,
                                   format.baseline_shift);
            }
            emit text_char_format_changed(layer_->id, format, mask);
        }
    } else if (key.contains(QStringLiteral("type")) ||
               key.contains(QStringLiteral("text flow")) ||
               key.contains(QStringLiteral("dynamic"))) {
        if (text_like) {
            begin_text_property_change();
            RichTextCharFormat format = rich_text_char_format_from_layer(defaults);
            const uint32_t mask = RichTextCharTextStyle |
                RichTextCharUnderline | RichTextCharStrikethrough |
                RichTextCharLigatures | RichTextCharStylisticAlternates |
                RichTextCharFractions | RichTextCharOpenTypeFeatures |
                RichTextCharLanguage;
            apply_rich_text_format_to_layer_range(
                *layer_, format, mask, active_text_selection);
            emit text_char_format_changed(layer_->id, format, mask);
        }
        layer_->text_overflow_mode = defaults.text_overflow_mode;
        layer_->text_fit_min_scale = defaults.text_fit_min_scale;
        layer_->ticker_playback_mode = defaults.ticker_playback_mode;
        layer_->ticker_completion = defaults.ticker_completion;
        set_animated_value(layer_->ticker_completion_prop, t,
                           defaults.ticker_completion);
        layer_->ticker_style = defaults.ticker_style;
        layer_->ticker_speed = defaults.ticker_speed;
        layer_->ticker_line_hold = defaults.ticker_line_hold;
        layer_->ticker_direction = defaults.ticker_direction;
    } else if (key.contains(QStringLiteral("paragraph"))) {
        if (!text_like) {
            changed = false;
        } else {
            begin_text_property_change();
            RichTextParagraphFormat paragraph =
                defaults.rich_text.empty()
                    ? rich_text_document_from_layer_defaults(defaults)
                          .default_paragraph_format
                    : defaults.rich_text.default_paragraph_format;
            apply_rich_text_paragraph_format_to_layer(
                *layer_, paragraph, RichTextParagraphAll,
                active_text_selection);
            if (!active_text_selection) {
                set_animated_value(layer_->paragraph_indent_left_prop, t,
                                   paragraph.indent_left);
                set_animated_value(layer_->paragraph_indent_right_prop, t,
                                   paragraph.indent_right);
                set_animated_value(layer_->paragraph_indent_first_line_prop, t,
                                   paragraph.indent_first_line);
                set_animated_value(layer_->paragraph_space_before_prop, t,
                                   paragraph.space_before);
                set_animated_value(layer_->paragraph_space_after_prop, t,
                                   paragraph.space_after);
            }
            emit text_char_format_changed(
                layer_->id, RichTextCharFormat(), 0);
        }
    } else if (key.contains(QStringLiteral("auto styling")) || key.contains(QStringLiteral("auto_style"))) {
        layer_->rich_text.auto_style_enabled = false;
        layer_->rich_text.auto_default_style_preset_id.clear();
    } else if (key.contains(QStringLiteral("live"))) {
        layer_->expose_text = defaults.expose_text;
        layer_->exposed_hide_if_empty = defaults.exposed_hide_if_empty;
        layer_->exposed_single_value = defaults.exposed_single_value;
        layer_->expose_fill_color = defaults.expose_fill_color;
        layer_->exposed_fill_single_value = defaults.exposed_fill_single_value;
        layer_->expose_stroke_color = defaults.expose_stroke_color;
        layer_->exposed_stroke_single_value = defaults.exposed_stroke_single_value;
        layer_->ignore_persistence = defaults.ignore_persistence;
        layer_->video_playback_mode = defaults.video_playback_mode;
        layer_->audio_independent = defaults.audio_independent;
    } else if (key.contains(QStringLiteral("bullet"))) {
        changed = false;
    } else if (key.contains(QStringLiteral("gradient"))) {
        if (text_like) {
            begin_text_property_change();
            RichTextCharFormat format = rich_text_char_format_from_layer(defaults);
            apply_rich_text_format_to_layer_range(
                *layer_, format, RichTextCharFillColor,
                active_text_selection);
            emit text_char_format_changed(
                layer_->id, format, RichTextCharFillColor);
        } else {
            layer_->gradient_type = defaults.gradient_type;
            layer_->gradient_spread = defaults.gradient_spread;
            layer_->gradient_start_color = defaults.gradient_start_color;
            layer_->gradient_end_color = defaults.gradient_end_color;
            layer_->gradient_start_pos = defaults.gradient_start_pos;
            layer_->gradient_end_pos = defaults.gradient_end_pos;
            layer_->gradient_start_opacity = defaults.gradient_start_opacity;
            layer_->gradient_end_opacity = defaults.gradient_end_opacity;
            layer_->gradient_opacity = defaults.gradient_opacity;
            layer_->gradient_angle = defaults.gradient_angle;
            layer_->gradient_center_x = defaults.gradient_center_x;
            layer_->gradient_center_y = defaults.gradient_center_y;
            layer_->gradient_scale = defaults.gradient_scale;
            layer_->gradient_focal_x = defaults.gradient_focal_x;
            layer_->gradient_focal_y = defaults.gradient_focal_y;
            layer_->gradient_stops.clear();
        }
    } else if (key.contains(QStringLiteral("image"))) {
        if (layer_type_is_image_like(layer_->type)) {
            layer_->scale_filter = defaults.scale_filter;
            layer_->image_box_mode = defaults.image_box_mode;
            layer_->image_crop_when_outside_box = defaults.image_crop_when_outside_box;
            layer_->image_anchor_x = defaults.image_anchor_x;
            layer_->image_anchor_y = defaults.image_anchor_y;
            layer_->image_box_lock_aspect_ratio = defaults.image_box_lock_aspect_ratio;
            layer_->lock_aspect_ratio = true;
            reset_shape_size_to_intrinsic_image();
        } else {
            changed = false;
        }
    } else if (key.contains(QStringLiteral("video"))) {
        if (layer_->type == LayerType::Video) {
            layer_->video_in_point = defaults.video_in_point;
            layer_->video_out_point = defaults.video_out_point;
            layer_->video_loop = defaults.video_loop;
            layer_->video_time_remap_enabled = defaults.video_time_remap_enabled;
            layer_->video_source_time = defaults.video_source_time;
            layer_->video_time_remap_loop_segments.clear();
            layer_->video_time_remap_audio_mode = defaults.video_time_remap_audio_mode;
            layer_->video_frame_interpolation = defaults.video_frame_interpolation;
            layer_->video_optical_flow_enabled = defaults.video_optical_flow_enabled;
            layer_->video_prefer_hardware_decode = defaults.video_prefer_hardware_decode;
            layer_->video_allow_hardware_fallback = defaults.video_allow_hardware_fallback;
            layer_->video_decode_cache_policy = defaults.video_decode_cache_policy;
            layer_->scale_filter = defaults.scale_filter;
            layer_->image_box_mode = defaults.image_box_mode;
            layer_->image_crop_when_outside_box = defaults.image_crop_when_outside_box;
            layer_->image_anchor_x = defaults.image_anchor_x;
            layer_->image_anchor_y = defaults.image_anchor_y;
        } else {
            changed = false;
        }
    } else if (key.contains(QStringLiteral("audio"))) {
        layer_->audio_in_point = defaults.audio_in_point;
        layer_->audio_out_point = defaults.audio_out_point;
        layer_->audio_volume = defaults.audio_volume;
        layer_->audio_pan = defaults.audio_pan;
        set_animated_value(layer_->audio_volume_prop, t, defaults.audio_volume);
        set_animated_value(layer_->audio_pan_prop, t, defaults.audio_pan);
        layer_->audio_muted = defaults.audio_muted;
        layer_->audio_solo = defaults.audio_solo;
        layer_->audio_fade_in = defaults.audio_fade_in;
        layer_->audio_fade_out = defaults.audio_fade_out;
        layer_->audio_fade_curve = defaults.audio_fade_curve;
        layer_->audio_loop = defaults.audio_loop;
        layer_->audio_playback_mode = defaults.audio_playback_mode;
        layer_->audio_independent = defaults.audio_independent;
    } else if (key.contains(QStringLiteral("asset"))) {
        layer_->asset_playback_mode = defaults.asset_playback_mode;
        layer_->asset_playback_offset = defaults.asset_playback_offset;
        layer_->asset_pause_duration = defaults.asset_pause_duration;
        layer_->asset_loop_count = defaults.asset_loop_count;
        layer_->asset_loop = defaults.asset_loop;
    } else if (key.contains(QStringLiteral("material"))) {
        layer_->material_accepts_lights = defaults.material_accepts_lights;
        layer_->material_casts_shadows = defaults.material_casts_shadows;
        layer_->material_accepts_shadows = defaults.material_accepts_shadows;
        layer_->material_appears_in_reflections =
            defaults.material_appears_in_reflections;
        set_animated_value(layer_->material_ambient, t,
                           defaults.material_ambient.evaluate(0.0));
        set_animated_value(layer_->material_diffuse, t,
                           defaults.material_diffuse.evaluate(0.0));
        set_animated_value(layer_->material_specular, t,
                           defaults.material_specular.evaluate(0.0));
        set_animated_value(layer_->material_shininess, t,
                           defaults.material_shininess.evaluate(0.0));
        set_animated_value(layer_->material_metallic, t,
                           defaults.material_metallic.evaluate(0.0));
        set_animated_value(layer_->material_roughness, t,
                           defaults.material_roughness.evaluate(0.0));
        set_animated_value(layer_->material_reflection_intensity, t,
                           defaults.material_reflection_intensity.evaluate(0.0));
    } else if (key.contains(QStringLiteral("geometry"))) {
        layer_->geometry_extrusion_enabled = defaults.geometry_extrusion_enabled;
        set_animated_value(layer_->geometry_extrusion_depth, t,
                           defaults.geometry_extrusion_depth.evaluate(0.0));
        set_animated_value(layer_->geometry_bevel_depth, t,
                           defaults.geometry_bevel_depth.evaluate(0.0));
        layer_->geometry_extrusion_segments = defaults.geometry_extrusion_segments;
        layer_->geometry_bevel_segments = defaults.geometry_bevel_segments;
        layer_->geometry_bevel_front = defaults.geometry_bevel_front;
        layer_->geometry_bevel_back = defaults.geometry_bevel_back;
    } else if (layer_->type == LayerType::Chart &&
               key.contains(QStringLiteral("chart_axes_grid"))) {
        layer_->chart_axes_enabled = defaults.chart_axes_enabled;
        layer_->chart_axis_labels_enabled = defaults.chart_axis_labels_enabled;
        layer_->chart_grid_enabled = defaults.chart_grid_enabled;
        layer_->chart_axis_color = defaults.chart_axis_color;
        layer_->chart_grid_color = defaults.chart_grid_color;
    } else if (layer_->type == LayerType::Chart &&
               key.contains(QStringLiteral("chart_labels_legend"))) {
        layer_->chart_data_labels_enabled = defaults.chart_data_labels_enabled;
        layer_->chart_legend_enabled = defaults.chart_legend_enabled;
        layer_->chart_thousands_separator = defaults.chart_thousands_separator;
        layer_->chart_decimal_precision = defaults.chart_decimal_precision;
        layer_->chart_number_prefix = defaults.chart_number_prefix;
        layer_->chart_number_suffix = defaults.chart_number_suffix;
        layer_->chart_font_family = defaults.chart_font_family;
        layer_->chart_label_color = defaults.chart_label_color;
        set_animated_value(layer_->chart_font_size, t,
                           defaults.chart_font_size.evaluate(0.0));
    } else if (layer_->type == LayerType::Chart &&
               key.contains(QStringLiteral("chart_appearance"))) {
        layer_->chart_background_enabled = defaults.chart_background_enabled;
        layer_->chart_fill_enabled = defaults.chart_fill_enabled;
        layer_->chart_outline_enabled = defaults.chart_outline_enabled;
        layer_->chart_background_color = defaults.chart_background_color;
        set_animated_value(layer_->chart_line_thickness, t,
                           defaults.chart_line_thickness.evaluate(0.0));
        set_animated_value(layer_->chart_bar_width, t,
                           defaults.chart_bar_width.evaluate(0.0));
        set_animated_value(layer_->chart_bar_spacing, t,
                           defaults.chart_bar_spacing.evaluate(0.0));
        set_animated_value(layer_->chart_corner_radius, t,
                           defaults.chart_corner_radius.evaluate(0.0));
        set_animated_value(layer_->chart_padding, t,
                           defaults.chart_padding.evaluate(0.0));
        set_animated_value(layer_->chart_margin_left, t,
                           defaults.chart_margin_left.evaluate(0.0));
        set_animated_value(layer_->chart_margin_top, t,
                           defaults.chart_margin_top.evaluate(0.0));
        set_animated_value(layer_->chart_margin_right, t,
                           defaults.chart_margin_right.evaluate(0.0));
        set_animated_value(layer_->chart_margin_bottom, t,
                           defaults.chart_margin_bottom.evaluate(0.0));
    } else if (layer_->type == LayerType::Chart &&
               key.contains(QStringLiteral("chart_animation"))) {
        set_animated_value(layer_->chart_animation_progress, t,
                           defaults.chart_animation_progress.evaluate(0.0));
        set_animated_value(layer_->chart_reveal_progress, t,
                           defaults.chart_reveal_progress.evaluate(0.0));
        set_animated_value(layer_->chart_value_transition, t,
                           defaults.chart_value_transition.evaluate(0.0));
        set_animated_value(layer_->chart_label_opacity, t,
                           defaults.chart_label_opacity.evaluate(0.0));
        set_animated_value(layer_->chart_element_opacity, t,
                           defaults.chart_element_opacity.evaluate(0.0));
        layer_->chart_data_transition_seconds =
            defaults.chart_data_transition_seconds;
    } else if (key.contains(QStringLiteral("appearance"))) {
        if (text_like) {
            begin_text_property_change();
            RichTextCharFormat format = rich_text_char_format_from_layer(defaults);
            const uint32_t mask = RichTextCharFillColor | RichTextCharStroke;
            apply_rich_text_format_to_layer_range(
                *layer_, format, mask, active_text_selection);
            emit text_char_format_changed(layer_->id, format, mask);
        } else {
            layer_->fill_color = defaults.fill_color;
            layer_->stroke_color = defaults.stroke_color;
            layer_->stroke_width = defaults.stroke_width;
            set_animated_value(layer_->fill_color_a, t,
                               defaults.fill_color_a.evaluate(0.0));
            set_animated_value(layer_->fill_color_r, t,
                               defaults.fill_color_r.evaluate(0.0));
            set_animated_value(layer_->fill_color_g, t,
                               defaults.fill_color_g.evaluate(0.0));
            set_animated_value(layer_->fill_color_b, t,
                               defaults.fill_color_b.evaluate(0.0));
            set_animated_value(layer_->stroke_color_a, t,
                               defaults.stroke_color_a.evaluate(0.0));
            set_animated_value(layer_->stroke_color_r, t,
                               defaults.stroke_color_r.evaluate(0.0));
            set_animated_value(layer_->stroke_color_g, t,
                               defaults.stroke_color_g.evaluate(0.0));
            set_animated_value(layer_->stroke_color_b, t,
                               defaults.stroke_color_b.evaluate(0.0));
        }
        layer_->stroke_offset = defaults.stroke_offset;
        set_animated_value(layer_->stroke_offset_prop, t, defaults.stroke_offset);
        set_animated_value(layer_->opacity, t, defaults.opacity.evaluate(0.0));
        set_animated_value(layer_->material_emissive_color_a, t,
                           defaults.material_emissive_color_a.evaluate(0.0));
        set_animated_value(layer_->material_emissive_color_r, t,
                           defaults.material_emissive_color_r.evaluate(0.0));
        set_animated_value(layer_->material_emissive_color_g, t,
                           defaults.material_emissive_color_g.evaluate(0.0));
        set_animated_value(layer_->material_emissive_color_b, t,
                           defaults.material_emissive_color_b.evaluate(0.0));
        mirror_material_emissive_color(*layer_, t);
        set_animated_value(layer_->material_emissive_intensity, t,
                           defaults.material_emissive_intensity.evaluate(0.0));
    } else if (key.contains(QStringLiteral("animator"))) {
        layer_->text_animators = TextAnimatorStack();
    } else {
        changed = false;
    }

    if (!changed)
        return;
    load_values();
    emit property_changed(true);
}
