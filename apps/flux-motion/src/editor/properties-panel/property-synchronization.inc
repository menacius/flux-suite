    connect(btn_kf_appearance_fill_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        if (active_text_edit_layer_id_ == layer_->id &&
            (layer_->type == LayerType::Text || layer_->type == LayerType::Clock ||
             layer_->type == LayerType::Ticker))
            return;
        const bool text_fill = layer_->type == LayerType::Text || layer_->type == LayerType::Clock ||
                               layer_->type == LayerType::Ticker;
        double t = local_time();
        uint32_t color = text_fill ? eval_text_color(*layer_, t) : eval_fill_color(*layer_, t);
        auto &a = text_fill ? layer_->text_color_a : layer_->fill_color_a;
        auto &r = text_fill ? layer_->text_color_r : layer_->fill_color_r;
        auto &g = text_fill ? layer_->text_color_g : layer_->fill_color_g;
        auto &b = text_fill ? layer_->text_color_b : layer_->fill_color_b;
        if (any_keyframe_at_time({&a, &r, &g, &b}, t)) {
            remove_keyframe_at(a, t);
            remove_keyframe_at(r, t);
            remove_keyframe_at(g, t);
            remove_keyframe_at(b, t);
        } else {
            add_or_replace_keyframe(a, t, (color >> 24) & 0xFF);
            add_or_replace_keyframe(r, t, (color >> 16) & 0xFF);
            add_or_replace_keyframe(g, t, (color >> 8) & 0xFF);
            add_or_replace_keyframe(b, t, color & 0xFF);
        }
        load_values();
        emit_change();
    });
    connect(btn_kf_appearance_stroke_, &QPushButton::clicked, this,
            [this, can_edit, local_time, emit_change]() {
                if (!can_edit()) return;
                if (active_text_edit_layer_id_ == layer_->id &&
                    (layer_->type == LayerType::Text ||
                     layer_->type == LayerType::Clock ||
                     layer_->type == LayerType::Ticker))
                    return;
                const double t = local_time();
                const uint32_t color = eval_stroke_color(*layer_, t);
                if (any_keyframe_at_time(
                        {&layer_->stroke_color_a, &layer_->stroke_color_r,
                         &layer_->stroke_color_g, &layer_->stroke_color_b}, t)) {
                    remove_keyframe_at(layer_->stroke_color_a, t);
                    remove_keyframe_at(layer_->stroke_color_r, t);
                    remove_keyframe_at(layer_->stroke_color_g, t);
                    remove_keyframe_at(layer_->stroke_color_b, t);
                } else {
                    add_or_replace_keyframe(layer_->stroke_color_a, t,
                                            (color >> 24) & 0xFF);
                    add_or_replace_keyframe(layer_->stroke_color_r, t,
                                            (color >> 16) & 0xFF);
                    add_or_replace_keyframe(layer_->stroke_color_g, t,
                                            (color >> 8) & 0xFF);
                    add_or_replace_keyframe(layer_->stroke_color_b, t,
                                            color & 0xFF);
                }
                load_values();
                emit_change();
            });
    auto connect_material_keyframe = [this, can_edit, local_time, emit_change](
                                         QPushButton *button,
                                         AnimatedProperty Layer::*member,
                                         QDoubleSpinBox *spin) {
        connect(button, &QPushButton::clicked, this,
                [this, can_edit, local_time, emit_change, member, spin]() {
                    if (!can_edit()) return;
                    AnimatedProperty &property = layer_.get()->*member;
                    toggle_keyframe(property, local_time(), spin->value());
                    load_values();
                    emit_change();
                });
    };
    connect_material_keyframe(btn_kf_material_ambient_, &Layer::material_ambient, spn_material_ambient_);
    connect_material_keyframe(btn_kf_material_diffuse_, &Layer::material_diffuse, spn_material_diffuse_);
    connect_material_keyframe(btn_kf_material_specular_, &Layer::material_specular, spn_material_specular_);
    connect_material_keyframe(btn_kf_material_shininess_, &Layer::material_shininess, spn_material_shininess_);
    connect_material_keyframe(btn_kf_material_metallic_, &Layer::material_metallic, spn_material_metallic_);
    connect_material_keyframe(btn_kf_material_roughness_, &Layer::material_roughness, spn_material_roughness_);
    connect_material_keyframe(btn_kf_material_reflection_, &Layer::material_reflection_intensity, spn_material_reflection_);
    connect_material_keyframe(btn_kf_material_emissive_intensity_, &Layer::material_emissive_intensity, spn_material_emissive_intensity_);
    connect_material_keyframe(btn_kf_geometry_extrusion_depth_, &Layer::geometry_extrusion_depth, spn_geometry_extrusion_depth_);
    connect_material_keyframe(btn_kf_geometry_bevel_depth_, &Layer::geometry_bevel_depth, spn_geometry_bevel_depth_);

    connect(btn_kf_material_emissive_color_, &QPushButton::clicked, this,
            [this, can_edit, local_time, emit_change]() {
                if (!can_edit()) return;
                const double t = local_time();
                const uint32_t color = evaluated_material_emissive_color(*layer_, t);
                if (any_keyframe_at_time(
                        {&layer_->material_emissive_color_a,
                         &layer_->material_emissive_color_r,
                         &layer_->material_emissive_color_g,
                         &layer_->material_emissive_color_b}, t)) {
                    remove_keyframe_at(layer_->material_emissive_color_a, t);
                    remove_keyframe_at(layer_->material_emissive_color_r, t);
                    remove_keyframe_at(layer_->material_emissive_color_g, t);
                    remove_keyframe_at(layer_->material_emissive_color_b, t);
                } else {
                    add_or_replace_keyframe(layer_->material_emissive_color_a, t, (color >> 24) & 0xFF);
                    add_or_replace_keyframe(layer_->material_emissive_color_r, t, (color >> 16) & 0xFF);
                    add_or_replace_keyframe(layer_->material_emissive_color_g, t, (color >> 8) & 0xFF);
                    add_or_replace_keyframe(layer_->material_emissive_color_b, t, color & 0xFF);
                }
                mirror_material_emissive_color(*layer_, t);
                load_values();
                emit_change();
            });

    connect(btn_kf_fill_color_, &QPushButton::clicked, this, [this, can_edit, local_time, emit_change]() {
        if (!can_edit()) return;
        double t = local_time();
        uint32_t color = eval_fill_color(*layer_, t);
        if (any_keyframe_at_time({&layer_->fill_color_a, &layer_->fill_color_r,
                                  &layer_->fill_color_g, &layer_->fill_color_b}, t)) {
            remove_keyframe_at(layer_->fill_color_a, t);
            remove_keyframe_at(layer_->fill_color_r, t);
            remove_keyframe_at(layer_->fill_color_g, t);
            remove_keyframe_at(layer_->fill_color_b, t);
        } else {
            add_or_replace_keyframe(layer_->fill_color_a, t, (color >> 24) & 0xFF);
            add_or_replace_keyframe(layer_->fill_color_r, t, (color >> 16) & 0xFF);
            add_or_replace_keyframe(layer_->fill_color_g, t, (color >> 8) & 0xFF);
            add_or_replace_keyframe(layer_->fill_color_b, t, color & 0xFF);
        }
        load_values();
        emit_change();
    });

    auto apply_rule_editor_to_rule = [this](RichTextAutoStyleRule &rule) {
        rule.enabled = chk_auto_rule_enabled_ ? chk_auto_rule_enabled_->isChecked() : true;
        rule.display_name = edt_auto_rule_name_ ? edt_auto_rule_name_->text().trimmed().toStdString() : std::string();
        rule.style_preset_id = cmb_auto_rule_style_->currentData().toString().toStdString();
        rule.conflict_mode = cmb_auto_rule_conflict_mode_ ? cmb_auto_rule_conflict_mode_->currentData().toString().toStdString() : std::string("override_previous");
        rule.match_mode = cmb_auto_rule_match_mode_ ? cmb_auto_rule_match_mode_->currentData().toString().toStdString() : std::string("all_matches");
        rule.generalization_mode = cmb_auto_rule_generalization_ ? cmb_auto_rule_generalization_->currentData().toString().toStdString() : std::string("auto_merge");
        rule.prevent_duplicates = chk_auto_rule_prevent_duplicates_ ? chk_auto_rule_prevent_duplicates_->isChecked() : true;
        rule.allow_multiple_cases = chk_auto_rule_allow_multiple_cases_ ? chk_auto_rule_allow_multiple_cases_->isChecked() : true;
        if (rule.allow_multiple_cases) rule.match_mode = "all_matches";
        rule.stop_processing = chk_auto_rule_stop_processing_ ? chk_auto_rule_stop_processing_->isChecked() : false;
        rule.require_stop_match = chk_auto_rule_require_stop_match_ ? chk_auto_rule_require_stop_match_->isChecked() : true;
        rule.include_start_marker = chk_auto_rule_include_start_marker_ ? chk_auto_rule_include_start_marker_->isChecked() : true;
        rule.include_end_marker = chk_auto_rule_include_end_marker_ ? chk_auto_rule_include_end_marker_->isChecked() : false;
        rule.excludes_rule_ids = edt_auto_rule_excludes_ ? auto_style_split_ids(edt_auto_rule_excludes_->text()) : std::vector<std::string>();

        /* Learned rules are regex-backed. The marker controls are only a
         * friendly summary for those rules and must not replace the inferred
         * regex when Save Rule is pressed. Replacing condition_type here made
         * the rule stop matching, which exposed only the base style and looked
         * as though Save had applied the base preset. */
        if (rule.condition_type != "regex") {
            rule.condition_type = "range_markers";
            rule.start_condition = cmb_auto_rule_start_condition_->currentData().toString().toStdString();
            rule.end_condition = cmb_auto_rule_end_condition_->currentData().toString().toStdString();
            rule.start_offset = (size_t)spn_auto_rule_start_offset_->value();
            rule.end_offset = (size_t)spn_auto_rule_end_offset_->value();
            rule.start_custom_chars = edt_auto_rule_start_chars_->text().toStdString();
            rule.end_custom_chars = edt_auto_rule_end_chars_->text().toStdString();
            rule.start = rule.start_offset;
            rule.length = rule.end_offset > rule.start_offset ? rule.end_offset - rule.start_offset : 0;
        }
    };
    auto load_rule_to_editor = [this](const RichTextAutoStyleRule &rule) {
        /* Loading one rule must never leak the previous rule's widget state or
         * trigger write-back handlers. In particular, learned rules have an
         * empty preset id and use their embedded cached style. */
        const QSignalBlocker block_enabled(chk_auto_rule_enabled_);
        const QSignalBlocker block_name(edt_auto_rule_name_);
        const QSignalBlocker block_style(cmb_auto_rule_style_);
        const QSignalBlocker block_start(cmb_auto_rule_start_condition_);
        const QSignalBlocker block_end(cmb_auto_rule_end_condition_);
        const QSignalBlocker block_start_offset(spn_auto_rule_start_offset_);
        const QSignalBlocker block_end_offset(spn_auto_rule_end_offset_);
        const QSignalBlocker block_start_chars(edt_auto_rule_start_chars_);
        const QSignalBlocker block_end_chars(edt_auto_rule_end_chars_);
        const QSignalBlocker block_conflict(cmb_auto_rule_conflict_mode_);
        const QSignalBlocker block_match(cmb_auto_rule_match_mode_);
        const QSignalBlocker block_generalization(cmb_auto_rule_generalization_);
        const QSignalBlocker block_duplicates(chk_auto_rule_prevent_duplicates_);
        const QSignalBlocker block_multiple(chk_auto_rule_allow_multiple_cases_);
        const QSignalBlocker block_excludes(edt_auto_rule_excludes_);
        const QSignalBlocker block_stop(chk_auto_rule_stop_processing_);
        const QSignalBlocker block_require(chk_auto_rule_require_stop_match_);
        const QSignalBlocker block_include_start(chk_auto_rule_include_start_marker_);
        const QSignalBlocker block_include_end(chk_auto_rule_include_end_marker_);
        if (chk_auto_rule_enabled_) chk_auto_rule_enabled_->setChecked(rule.enabled);
        if (edt_auto_rule_name_) edt_auto_rule_name_->setText(QString::fromStdString(rule.display_name));
        int idx = cmb_auto_rule_style_->findData(QString::fromStdString(rule.style_preset_id));
        if (idx < 0 && !rule.style_preset_id.empty()) {
            const QString missing_id = QString::fromStdString(rule.style_preset_id);
            cmb_auto_rule_style_->addItem(QStringLiteral("Missing preset — %1").arg(missing_id), missing_id);
            idx = cmb_auto_rule_style_->count() - 1;
        }
        if (idx < 0) idx = 0;
        cmb_auto_rule_style_->setCurrentIndex(idx);
        int sidx = cmb_auto_rule_start_condition_->findData(QString::fromStdString(rule.start_condition.empty() ? std::string("text_start") : rule.start_condition));
        int eidx = cmb_auto_rule_end_condition_->findData(QString::fromStdString(rule.end_condition.empty() ? std::string("character_index") : rule.end_condition));
        if (sidx >= 0) cmb_auto_rule_start_condition_->setCurrentIndex(sidx);
        if (eidx >= 0) cmb_auto_rule_end_condition_->setCurrentIndex(eidx);
        spn_auto_rule_start_offset_->setValue((int)rule.start_offset);
        spn_auto_rule_end_offset_->setValue((int)(rule.end_offset ? rule.end_offset : rule.length));
        edt_auto_rule_start_chars_->setText(QString::fromStdString(rule.start_custom_chars));
        edt_auto_rule_end_chars_->setText(QString::fromStdString(rule.end_custom_chars));
        if (cmb_auto_rule_conflict_mode_) { int cidx = cmb_auto_rule_conflict_mode_->findData(QString::fromStdString(rule.conflict_mode.empty() ? std::string("override_previous") : rule.conflict_mode)); if (cidx >= 0) cmb_auto_rule_conflict_mode_->setCurrentIndex(cidx); }
        if (cmb_auto_rule_match_mode_) { int midx = cmb_auto_rule_match_mode_->findData(QString::fromStdString(rule.match_mode.empty() ? std::string("all_matches") : rule.match_mode)); if (midx >= 0) cmb_auto_rule_match_mode_->setCurrentIndex(midx); }
        if (cmb_auto_rule_generalization_) { int gidx = cmb_auto_rule_generalization_->findData(QString::fromStdString(rule.generalization_mode.empty() ? std::string("auto_merge") : rule.generalization_mode)); if (gidx >= 0) cmb_auto_rule_generalization_->setCurrentIndex(gidx); }
        if (chk_auto_rule_prevent_duplicates_) chk_auto_rule_prevent_duplicates_->setChecked(rule.prevent_duplicates);
        if (chk_auto_rule_allow_multiple_cases_) chk_auto_rule_allow_multiple_cases_->setChecked(rule.allow_multiple_cases);
        if (edt_auto_rule_excludes_) edt_auto_rule_excludes_->setText(auto_style_join_ids(rule.excludes_rule_ids));
        if (chk_auto_rule_stop_processing_) chk_auto_rule_stop_processing_->setChecked(rule.stop_processing);
        if (chk_auto_rule_require_stop_match_) chk_auto_rule_require_stop_match_->setChecked(rule.require_stop_match);
        if (chk_auto_rule_include_start_marker_) chk_auto_rule_include_start_marker_->setChecked(rule.include_start_marker);
        if (chk_auto_rule_include_end_marker_) chk_auto_rule_include_end_marker_->setChecked(rule.include_end_marker);
        if (spn_auto_rule_chars_) spn_auto_rule_chars_->setValue((int)(rule.end_offset ? rule.end_offset : rule.length));
    };
    auto refresh_auto_rule_list = [this]() {
        if (!lst_auto_style_rules_ || !layer_) return;
        obsfxm::StylePresetLibrary library;
        const int selected = lst_auto_style_rules_->currentRow();
        auto populate = [&](QListWidget *list) {
            if (!list) return;
            QSignalBlocker blocker(list);
            list->clear();
            int i = 1;
            for (const auto &rule : layer_->rich_text.auto_style_rules) {
                const QString preset_name = library.displayNameForId(QString::fromStdString(rule.style_preset_id));
                const QString display_name = auto_style_rule_descriptive_name(rule);
                const QString rule_id = QString::fromStdString(rule.rule_id.empty() ? std::to_string(i) : rule.rule_id);
                const QString details = rule.condition_type == "regex"
                    ? QString::fromStdString(rule.regex_pattern)
                    : QStringLiteral("%1 → %2").arg(
                        auto_style_marker_label(rule.start_condition, rule.start_offset, rule.start_custom_chars),
                        auto_style_marker_label(rule.end_condition, rule.end_offset ? rule.end_offset : rule.length, rule.end_custom_chars));
                auto *item = new QListWidgetItem(QStringLiteral("%1. %2%3")
                                                 .arg(i++).arg(display_name)
                                                 .arg(rule.enabled ? QString() : QStringLiteral("  [off]")), list);
                item->setData(Qt::UserRole, i - 2);
                item->setToolTip(QStringLiteral("Rule ID: %1\nPreset: %2\nPattern: %3\n%4")
                                 .arg(rule_id, preset_name, details,
                                      rule.enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled")));
            }
            if (selected >= 0 && selected < list->count()) list->setCurrentRow(selected);
        };
        populate(lst_auto_style_rules_);
        populate(lst_auto_rules_dialog_);
    };
    auto cache_text_preset_format = [](const std::string &preset_id, RichTextCharFormat &format, uint32_t &mask) {
        mask = 0;
        if (preset_id.empty()) return;
        obsfxm::StylePresetLibrary library;
        obsfxm::StylePreset preset;
        if (library.findById(QString::fromStdString(preset_id), &preset) &&
            preset.kind == obsfxm::StylePresetKind::Text &&
            obsfxm::StylePresetLibrary::textPresetToCharFormat(preset, format))
            mask = obsfxm::StylePresetLibrary::textPresetCharMask();
    };
    auto cache_rule_format = [cache_text_preset_format](RichTextAutoStyleRule &rule) {
        /* Learned rules carry their inferred manual format directly. Only a
         * user-selected preset should replace that cached format. */
        if (!rule.style_preset_id.empty())
            cache_text_preset_format(rule.style_preset_id, rule.cached_format, rule.cached_mask);
    };
    auto cache_default_auto_style = [this, cache_text_preset_format]() {
        if (!layer_) return;
        cache_text_preset_format(layer_->rich_text.auto_default_style_preset_id,
                                 layer_->rich_text.auto_default_style_cached_format,
                                 layer_->rich_text.auto_default_style_cached_mask);
    };
    auto invalidate_auto_text_styling = [this, cache_default_auto_style, cache_rule_format]() {
        if (!layer_) return;
        if (layer_->rich_text.empty())
            layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);
        if (layer_->rich_text.plain_text != layer_->text_content) {
            const RichTextCharFormat insertion_format =
                insertion_format_for_text_replace(layer_->rich_text);
            const uint32_t insertion_mask =
                insertion_mask_for_text_replace(layer_->rich_text);
            rich_text_document_replace_text(layer_->rich_text, layer_->text_content,
                                            insertion_format, insertion_mask);
        }
        cache_default_auto_style();
        for (auto &rule : layer_->rich_text.auto_style_rules)
            cache_rule_format(rule);
        layer_->rich_text.normalize();
    };
    auto format_to_json = [](const RichTextCharFormat &f) {
        auto stops_to_json = [](const std::vector<GradientStop> &stops) {
            QJsonArray result;
            for (const GradientStop &stop : stops) {
                result.append(QJsonObject{{"color", (qint64)stop.color},
                                          {"position", stop.position},
                                          {"opacity", stop.opacity}});
            }
            return result;
        };
        QJsonObject fill{{"type", f.fill.type}, {"color", (qint64)f.fill.color},
                         {"gradient_type", f.fill.gradient_type}, {"gradient_spread", f.fill.gradient_spread},
                         {"gradient_start_color", (qint64)f.fill.gradient_start_color},
                         {"gradient_end_color", (qint64)f.fill.gradient_end_color},
                         {"gradient_start_pos", f.fill.gradient_start_pos}, {"gradient_end_pos", f.fill.gradient_end_pos},
                         {"gradient_start_opacity", f.fill.gradient_start_opacity}, {"gradient_end_opacity", f.fill.gradient_end_opacity},
                         {"gradient_opacity", f.fill.gradient_opacity}, {"gradient_angle", f.fill.gradient_angle},
                         {"gradient_center_x", f.fill.gradient_center_x}, {"gradient_center_y", f.fill.gradient_center_y},
                         {"gradient_scale", f.fill.gradient_scale}, {"gradient_focal_x", f.fill.gradient_focal_x},
                         {"gradient_focal_y", f.fill.gradient_focal_y},
                         {"gradient_stops", stops_to_json(f.fill.gradient_stops)}};
        QJsonObject stroke{{"enabled", f.stroke.enabled}, {"width", f.stroke.width}, {"opacity", f.stroke.opacity},
                           {"on_front", f.stroke.on_front}, {"alignment", f.stroke.alignment},
                           {"antialias", f.stroke.antialias}, {"join_style", f.stroke.join_style}};
        RichTextCharFormat sf; sf.fill = f.stroke.fill;
        QJsonObject stroke_fill{{"type", sf.fill.type}, {"color", (qint64)sf.fill.color},
                                {"gradient_type", sf.fill.gradient_type}, {"gradient_spread", sf.fill.gradient_spread},
                                {"gradient_start_color", (qint64)sf.fill.gradient_start_color},
                                {"gradient_end_color", (qint64)sf.fill.gradient_end_color},
                                {"gradient_start_pos", sf.fill.gradient_start_pos}, {"gradient_end_pos", sf.fill.gradient_end_pos},
                                {"gradient_start_opacity", sf.fill.gradient_start_opacity}, {"gradient_end_opacity", sf.fill.gradient_end_opacity},
                                {"gradient_opacity", sf.fill.gradient_opacity}, {"gradient_angle", sf.fill.gradient_angle},
                                {"gradient_center_x", sf.fill.gradient_center_x}, {"gradient_center_y", sf.fill.gradient_center_y},
                                {"gradient_scale", sf.fill.gradient_scale}, {"gradient_focal_x", sf.fill.gradient_focal_x},
                                {"gradient_focal_y", sf.fill.gradient_focal_y},
                                {"gradient_stops", stops_to_json(sf.fill.gradient_stops)}};
        stroke.insert("fill", stroke_fill);
        return QJsonObject{{"font_family", QString::fromStdString(f.font_family)}, {"font_style", QString::fromStdString(f.font_style)},
                           {"font_size", f.font_size}, {"bold", f.bold}, {"italic", f.italic}, {"underline", f.underline},
                           {"strikethrough", f.strikethrough}, {"kerning", f.kerning}, {"kerning_mode", f.kerning_mode},
                           {"manual_kerning", f.manual_kerning}, {"tracking", f.tracking}, {"scale_x", f.scale_x},
                           {"scale_y", f.scale_y}, {"baseline_shift", f.baseline_shift}, {"text_style", f.text_style},
                           {"ligatures", f.ligatures}, {"stylistic_alternates", f.stylistic_alternates},
                           {"fractions", f.fractions}, {"opentype_features", f.opentype_features},
                           {"language", QString::fromStdString(f.language)}, {"fill", fill}, {"stroke", stroke}};
    };
    auto fill_from_json = [](const QJsonObject &o, RichTextFill &f) {
        f.type = o.value("type").toInt(f.type); f.color = (uint32_t)o.value("color").toVariant().toULongLong();
        f.gradient_type = o.value("gradient_type").toInt(f.gradient_type); f.gradient_spread = o.value("gradient_spread").toInt(f.gradient_spread);
        f.gradient_start_color = (uint32_t)o.value("gradient_start_color").toVariant().toULongLong();
        f.gradient_end_color = (uint32_t)o.value("gradient_end_color").toVariant().toULongLong();
        f.gradient_start_pos = (float)o.value("gradient_start_pos").toDouble(f.gradient_start_pos);
        f.gradient_end_pos = (float)o.value("gradient_end_pos").toDouble(f.gradient_end_pos);
        f.gradient_start_opacity = (float)o.value("gradient_start_opacity").toDouble(f.gradient_start_opacity);
        f.gradient_end_opacity = (float)o.value("gradient_end_opacity").toDouble(f.gradient_end_opacity);
        f.gradient_opacity = (float)o.value("gradient_opacity").toDouble(f.gradient_opacity);
        f.gradient_angle = (float)o.value("gradient_angle").toDouble(f.gradient_angle);
        f.gradient_center_x = (float)o.value("gradient_center_x").toDouble(f.gradient_center_x);
        f.gradient_center_y = (float)o.value("gradient_center_y").toDouble(f.gradient_center_y);
        f.gradient_scale = (float)o.value("gradient_scale").toDouble(f.gradient_scale);
        f.gradient_focal_x = (float)o.value("gradient_focal_x").toDouble(f.gradient_focal_x);
        f.gradient_focal_y = (float)o.value("gradient_focal_y").toDouble(f.gradient_focal_y);
        if (o.contains("gradient_stops")) {
            f.gradient_stops.clear();
            for (const QJsonValue &entry : o.value("gradient_stops").toArray()) {
                const QJsonObject item = entry.toObject();
                GradientStop stop;
                stop.color = (uint32_t)item.value("color").toVariant().toULongLong();
                stop.position = std::clamp(
                    (float)item.value("position").toDouble(0.5), 0.0f, 1.0f);
                stop.opacity = std::clamp(
                    (float)item.value("opacity").toDouble(1.0), 0.0f, 1.0f);
                f.gradient_stops.push_back(stop);
            }
        }
    };
    auto format_from_json = [fill_from_json](const QJsonObject &o, RichTextCharFormat f) {
        f.font_family = o.value("font_family").toString(QString::fromStdString(f.font_family)).toStdString();
        f.font_style = o.value("font_style").toString(QString::fromStdString(f.font_style)).toStdString();
        f.font_size = o.value("font_size").toInt(f.font_size); f.bold = o.value("bold").toBool(f.bold);
        f.italic = o.value("italic").toBool(f.italic); f.underline = o.value("underline").toBool(f.underline);
        f.strikethrough = o.value("strikethrough").toBool(f.strikethrough); f.kerning = o.value("kerning").toBool(f.kerning);
        f.kerning_mode = o.value("kerning_mode").toInt(f.kerning_mode); f.manual_kerning = (float)o.value("manual_kerning").toDouble(f.manual_kerning);
        f.tracking = (float)o.value("tracking").toDouble(f.tracking); f.scale_x = (float)o.value("scale_x").toDouble(f.scale_x);
        f.scale_y = (float)o.value("scale_y").toDouble(f.scale_y); f.baseline_shift = (float)o.value("baseline_shift").toDouble(f.baseline_shift);
        f.text_style = o.value("text_style").toInt(f.text_style); f.ligatures = o.value("ligatures").toBool(f.ligatures);
        f.stylistic_alternates = o.value("stylistic_alternates").toBool(f.stylistic_alternates);
        f.fractions = o.value("fractions").toBool(f.fractions); f.opentype_features = o.value("opentype_features").toBool(f.opentype_features);
        f.language = o.value("language").toString(QString::fromStdString(f.language)).toStdString();
        fill_from_json(o.value("fill").toObject(), f.fill);
        const QJsonObject stroke = o.value("stroke").toObject();
        f.stroke.enabled = stroke.value("enabled").toBool(f.stroke.enabled); f.stroke.width = (float)stroke.value("width").toDouble(f.stroke.width);
        f.stroke.opacity = (float)stroke.value("opacity").toDouble(f.stroke.opacity); f.stroke.on_front = stroke.value("on_front").toBool(f.stroke.on_front);
        f.stroke.alignment = stroke.value("alignment").toInt(f.stroke.alignment); f.stroke.antialias = stroke.value("antialias").toBool(f.stroke.antialias);
        f.stroke.join_style = stroke.value("join_style").toInt(f.stroke.join_style); fill_from_json(stroke.value("fill").toObject(), f.stroke.fill);
        return f;
    };
    auto rule_to_json = [format_to_json](const RichTextAutoStyleRule &r) {
        QJsonArray excludes; for (const auto &id : r.excludes_rule_ids) excludes.append(QString::fromStdString(id));
        return QJsonObject{{"pattern_schema_version", fxm::serialization::kCurrentPatternSchemaVersion},
            {"enabled", r.enabled}, {"style_preset_id", QString::fromStdString(r.style_preset_id)},
            {"rule_id", QString::fromStdString(r.rule_id)}, {"display_name", QString::fromStdString(r.display_name)},
            {"conflict_mode", QString::fromStdString(r.conflict_mode)}, {"match_mode", QString::fromStdString(r.match_mode)},
            {"stop_processing", r.stop_processing}, {"excludes_rule_ids", excludes}, {"condition_type", QString::fromStdString(r.condition_type)},
            {"regex_pattern", QString::fromStdString(r.regex_pattern)}, {"regex_capture_group", (qint64)r.regex_capture_group},
            {"regex_case_sensitive", r.regex_case_sensitive}, {"generalization_mode", QString::fromStdString(r.generalization_mode)}, {"prevent_duplicates", r.prevent_duplicates}, {"allow_multiple_cases", r.allow_multiple_cases}, {"start_condition", QString::fromStdString(r.start_condition)},
            {"end_condition", QString::fromStdString(r.end_condition)}, {"start_offset", (qint64)r.start_offset},
            {"end_offset", (qint64)r.end_offset}, {"start_custom_chars", QString::fromStdString(r.start_custom_chars)},
            {"end_custom_chars", QString::fromStdString(r.end_custom_chars)}, {"require_stop_match", r.require_stop_match},
            {"include_start_marker", r.include_start_marker}, {"include_end_marker", r.include_end_marker},
            {"start", (qint64)r.start}, {"length", (qint64)r.length}, {"cached_mask", (qint64)r.cached_mask},
            {"cached_format", format_to_json(r.cached_format)}};
    };
    auto bounded_rule_string = [](const QJsonObject &o, const char *key,
                                  const QString &fallback, int maximum) {
        QString value = o.value(QString::fromUtf8(key)).toString(fallback);
        if (value.size() > maximum) value.truncate(maximum);
        return value.toStdString();
    };
    auto bounded_rule_size = [](const QJsonObject &o, const char *key,
                                quint64 fallback, quint64 maximum) {
        bool ok = false;
        const quint64 value = o.value(QString::fromUtf8(key)).toVariant().toULongLong(&ok);
        return static_cast<size_t>(std::min(ok ? value : fallback, maximum));
    };
    auto rule_from_json = [format_from_json, bounded_rule_string,
                           bounded_rule_size](const QJsonObject &o, int index) {
        RichTextAutoStyleRule r; r.enabled = o.value("enabled").toBool(true);
        r.style_preset_id = bounded_rule_string(o, "style_preset_id", QString(), 256);
        r.rule_id = bounded_rule_string(o, "rule_id", QString(), 256);
        if (r.rule_id.empty()) r.rule_id = std::string("rule_") + std::to_string(index + 1);
        r.display_name = bounded_rule_string(o, "display_name", QString(), 512);
        r.conflict_mode = bounded_rule_string(o, "conflict_mode", QStringLiteral("override_previous"), 64);
        r.match_mode = bounded_rule_string(o, "match_mode", QStringLiteral("all_matches"), 64);
        r.stop_processing = o.value("stop_processing").toBool(false);
        const QJsonArray excludes = o.value("excludes_rule_ids").toArray();
        for (int i = 0; i < excludes.size() && i < 128; ++i) {
            QString id = excludes.at(i).toString(); if (id.size() > 256) id.truncate(256);
            if (!id.isEmpty()) r.excludes_rule_ids.push_back(id.toStdString());
        }
        r.condition_type = bounded_rule_string(o, "condition_type", QStringLiteral("range_markers"), 64);
        r.regex_pattern = bounded_rule_string(o, "regex_pattern", QString(), 65536);
        r.regex_capture_group = bounded_rule_size(o, "regex_capture_group", 0, 1024);
        r.regex_case_sensitive = o.value("regex_case_sensitive").toBool(true);
        r.generalization_mode = bounded_rule_string(o, "generalization_mode", QStringLiteral("auto_merge"), 64);
        r.prevent_duplicates = o.value("prevent_duplicates").toBool(true); r.allow_multiple_cases = o.value("allow_multiple_cases").toBool(true);
        r.start_condition = bounded_rule_string(o, "start_condition", QStringLiteral("text_start"), 64);
        r.end_condition = bounded_rule_string(o, "end_condition", QStringLiteral("character_index"), 64);
        r.start_offset = bounded_rule_size(o, "start_offset", 0, 10000000);
        r.end_offset = bounded_rule_size(o, "end_offset", 0, 10000000);
        r.start_custom_chars = bounded_rule_string(o, "start_custom_chars", QString(), 65536);
        r.end_custom_chars = bounded_rule_string(o, "end_custom_chars", QString(), 65536);
        r.require_stop_match = o.value("require_stop_match").toBool(true); r.include_start_marker = o.value("include_start_marker").toBool(true);
        r.include_end_marker = o.value("include_end_marker").toBool(false);
        r.start = bounded_rule_size(o, "start", 0, 10000000);
        r.length = bounded_rule_size(o, "length", 0, 10000000);
        r.cached_mask = static_cast<uint32_t>(bounded_rule_size(o, "cached_mask", 0, 0xffffffffULL));
        r.cached_format = format_from_json(o.value("cached_format").toObject(), r.cached_format); return r;
    };
    auto update_rule_set_status = [this]() {
        if (!lbl_auto_rules_status_) return;
        lbl_auto_rules_status_->setText(auto_style_rules_file_path_.isEmpty()
            ? QStringLiteral("Rule set: Not saved")
            : QStringLiteral("Rule set: %1").arg(QFileInfo(auto_style_rules_file_path_).fileName()));
    };
    auto save_rule_set = [this, rule_to_json, update_rule_set_status](QString path) mutable {
        if (!layer_) return false;
        if (path.isEmpty()) path = QFileDialog::getSaveFileName(this, "Save Auto Styling Rule Set", QString(),
            "GSP Auto Styling Rule Set (*.gsp-auto-style.json);;JSON Files (*.json)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".gsp-auto-style.json";
        QJsonArray rules; for (const auto &rule : layer_->rich_text.auto_style_rules) rules.append(rule_to_json(rule));
        QJsonObject root{{"format", "gsp-auto-style-rule-set"}, {"version", 2},
                         {"schema_version", fxm::serialization::kCurrentFormattingSchemaVersion},
                         {"pattern_schema_version", fxm::serialization::kCurrentPatternSchemaVersion},
                         {"development_version", fxm::serialization::kCurrentDevelopmentVersion},
                         {"auto_style_enabled", layer_->rich_text.auto_style_enabled},
                         {"base_style_preset_id", QString::fromStdString(layer_->rich_text.auto_default_style_preset_id)},
                         {"rules", rules}};
        QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "Save Rule Set", "Could not write the selected file."); return false;
        }
        const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (file.write(data) != data.size() || !file.commit()) {
            QMessageBox::warning(this, "Save Rule Set", "Could not write the selected file atomically."); return false;
        }
        auto_style_rules_file_path_ = path; update_rule_set_status(); return true;
    };
    connect(btn_auto_rules_save_as_, &QPushButton::clicked, this, [save_rule_set]() mutable { save_rule_set(QString()); });
    connect(btn_auto_rules_save_, &QPushButton::clicked, this, [this, save_rule_set]() mutable { save_rule_set(auto_style_rules_file_path_); });
    connect(btn_auto_rules_load_, &QPushButton::clicked, this, [this, rule_from_json, invalidate_auto_text_styling, refresh_auto_rule_list, update_rule_set_status]() {
        if (!layer_) return;
        const QString path = QFileDialog::getOpenFileName(this, "Load Auto Styling Rule Set", QString(),
            "GSP Auto Styling Rule Set (*.gsp-auto-style.json *.json);;All Files (*)");
        if (path.isEmpty()) return;
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Load Rule Set", "Could not open the selected file."); return; }
        QJsonParseError error; const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) { QMessageBox::warning(this, "Load Rule Set", "The selected file is not valid JSON."); return; }
        const QJsonObject root = doc.object();
        if (root.value("format").toString() != "gsp-auto-style-rule-set" || !root.value("rules").isArray()) {
            QMessageBox::warning(this, "Load Rule Set", "This is not a supported GSP Auto Styling rule set file."); return;
        }
        std::vector<RichTextAutoStyleRule> loaded; int index = 0;
        for (const auto &value : root.value("rules").toArray()) if (value.isObject() && loaded.size() < 128) loaded.push_back(rule_from_json(value.toObject(), index++));
        if (loaded.empty() && !root.value("rules").toArray().isEmpty()) {
            QMessageBox::warning(this, "Load Rule Set", "The rule set contains no recoverable rule definitions."); return;
        }
        QMessageBox choice(this); choice.setWindowTitle("Load Rule Set"); choice.setText(QString("Loaded %1 rule(s). How should they be applied?").arg(loaded.size()));
        auto *replace = choice.addButton("Replace Current", QMessageBox::AcceptRole); auto *append = choice.addButton("Append", QMessageBox::ActionRole); choice.addButton(QMessageBox::Cancel);
        choice.exec(); if (choice.clickedButton() != replace && choice.clickedButton() != append) return;
        size_t added_count = 0;
        if (choice.clickedButton() == replace) {
            layer_->rich_text.auto_style_rules.clear();
            added_count = rich_text_merge_auto_style_rules(layer_->rich_text.auto_style_rules, loaded);
        } else {
            added_count = rich_text_merge_auto_style_rules(layer_->rich_text.auto_style_rules, loaded);
        }
        if (choice.clickedButton() == replace) {
            QString preset_id = root.value("base_style_preset_id").toString();
            if (preset_id.size() > 256) preset_id.truncate(256);
            layer_->rich_text.auto_default_style_preset_id = preset_id.toStdString();
        }
        layer_->rich_text.auto_style_enabled = root.value("auto_style_enabled").toBool(true);
        auto_style_rules_file_path_ = path; update_rule_set_status(); invalidate_auto_text_styling(); refresh_auto_rule_list(); emit property_changed();
        if (added_count < loaded.size()) QMessageBox::information(this, "Load Rule Set", QString("Added %1 rule(s); skipped or merged %2 equivalent duplicate(s).").arg(added_count).arg(loaded.size() - added_count));
    });

    connect(chk_auto_style_enabled_, &QCheckBox::toggled, this, [this, invalidate_auto_text_styling](bool v){
        if (loading_values_ || !layer_) return;
        if (layer_->rich_text.empty()) layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);
        layer_->rich_text.auto_style_enabled = v;
        invalidate_auto_text_styling();
        emit property_changed();
    });
    connect(cmb_auto_default_style_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidate_auto_text_styling](int){
        if (loading_values_ || !layer_ || !cmb_auto_default_style_) return;
        if (layer_->rich_text.empty()) layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);
        layer_->rich_text.auto_default_style_preset_id = cmb_auto_default_style_->currentData().toString().toStdString();
        invalidate_auto_text_styling();
        emit property_changed();
    });
    connect(cmb_auto_rule_style_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, invalidate_auto_text_styling, refresh_auto_rule_list](int) {
        if (loading_values_ || !layer_ || !cmb_auto_rule_style_ || !lst_auto_style_rules_)
            return;
        const int row = lst_auto_style_rules_->currentRow();
        if (row < 0 || row >= static_cast<int>(layer_->rich_text.auto_style_rules.size()))
            return;
        auto &rule = layer_->rich_text.auto_style_rules[static_cast<size_t>(row)];
        const std::string selected_id = cmb_auto_rule_style_->currentData().toString().toStdString();
        if (rule.style_preset_id == selected_id)
            return;
        rule.style_preset_id = selected_id;
        /* Preset resolution is authoritative at render time. An empty id keeps
         * the rule's embedded learned format. */
        invalidate_auto_text_styling();
        refresh_auto_rule_list();
        if (lst_auto_style_rules_) lst_auto_style_rules_->setCurrentRow(row);
        if (lst_auto_rules_dialog_) lst_auto_rules_dialog_->setCurrentRow(row);
        emit property_changed();
    });
    connect(btn_auto_learn_formatting_, &QPushButton::clicked, this, [this, invalidate_auto_text_styling, refresh_auto_rule_list](){
        if (!layer_) return;
        try {
            if (layer_->rich_text.empty())
                layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);

            const auto learned = rich_text_infer_auto_style_rules(layer_->rich_text);
            if (learned.empty()) {
                QMessageBox::information(this, "Learn Formatting Pattern",
                                         "No reusable manually formatted text runs were found. Format part of the text first, then try again.");
                return;
            }

            auto named_learned = learned;
            for (auto &rule : named_learned) {
                if (rule.display_name.empty())
                    rule.display_name = auto_style_rule_descriptive_name(rule).toStdString();
            }

            const size_t added_count = rich_text_merge_auto_style_rules(
                layer_->rich_text.auto_style_rules, named_learned);
            layer_->rich_text.auto_style_enabled = true;
            if (chk_auto_style_enabled_) {
                const QSignalBlocker blocker(chk_auto_style_enabled_);
                chk_auto_style_enabled_->setChecked(true);
            }

            invalidate_auto_text_styling();
            refresh_auto_rule_list();
            emit property_changed();

            const qlonglong added = static_cast<qlonglong>(added_count);
            const qlonglong merged = static_cast<qlonglong>(named_learned.size() - added_count);
            QMessageBox::information(this, "Learn Formatting Pattern",
                                     QStringLiteral("Added %1 reusable rule(s); %2 equivalent example(s) were merged or skipped.")
                                         .arg(added)
                                         .arg(merged));
        } catch (const std::exception &error) {
            QMessageBox::critical(this, "Learn Formatting Pattern",
                                  QStringLiteral("GSP could not analyze this formatted text safely. No rules were changed.\n\nTechnical detail: %1")
                                      .arg(QString::fromUtf8(error.what())));
        } catch (...) {
            QMessageBox::critical(this, "Learn Formatting Pattern",
                                  "GSP could not analyze this formatted text safely. No rules were changed.");
        }
    });
    connect(btn_auto_rule_add_, &QPushButton::clicked, this, [this, apply_rule_editor_to_rule, cache_rule_format, invalidate_auto_text_styling, refresh_auto_rule_list](){
        if (!layer_ || !cmb_auto_rule_style_) return;
        const QString id = cmb_auto_rule_style_->currentData().toString();
        if (id.isEmpty()) return;
        if (layer_->rich_text.empty()) layer_->rich_text = rich_text_document_from_layer_defaults(*layer_);
        RichTextAutoStyleRule rule;
        rule.rule_id = std::string("rule_") + std::to_string(layer_->rich_text.auto_style_rules.size() + 1);
        rule.enabled = true;
        rule.style_preset_id = id.toStdString();
        apply_rule_editor_to_rule(rule);
        if (rule.display_name.empty()) rule.display_name = auto_style_rule_descriptive_name(rule).toStdString();
        if (rule.rule_id.empty()) rule.rule_id = std::string("rule_") + std::to_string(layer_->rich_text.auto_style_rules.size() + 1);
        cache_rule_format(rule);
        layer_->rich_text.auto_style_rules.push_back(rule);
        invalidate_auto_text_styling();
        refresh_auto_rule_list();
        emit property_changed();
    });
    connect(btn_auto_rule_update_, &QPushButton::clicked, this, [this, apply_rule_editor_to_rule, cache_rule_format, invalidate_auto_text_styling, refresh_auto_rule_list](){
        if (!layer_ || !lst_auto_style_rules_ || !lst_auto_style_rules_->currentItem()) return;
        int row = lst_auto_style_rules_->currentRow();
        if (row < 0 || row >= (int)layer_->rich_text.auto_style_rules.size()) return;
        auto &rule = layer_->rich_text.auto_style_rules[(size_t)row];
        apply_rule_editor_to_rule(rule);
        if (rule.display_name.empty()) rule.display_name = auto_style_rule_descriptive_name(rule).toStdString();
        cache_rule_format(rule);
        invalidate_auto_text_styling();
        refresh_auto_rule_list();
        lst_auto_style_rules_->setCurrentRow(row);
        emit property_changed();
    });
    connect(btn_auto_rule_delete_, &QPushButton::clicked, this, [this, invalidate_auto_text_styling, refresh_auto_rule_list](){
        if (!layer_ || !lst_auto_style_rules_ || !lst_auto_style_rules_->currentItem()) return;
        int row = lst_auto_style_rules_->currentRow();
        if (row < 0 || row >= (int)layer_->rich_text.auto_style_rules.size()) return;
        layer_->rich_text.auto_style_rules.erase(layer_->rich_text.auto_style_rules.begin() + row);
        invalidate_auto_text_styling();
        refresh_auto_rule_list();
        emit property_changed();
    });
    auto clear_all_auto_rules = [this, invalidate_auto_text_styling, refresh_auto_rule_list]() {
        if (!layer_ || layer_->rich_text.auto_style_rules.empty()) return;
        const auto result = QMessageBox::question(
            this, "Clear All Auto Styling Rules",
            QString("Remove all %1 Auto Styling rules?\n\nThis action can be undone only through the editor undo history.")
                .arg(layer_->rich_text.auto_style_rules.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (result != QMessageBox::Yes) return;
        layer_->rich_text.auto_style_rules.clear();
        invalidate_auto_text_styling();
        refresh_auto_rule_list();
        emit property_changed();
    };
    connect(btn_auto_rules_clear_, &QPushButton::clicked, this, clear_all_auto_rules);
    connect(btn_auto_rules_clear_dialog_, &QPushButton::clicked, this, clear_all_auto_rules);

    connect(btn_auto_rule_up_, &QPushButton::clicked, this, [this, invalidate_auto_text_styling, refresh_auto_rule_list](){
        if (!layer_ || !lst_auto_style_rules_) return;
        int row = lst_auto_style_rules_->currentRow();
        if (row <= 0 || row >= (int)layer_->rich_text.auto_style_rules.size()) return;
        std::swap(layer_->rich_text.auto_style_rules[(size_t)row], layer_->rich_text.auto_style_rules[(size_t)row - 1]);
        invalidate_auto_text_styling();
        refresh_auto_rule_list(); lst_auto_style_rules_->setCurrentRow(row - 1); emit property_changed();
    });
    connect(btn_auto_rule_down_, &QPushButton::clicked, this, [this, invalidate_auto_text_styling, refresh_auto_rule_list](){
        if (!layer_ || !lst_auto_style_rules_) return;
        int row = lst_auto_style_rules_->currentRow();
        if (row < 0 || row + 1 >= (int)layer_->rich_text.auto_style_rules.size()) return;
        std::swap(layer_->rich_text.auto_style_rules[(size_t)row], layer_->rich_text.auto_style_rules[(size_t)row + 1]);
        invalidate_auto_text_styling();
        refresh_auto_rule_list(); lst_auto_style_rules_->setCurrentRow(row + 1); emit property_changed();
    });
    auto open_rule_editor = [this](int row) {
        if (!auto_rules_dialog_) return;
        if (row < 0 && layer_ && !layer_->rich_text.auto_style_rules.empty()) row = 0;
        if (lst_auto_style_rules_) lst_auto_style_rules_->setCurrentRow(row);
        if (lst_auto_rules_dialog_) lst_auto_rules_dialog_->setCurrentRow(row);
        if (txt_auto_rules_demo_ && txt_auto_rules_demo_->toPlainText().isEmpty() && layer_)
            txt_auto_rules_demo_->setPlainText(QString::fromStdString(layer_->rich_text.plain_text));
        auto_rules_dialog_->exec();
    };
    connect(btn_auto_rules_edit_, &QPushButton::clicked, this, [open_rule_editor]() mutable { open_rule_editor(-1); });
    connect(lst_auto_style_rules_, &QListWidget::itemDoubleClicked, this,
            [this, open_rule_editor](QListWidgetItem *item) mutable {
                open_rule_editor(lst_auto_style_rules_ ? lst_auto_style_rules_->row(item) : -1);
            });
    connect(lst_auto_rules_dialog_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (lst_auto_style_rules_ && lst_auto_style_rules_->currentRow() != row)
            lst_auto_style_rules_->setCurrentRow(row);
    });
    connect(txt_auto_rules_demo_, &QTextEdit::textChanged, this, [this]() {
        if (!lbl_auto_rules_demo_status_ || !txt_auto_rules_demo_) return;
        const QString text = txt_auto_rules_demo_->toPlainText();
        if (text.isEmpty()) { lbl_auto_rules_demo_status_->setText("No demo text"); return; }
        const int lines = text.count('\n') + 1;
        const int spaces = text.count(' ');
        const int tabs = text.count('\t');
        const int active_rules = layer_ ? (int)std::count_if(layer_->rich_text.auto_style_rules.begin(), layer_->rich_text.auto_style_rules.end(),
            [](const RichTextAutoStyleRule &r){ return r.enabled; }) : 0;
        lbl_auto_rules_demo_status_->setText(QString("%1 characters • %2 line(s) • %3 spaces • %4 tabs • %5 active rule(s)")
            .arg(text.size()).arg(lines).arg(spaces).arg(tabs).arg(active_rules));
    });

    connect(lst_auto_style_rules_, &QListWidget::currentRowChanged, this, [this, load_rule_to_editor](int row){
        const bool valid = layer_ && row >= 0 && row < (int)layer_->rich_text.auto_style_rules.size();
        if (auto_rule_editor_box_) auto_rule_editor_box_->setEnabled(valid);
        if (lst_auto_rules_dialog_ && lst_auto_rules_dialog_->currentRow() != row) {
            QSignalBlocker blocker(lst_auto_rules_dialog_);
            lst_auto_rules_dialog_->setCurrentRow(row);
        }
        if (!valid) return;
        const auto &rule = layer_->rich_text.auto_style_rules[(size_t)row];
        load_rule_to_editor(rule);
    });
    auto update_marker_field_visibility = [this]() {
        if (!cmb_auto_rule_start_condition_ || !cmb_auto_rule_end_condition_) return;
        const QString start = cmb_auto_rule_start_condition_->currentData().toString();
        const QString end = cmb_auto_rule_end_condition_->currentData().toString();
        edt_auto_rule_start_chars_->setVisible(start == "custom_char");
        edt_auto_rule_end_chars_->setVisible(end == "custom_char");
        spn_auto_rule_start_offset_->setVisible(start == "character_index" || start == "character_count" || start == "word_count");
        spn_auto_rule_end_offset_->setVisible(end == "character_index" || end == "character_count" || end == "word_count");
        chk_auto_rule_include_start_marker_->setVisible(start == "custom_char");
        chk_auto_rule_include_end_marker_->setVisible(end == "custom_char");
        chk_auto_rule_require_stop_match_->setVisible(end != "text_end" && end != "character_index" && end != "character_count" && end != "word_count");
    };
    connect(cmb_auto_rule_start_condition_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [update_marker_field_visibility](int){ update_marker_field_visibility(); });
    connect(cmb_auto_rule_end_condition_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [update_marker_field_visibility](int){ update_marker_field_visibility(); });
    update_marker_field_visibility();

    ticker_status_timer_ = new QTimer(this);
    ticker_status_timer_->setInterval(150);
    connect(ticker_status_timer_, &QTimer::timeout, this,
            &PropertiesPanel::update_ticker_runtime_button);
    ticker_status_timer_->start();

    /* Keep every late-created Properties field/form on the exact compact
     * metrics used by the 3D Camera inspector. */
    fxm_apply_transform_panel_widget_style(inner);

    auto chart_changed = [this]() {
        if (!loading_values_)
            emit property_changed(true);
    };
    connect(cmb_chart_type_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, chart_changed](int index) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart || index < 0)
            return;
        const ChartType selected = static_cast<ChartType>(
            cmb_chart_type_->itemData(index).toInt());
        layer_->chart_type = selected == ChartType::Bar &&
                cmb_chart_orientation_->currentData().toInt() == 1
            ? ChartType::HorizontalBar : selected;
        chart_changed();
        load_values();
    });
    connect(cmb_chart_orientation_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, chart_changed](int index) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart || index < 0)
            return;
        if (layer_->chart_type == ChartType::Bar ||
            layer_->chart_type == ChartType::HorizontalBar) {
            layer_->chart_type = cmb_chart_orientation_->itemData(index).toInt() == 1
                ? ChartType::HorizontalBar : ChartType::Bar;
            chart_changed();
            load_values();
        }
    });
    connect(cmb_chart_data_mode_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, chart_changed](int index) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart || index < 0)
            return;
        layer_->chart_data_mode = static_cast<ChartDataMode>(
            cmb_chart_data_mode_->itemData(index).toInt());
        chart_changed();
        load_values();
    });
    connect(btn_chart_data_settings_, &QToolButton::clicked, this, [this]() {
        if (!title_)
            return;
        ExternalDataSettingsDialog dialog(title_, this);
        dialog.exec();
        load_values();
    });
    connect(cmb_chart_source_, &QComboBox::currentTextChanged, this,
            [this, chart_changed](const QString &text) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart)
            return;
        const QVariant id = cmb_chart_source_->currentData();
        layer_->chart_binding.source_id =
            (id.isValid() && !id.toString().isEmpty() ? id.toString() : text)
                .toStdString();
        chart_changed();
        load_values();
    });
    auto connect_chart_mapping = [this, chart_changed](
            QComboBox *combo, std::string ChartDataBinding::*member) {
        connect(combo, &QComboBox::currentTextChanged, this,
                [this, chart_changed, member, combo](const QString &text) {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart)
                return;
            layer_->chart_binding.*member = text.toStdString();
            chart_changed();
            if (combo == cmb_chart_table_)
                load_values();
        });
    };
    connect_chart_mapping(cmb_chart_table_, &ChartDataBinding::table_path);
    connect_chart_mapping(cmb_chart_category_, &ChartDataBinding::category_field);
    connect_chart_mapping(cmb_chart_series_field_, &ChartDataBinding::series_field);
    connect_chart_mapping(cmb_chart_secondary_field_, &ChartDataBinding::secondary_value_field);
    connect_chart_mapping(cmb_chart_metadata_field_, &ChartDataBinding::metadata_field);
    connect_chart_mapping(cmb_chart_color_field_, &ChartDataBinding::color_field);
    connect(cmb_chart_fill_style_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, chart_changed](int index) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart || index < 0)
            return;
        layer_->fill_type = cmb_chart_fill_style_->itemData(index).toInt();
        chart_changed();
        load_values();
    });
    for (QCheckBox *box : {chk_chart_grid_, chk_chart_axes_,
                           chk_chart_axis_labels_, chk_chart_data_labels_,
                           chk_chart_legend_, chk_chart_background_,
                           chk_chart_fill_, chk_chart_outline_,
                           chk_chart_thousands_}) {
        connect(box, &QCheckBox::toggled, this, [this, box, chart_changed](bool value) {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart)
                return;
            if (box == chk_chart_grid_) layer_->chart_grid_enabled = value;
            else if (box == chk_chart_axes_) layer_->chart_axes_enabled = value;
            else if (box == chk_chart_axis_labels_) layer_->chart_axis_labels_enabled = value;
            else if (box == chk_chart_data_labels_) layer_->chart_data_labels_enabled = value;
            else if (box == chk_chart_legend_) layer_->chart_legend_enabled = value;
            else if (box == chk_chart_background_) layer_->chart_background_enabled = value;
            else if (box == chk_chart_fill_) layer_->chart_fill_enabled = value;
            else if (box == chk_chart_outline_) layer_->chart_outline_enabled = value;
            else if (box == chk_chart_thousands_) layer_->chart_thousands_separator = value;
            chart_changed();
            if (box == chk_chart_axes_ || box == chk_chart_grid_ ||
                box == chk_chart_background_)
                load_values();
        });
    }
    for (QSpinBox *spin : {spn_chart_start_row_, spn_chart_maximum_rows_}) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this, spin, chart_changed](int value) {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
            if (spin == spn_chart_start_row_) layer_->chart_binding.start_row = value;
            else layer_->chart_binding.maximum_rows = value;
            chart_changed();
        });
    }
    connect(chk_chart_ignore_empty_, &QCheckBox::toggled, this,
            [this, chart_changed](bool value) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        layer_->chart_binding.ignore_empty_rows = value;
        chart_changed();
    });
    connect(spn_chart_precision_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, chart_changed](int value) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        layer_->chart_decimal_precision = value;
        chart_changed();
    });
    connect(spn_chart_transition_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this, chart_changed](double value) {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        layer_->chart_data_transition_seconds = value;
        chart_changed();
    });
    auto connect_chart_property = [this, chart_changed](
            QDoubleSpinBox *spin, AnimatedProperty Layer::*member, double divisor) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, chart_changed, member, divisor](double value) {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
            set_animated_value(layer_.get()->*member,
                std::clamp(playhead_ - layer_->in_time, 0.0,
                           std::max(0.0, layer_->out_time - layer_->in_time)),
                value / divisor);
            chart_changed();
        });
    };
    connect_chart_property(spn_chart_animation_, &Layer::chart_animation_progress, 100.0);
    connect_chart_property(spn_chart_reveal_, &Layer::chart_reveal_progress, 100.0);
    connect_chart_property(spn_chart_value_transition_, &Layer::chart_value_transition, 100.0);
    connect_chart_property(spn_chart_label_opacity_, &Layer::chart_label_opacity, 100.0);
    connect_chart_property(spn_chart_element_opacity_, &Layer::chart_element_opacity, 100.0);
    connect_chart_property(spn_chart_font_size_, &Layer::chart_font_size, 1.0);
    connect_chart_property(spn_chart_line_thickness_, &Layer::chart_line_thickness, 1.0);
    connect_chart_property(spn_chart_bar_width_, &Layer::chart_bar_width, 100.0);
    connect_chart_property(spn_chart_bar_spacing_, &Layer::chart_bar_spacing, 100.0);
    connect_chart_property(spn_chart_corner_radius_, &Layer::chart_corner_radius, 1.0);
    connect_chart_property(spn_chart_padding_, &Layer::chart_padding, 1.0);
    connect_chart_property(spn_chart_margin_left_, &Layer::chart_margin_left, 1.0);
    connect_chart_property(spn_chart_margin_top_, &Layer::chart_margin_top, 1.0);
    connect_chart_property(spn_chart_margin_right_, &Layer::chart_margin_right, 1.0);
    connect_chart_property(spn_chart_margin_bottom_, &Layer::chart_margin_bottom, 1.0);
    for (QLineEdit *edit : {edt_chart_font_family_, edt_chart_number_prefix_,
                            edt_chart_number_suffix_}) {
        connect(edit, &QLineEdit::editingFinished, this,
                [this, edit, chart_changed]() {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
            if (edit == edt_chart_font_family_)
                layer_->chart_font_family = edit->text().toStdString();
            else if (edit == edt_chart_number_prefix_)
                layer_->chart_number_prefix = edit->text().toStdString();
            else
                layer_->chart_number_suffix = edit->text().toStdString();
            chart_changed();
        });
    }
    auto connect_chart_color = [this, chart_changed](QPushButton *button,
                                                      uint32_t Layer::*member,
                                                      const QString &title) {
        connect(button, &QPushButton::clicked, this,
                [this, chart_changed, button, member, title]() {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
            const QColor selected = QColorDialog::getColor(
                QColor::fromRgba(static_cast<QRgb>(layer_.get()->*member)), this, title,
                QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
            if (!selected.isValid()) return;
            layer_.get()->*member = static_cast<uint32_t>(selected.rgba());
            style_color_button(button, layer_.get()->*member);
            button->setText(QString());
            chart_changed();
        });
    };
    connect_chart_color(btn_chart_background_color_, &Layer::chart_background_color,
                        "Chart Background Color");
    connect_chart_color(btn_chart_grid_color_, &Layer::chart_grid_color,
                        "Chart Grid Color");
    connect_chart_color(btn_chart_axis_color_, &Layer::chart_axis_color,
                        "Chart Axis Color");
    connect_chart_color(btn_chart_label_color_, &Layer::chart_label_color,
                        "Chart Label Color");
    auto connect_chart_keyframe = [this, chart_changed](
            QPushButton *button, QDoubleSpinBox *spin,
            AnimatedProperty Layer::*member, double divisor) {
        connect(button, &QPushButton::clicked, this,
                [this, chart_changed, spin, member, divisor]() {
            if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
            const double time = std::clamp(playhead_ - layer_->in_time, 0.0,
                std::max(0.0, layer_->out_time - layer_->in_time));
            toggle_keyframe(layer_.get()->*member, time, spin->value() / divisor);
            chart_changed();
            load_values();
        });
        register_keyframe_times(button, [this, member]() {
            std::vector<double> times;
            if (!layer_) return times;
            for (const Keyframe &key : (layer_.get()->*member).keyframes)
                times.push_back(layer_->in_time + key.time);
            return times;
        });
    };
    connect_chart_keyframe(btn_kf_chart_animation_, spn_chart_animation_,
                           &Layer::chart_animation_progress, 100.0);
    connect_chart_keyframe(btn_kf_chart_reveal_, spn_chart_reveal_,
                           &Layer::chart_reveal_progress, 100.0);
    connect_chart_keyframe(btn_kf_chart_value_transition_, spn_chart_value_transition_,
                           &Layer::chart_value_transition, 100.0);
    connect_chart_keyframe(btn_kf_chart_label_opacity_, spn_chart_label_opacity_,
                           &Layer::chart_label_opacity, 100.0);
    connect_chart_keyframe(btn_kf_chart_element_opacity_, spn_chart_element_opacity_,
                           &Layer::chart_element_opacity, 100.0);
    connect(btn_chart_add_series_, &QToolButton::clicked, this,
            [this, chart_changed]() {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        static const uint32_t palette[] = {0xFF4C8DFFu, 0xFFFF6B6Bu, 0xFF51CF66u,
                                           0xFFFFD43Bu, 0xFFB197FCu, 0xFF22B8CFu};
        ChartSeries series;
        const int ordinal = static_cast<int>(layer_->chart_series.size()) + 1;
        series.id = "series-" + std::to_string(ordinal);
        series.name = "Series " + std::to_string(ordinal);
        series.value_field = "value" + std::to_string(ordinal);
        series.color = palette[(ordinal - 1) % 6];
        series.fill_color = (series.color & 0x00FFFFFFu) | 0x80000000u;
        layer_->chart_series.push_back(std::move(series));
        chart_changed();
        load_values();
    });
    connect(btn_chart_remove_series_, &QToolButton::clicked, this,
            [this, chart_changed]() {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        const int row = tbl_chart_series_->currentRow();
        if (row < 0 || row >= static_cast<int>(layer_->chart_series.size())) return;
        layer_->chart_series.erase(layer_->chart_series.begin() + row);
        chart_changed();
        load_values();
    });
    connect(tbl_chart_series_, &QTableWidget::itemChanged, this,
            [this, chart_changed](QTableWidgetItem *item) {
        if (!item || !layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        const int row = item->row();
        if (row < 0 || row >= static_cast<int>(layer_->chart_series.size())) return;
        ChartSeries &series = layer_->chart_series[static_cast<size_t>(row)];
        if (item->column() == 0) series.visible = item->checkState() == Qt::Checked;
        else if (item->column() == 1) series.name = item->text().toStdString();
        else if (item->column() == 2) series.value_field = item->text().toStdString();
        else if (item->column() == 4)
            series.fill_enabled = item->checkState() == Qt::Checked;
        else if (item->column() == 5)
            series.outline_enabled = item->checkState() == Qt::Checked;
        else if (item->column() == 6) {
            bool ok = false;
            const double width = item->text().toDouble(&ok);
            series.line_thickness = ok ? std::clamp(width, -1.0, 256.0) : -1.0;
        }
        chart_changed();
    });
    connect(btn_chart_add_value_, &QToolButton::clicked, this,
            [this, chart_changed]() {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        ChartValue value;
        value.id = "value-" + std::to_string(layer_->chart_values.size() + 1);
        value.category = "Category " + std::to_string(layer_->chart_values.size() + 1);
        value.series_id = layer_->chart_series.empty() ? "value"
                                                       : layer_->chart_series.front().id;
        layer_->chart_values.push_back(std::move(value));
        chart_changed();
        load_values();
        tbl_chart_values_->setCurrentCell(tbl_chart_values_->rowCount() - 1, 1);
    });
    connect(btn_chart_remove_value_, &QToolButton::clicked, this,
            [this, chart_changed]() {
        if (!layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        const int row = tbl_chart_values_->currentRow();
        if (row < 0 || row >= static_cast<int>(layer_->chart_values.size())) return;
        layer_->chart_values.erase(layer_->chart_values.begin() + row);
        chart_changed();
        load_values();
    });
    connect(tbl_chart_values_, &QTableWidget::itemChanged, this,
            [this, chart_changed](QTableWidgetItem *item) {
        if (!item || !layer_ || loading_values_ || layer_->type != LayerType::Chart) return;
        const int row = item->row();
        if (row < 0 || row >= static_cast<int>(layer_->chart_values.size())) return;
        ChartValue &value = layer_->chart_values[static_cast<size_t>(row)];
        if (item->column() == 0) {
            value.expose_in_dock = item->checkState() == Qt::Checked;
        } else if (item->column() == 1) {
            value.category = item->text().toStdString();
        } else if (item->column() == 3) {
            bool ok = false;
            const double parsed = item->text().toDouble(&ok);
            if (ok && std::isfinite(parsed)) value.value = parsed;
        } else if (item->column() == 4) {
            const QString text = item->text().trimmed();
            bool ok = false;
            const double parsed = text.toDouble(&ok);
            value.has_secondary_value = !text.isEmpty() && ok && std::isfinite(parsed);
            if (value.has_secondary_value) value.secondary_value = parsed;
        } else if (item->column() == 6) {
            value.metadata = item->text().toStdString();
        }
        chart_changed();
    });

    /* The camera inspector is the canonical Properties typography.  Apply it
     * once after every section has been built so nested/legacy section style
     * sheets cannot leave labels (including switch text such as Lock Scale)
     * with different sizes or colours. */
    QFont property_label_font = inner->font();
    property_label_font.setPixelSize(10);
    property_label_font.setBold(false);
    const QString property_label_style = QStringLiteral(
        "QLabel{color:%1;font-size:10px;font-weight:normal;"
        "background:transparent;padding:0;}").arg(subtle_text_name);
    for (QLabel *label : inner->findChildren<QLabel *>()) {
        label->setFont(property_label_font);
        label->setStyleSheet(property_label_style);
        QPalette label_palette = label->palette();
        label_palette.setColor(QPalette::WindowText, subtle_text);
        label_palette.setColor(QPalette::Text, subtle_text);
        label->setPalette(label_palette);
    }
    for (FxmSwitch *toggle : inner->findChildren<FxmSwitch *>()) {
        toggle->setFont(property_label_font);
        toggle->setStyleSheet(QStringLiteral(
            "QCheckBox{color:%1;font-size:10px;font-weight:normal;"
            "background:transparent;padding:0;}").arg(subtle_text_name));
        QPalette toggle_palette = toggle->palette();
        toggle_palette.setColor(QPalette::WindowText, subtle_text);
        toggle_palette.setColor(QPalette::Text, subtle_text);
        toggle->setPalette(toggle_palette);
    }
}

void PropertiesPanel::apply_anchor_preset(int index)
{
    if (!layer_ || !cmb_anchor_)
        return;
    index = std::clamp(index, 0, 8);
    if (cmb_anchor_->currentIndex() == index) {
        /* Re-applying the same preset is intentionally a no-op. */
        return;
    }
    cmb_anchor_->setCurrentIndex(index);
}

void PropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    title_ = t;
    FXM_LOG_DEBUG("Properties", QStringLiteral("Properties title=%1")
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>")));
}

void PropertiesPanel::set_active_text_edit_layer(const std::string &layer_id)
{
    if (active_text_edit_layer_id_ == layer_id) return;
    active_text_edit_layer_id_ = layer_id;
    load_values();
    emit gradient_model_refresh_requested();
}

void PropertiesPanel::update_asset_playback_controls_visibility()
{
    if (!asset_box_)
        return;
    auto set_field_visible = [this](QWidget *field, bool visible) {
        if (!field) return;
        field->setVisible(visible);
        if (auto *form = qobject_cast<QFormLayout *>(asset_box_->layout())) {
            if (auto *label = qobject_cast<QWidget *>(form->labelForField(field)))
                label->setVisible(visible);
        }
    };

    const bool is_asset = layer_ && layer_->type == LayerType::Asset;
    const bool has_animation = is_asset && layer_->asset_animated;
    const bool independent = has_animation && layer_->asset_playback_mode == 1;
    set_field_visible(cmb_asset_playback_, has_animation);
    set_field_visible(spn_asset_offset_, independent);
    set_field_visible(spn_asset_pause_duration_, independent &&
                      layer_->asset_source_playback_mode == 2);
    set_field_visible(spn_asset_loop_count_, independent &&
                      layer_->asset_source_playback_mode == 1);
    set_field_visible(chk_asset_loop_, independent);
}

void PropertiesPanel::set_layer(std::shared_ptr<Layer> layer, double t)
{
    layer_    = layer;
    playhead_ = t;
    /* A null selection is represented by a genuinely empty inspector instead
     * of a collection of individually hidden sections and stale controls. */
    if (QWidget *content = widget())
        content->setVisible(static_cast<bool>(layer_));
    FXM_LOG_DEBUG("Properties", QStringLiteral(
        "Properties layer=%1 type=%2 playhead=%3")
        .arg(layer_ ? QString::fromStdString(layer_->id)
                    : QStringLiteral("<none>"))
        .arg(layer_ ? static_cast<int>(layer_->type) : -1)
        .arg(playhead_, 0, 'f', 6));
    load_values();
    emit gradient_model_refresh_requested();
}


void PropertiesPanel::refresh_media_range_controls()
{
    if (!layer_ || loading_values_)
        return;

    QScopedValueRollback<bool> loading_guard(loading_values_, true);
    auto set_double = [](QDoubleSpinBox *spin, double value) {
        if (!spin || !std::isfinite(value))
            return;
        if (std::abs(spin->value() - value) < 0.000001)
            return;
        QSignalBlocker blocker(spin);
        spin->setValue(value);
    };

    const bool is_video = layer_->type == LayerType::Video;
    const bool is_audio = layer_->type == LayerType::Audio;
    if (is_video) {
        set_double(spn_video_in_, layer_->video_in_point);
        set_double(spn_video_out_, layer_->video_out_point);
    }

    // if (is_audio) { legacy contract boundary; video audio now reuses the same controls.
    if (is_video || is_audio) {
        const double media_in = is_video ? layer_->video_in_point : layer_->audio_in_point;
        const double media_out = is_video ? layer_->video_out_point : layer_->audio_out_point;
        set_double(spn_audio_in_, media_in);
        set_double(spn_audio_out_, media_out);
        const double clip_span = std::max(0.0, layer_->out_time - layer_->in_time);
        const double clip_local = std::clamp(playhead_ - layer_->in_time, 0.0, clip_span);
        const double media_at_playhead = media_in + clip_local;
        const bool inside = playhead_ >= layer_->in_time && playhead_ <= layer_->out_time;
        if (lbl_audio_range_preview_) {
            lbl_audio_range_preview_->setText(QStringLiteral(
                "Preview In %1 s  |  Out %2 s  |  Strip %3 s")
                .arg(media_in, 0, 'f', 2)
                .arg(media_out, 0, 'f', 2)
                .arg(clip_span, 0, 'f', 2));
        }
        if (lbl_audio_range_feedback_) {
            lbl_audio_range_feedback_->setText(QStringLiteral("%1 · range length follows strip · media %2 s")
                .arg(inside ? QStringLiteral("Inside") : QStringLiteral("Outside"))
                .arg(media_at_playhead, 0, 'f', 2));
            lbl_audio_range_feedback_->setProperty("insideRange", inside);
        }
    }
}

void PropertiesPanel::update_playhead(double t)
{
    playhead_ = t;
    if (!layer_ || loading_values_)
        return;

    QScopedValueRollback<bool> loading_guard(loading_values_, true);
    const double lt = std::clamp(playhead_ - layer_->in_time, 0.0,
                                 std::max(0.0, layer_->out_time - layer_->in_time));
    const bool is_text = layer_->type == LayerType::Text;
    const bool is_clock = layer_->type == LayerType::Clock;
    const bool is_ticker = layer_->type == LayerType::Ticker;
    const bool is_text_like = is_text || is_clock || is_ticker;
    const bool is_image = layer_type_is_image_like(layer_->type);
    const bool is_rect = layer_type_uses_shape_geometry(layer_->type);

    auto set_double = [](QDoubleSpinBox *spin, double value) {
        if (!spin || !std::isfinite(value))
            return;
        if (std::abs(spin->value() - value) < 0.000001)
            return;
        QSignalBlocker blocker(spin);
        spin->setValue(value);
    };
    auto set_int = [](QSpinBox *spin, int value) {
        if (!spin)
            return;
        if (spin->value() == value)
            return;
        QSignalBlocker blocker(spin);
        spin->setValue(value);
    };
    auto set_color_button = [](QPushButton *button, uint32_t argb) {
        if (!button)
            return;
        if (button->property("buttonFillKind").toString() == QStringLiteral("color") &&
            button->property("argb").isValid() && button->property("argb").toUInt() == argb)
            return;
        style_color_button(button, argb);
        button->setProperty("buttonFillKind", QStringLiteral("color"));
        button->setProperty("argb", argb);
    };
    if (layer_->type == LayerType::Light && light_options_box_) {
        const TitleLight &light = layer_->light;
        set_color_button(btn_light_color_,
                         evaluated_light_color(light, playhead_));
        if (btn_light_color_)
            btn_light_color_->setText(QString());
        set_double(spn_light_intensity_,
                   light.intensity.evaluate(playhead_));
        set_double(spn_light_source_size_,
                   light.source_size.evaluate(playhead_));
        set_double(spn_light_falloff_distance_,
                   light.falloff_distance.evaluate(playhead_));
        set_double(spn_light_cone_angle_,
                   light.cone_angle.evaluate(playhead_));
        set_double(spn_light_cone_feather_,
                   light.cone_feather.evaluate(playhead_));
        const Vec3Value target = light.target.evaluate(playhead_);
        set_double(spn_light_target_x_, target.x);
        set_double(spn_light_target_y_, target.y);
        set_double(spn_light_target_z_, target.z);
        set_double(spn_light_shadow_darkness_,
                   light.shadow_darkness.evaluate(playhead_));
        set_double(spn_light_shadow_softness_,
                   light.shadow_softness.evaluate(playhead_));
        set_double(spn_light_shadow_bias_,
                   light.shadow_bias.evaluate(playhead_));
    }
    auto set_gradient_button = [](QPushButton *button, uint32_t start_argb,
                                  uint32_t end_argb, int gradient_type) {
        if (!button)
            return;
        if (button->property("buttonFillKind").toString() == QStringLiteral("gradient") &&
            button->property("gradientStartArgb").isValid() &&
            button->property("gradientStartArgb").toUInt() == start_argb &&
            button->property("gradientEndArgb").toUInt() == end_argb &&
            button->property("gradientType").toInt() == gradient_type)
            return;
        style_gradient_button(button, start_argb, end_argb, gradient_type);
        button->setProperty("buttonFillKind", QStringLiteral("gradient"));
        button->setProperty("gradientStartArgb", start_argb);
        button->setProperty("gradientEndArgb", end_argb);
        button->setProperty("gradientType", gradient_type);
    };
    auto set_kf_icon = [](QPushButton *button, bool active, bool has_keyframes) {
        if (!button)
            return;
        fxm_refresh_keyframe_navigation(button);
        const bool outlined = has_keyframes && !active;
        if (button->property("active").isValid() &&
            button->property("active").toBool() == active &&
            button->property("outlined").toBool() == outlined)
            return;
        button->setIcon(fxm_keyframe_diamond_icon(active, outlined));
        button->setProperty("active", active);
        button->setProperty("outlined", outlined);
        button->style()->unpolish(button);
        button->style()->polish(button);
    };
    auto set_prop_kf_icon = [&](QPushButton *button, const AnimatedProperty &prop) {
        set_kf_icon(button, keyframe_at_time(prop, lt), prop.is_animated());
    };
    auto set_group_kf_icon = [&](QPushButton *button, std::initializer_list<const AnimatedProperty *> props) {
        set_kf_icon(button, any_keyframe_at_time(props, lt), any_keyframes(props));
    };
    auto set_vec_kf_icon = [&](QPushButton *button, const AnimatedVec2Property &prop) {
        set_kf_icon(button, keyframe_at_time(prop, lt), prop.is_animated());
    };
    auto set_vec3_kf_icon = [&](QPushButton *button,
                                const AnimatedVec2Property &xy,
                                const AnimatedProperty &z) {
        set_kf_icon(button,
                    keyframe_at_time(xy, lt) || keyframe_at_time(z, lt),
                    xy.is_animated() || z.is_animated());
    };
    auto set_position3_kf_icon = [&](QPushButton *button) {
        if (layer_->position_3d_path_enabled)
            set_kf_icon(button, keyframe_at_time(layer_->position_3d, lt),
                        layer_->position_3d.is_animated());
        else
            set_vec3_kf_icon(button, layer_->position, layer_->position_z);
    };
    auto set_scalar3_kf_icon = [&](QPushButton *button,
                                   const AnimatedProperty &x,
                                   const AnimatedProperty &y,
                                   const AnimatedProperty &z) {
        set_kf_icon(button,
                    any_keyframe_at_time({&x, &y, &z}, lt),
                    any_keyframes({&x, &y, &z}));
    };

    const LayerVector3Value position3 = evaluated_layer_position_3d(*layer_, lt);
    const Vec2Value scale = layer_->scale.evaluate(lt);
    set_double(spn_px_, position3.x);
    set_double(spn_py_, position3.y);
    set_double(spn_scale_x_, scale.x * 100.0);
    set_double(spn_scale_y_, scale.y * 100.0);
    set_double(spn_transform_size_w_, eval_box_width(*layer_, lt));
    set_double(spn_transform_size_h_, eval_box_height(*layer_, lt));
    set_double(spn_rot_, layer_->rotation.evaluate(lt));
    set_double(spn_pz_, position3.z);
    set_double(spn_rot_x_, layer_->rotation_x.evaluate(lt));
    set_double(spn_rot_y_, layer_->rotation_y.evaluate(lt));
    set_double(spn_scale_z_, layer_->scale.evaluate(lt).z * 100.0);
    set_double(spn_anchor_z_, layer_->origin_prop.evaluate(lt).z);
    set_double(spn_orientation_x_, layer_->orientation_x.evaluate(lt));
    set_double(spn_orientation_y_, layer_->orientation_y.evaluate(lt));
    set_double(spn_orientation_z_, layer_->orientation_z.evaluate(lt));
    set_double(spn_opacity_, layer_->opacity.evaluate(lt));
    if (spn_appearance_opacity_)
        set_double(spn_appearance_opacity_, layer_->opacity.evaluate(lt) * 100.0);
    set_double(spn_origin_x_, eval_origin_x(*layer_, lt));
    set_double(spn_origin_y_, eval_origin_y(*layer_, lt));
    if (layer_->type == LayerType::Chart) {
        set_double(spn_chart_animation_,
                   layer_->chart_animation_progress.evaluate(lt) * 100.0);
        set_double(spn_chart_reveal_,
                   layer_->chart_reveal_progress.evaluate(lt) * 100.0);
        set_double(spn_chart_value_transition_,
                   layer_->chart_value_transition.evaluate(lt) * 100.0);
        set_double(spn_chart_label_opacity_,
                   layer_->chart_label_opacity.evaluate(lt) * 100.0);
        set_double(spn_chart_element_opacity_,
                   layer_->chart_element_opacity.evaluate(lt) * 100.0);
        set_double(spn_chart_font_size_, layer_->chart_font_size.evaluate(lt));
        set_double(spn_chart_line_thickness_,
                   layer_->chart_line_thickness.evaluate(lt));
        set_double(spn_chart_bar_width_,
                   layer_->chart_bar_width.evaluate(lt) * 100.0);
        set_double(spn_chart_bar_spacing_,
                   layer_->chart_bar_spacing.evaluate(lt) * 100.0);
        set_double(spn_chart_corner_radius_,
                   layer_->chart_corner_radius.evaluate(lt));
        set_double(spn_chart_padding_, layer_->chart_padding.evaluate(lt));
        set_double(spn_chart_margin_left_, layer_->chart_margin_left.evaluate(lt));
        set_double(spn_chart_margin_top_, layer_->chart_margin_top.evaluate(lt));
        set_double(spn_chart_margin_right_, layer_->chart_margin_right.evaluate(lt));
        set_double(spn_chart_margin_bottom_, layer_->chart_margin_bottom.evaluate(lt));
        set_prop_kf_icon(btn_kf_chart_animation_, layer_->chart_animation_progress);
        set_prop_kf_icon(btn_kf_chart_reveal_, layer_->chart_reveal_progress);
        set_prop_kf_icon(btn_kf_chart_value_transition_, layer_->chart_value_transition);
        set_prop_kf_icon(btn_kf_chart_label_opacity_, layer_->chart_label_opacity);
        set_prop_kf_icon(btn_kf_chart_element_opacity_, layer_->chart_element_opacity);
    }

    if (is_image) {
        double raw_w = eval_image_width(*layer_, lt);
        double raw_h = eval_image_height(*layer_, lt);
        if (raw_w <= 0.0 || raw_h <= 0.0) {
            if (layer_->type == LayerType::Video) {
                raw_w = layer_->video_pixel_width;
                raw_h = layer_->video_pixel_height;
            } else {
                const QSize intrinsic = editor_image_intrinsic_size(QString::fromStdString(layer_->image_path));
                if (intrinsic.isValid() && !intrinsic.isEmpty()) {
                    raw_w = intrinsic.width();
                    raw_h = intrinsic.height();
                }
            }
        }
        const fxm::ImageDisplaySize display = fxm::calculate_image_display_size(
            layer_->image_box_mode, layer_->image_size_auto_fit,
            eval_box_width(*layer_, lt), eval_box_height(*layer_, lt), raw_w, raw_h);
        set_double(spn_layer_w_, display.width);
        set_double(spn_layer_h_, display.height);
    } else {
        set_double(spn_layer_w_, eval_box_width(*layer_, lt));
        set_double(spn_layer_h_, eval_box_height(*layer_, lt));
    }
    set_double(spn_image_box_w_, eval_box_width(*layer_, lt));
    set_double(spn_image_box_h_, eval_box_height(*layer_, lt));

    set_color_button(btn_text_color_, eval_text_color(*layer_, lt));
    set_color_button(btn_fill_color_, eval_fill_color(*layer_, lt));
    if (btn_appearance_fill_color_) {
        if (layer_->fill_type == 1)
            set_gradient_button(btn_appearance_fill_color_,
                                layer_->gradient_start_color,
                                layer_->gradient_end_color,
                                layer_->gradient_type);
        else
            set_color_button(btn_appearance_fill_color_,
                             is_text_like ? eval_text_color(*layer_, lt) : eval_fill_color(*layer_, lt));
        btn_appearance_fill_color_->setText(QString());
    }

    if (is_text_like) {
        set_int(spn_size_, (int)std::clamp(std::round(layer_->font_size_prop.evaluate(lt)), 1.0, 512.0));
        set_double(spn_char_tracking_, layer_->char_tracking_prop.evaluate(lt));
        set_double(spn_char_scale_x_, layer_->char_scale_x_prop.evaluate(lt) * 100.0);
        set_double(spn_char_scale_y_, layer_->char_scale_y_prop.evaluate(lt) * 100.0);
        set_double(spn_baseline_shift_, layer_->baseline_shift_prop.evaluate(lt));
        set_double(spn_paragraph_indent_left_, eval_paragraph_indent_left(*layer_, lt));
        set_double(spn_paragraph_indent_right_, eval_paragraph_indent_right(*layer_, lt));
        set_double(spn_paragraph_indent_first_line_, eval_paragraph_indent_first_line(*layer_, lt));
        set_double(spn_paragraph_space_before_, layer_->paragraph_space_before_prop.evaluate(lt));
        set_double(spn_paragraph_space_after_, layer_->paragraph_space_after_prop.evaluate(lt));

        if (lbl_text_fit_scale_ && layer_->text_overflow_mode == 2 && !is_ticker) {
            QFont preview_font = font_for_layer(*layer_);
            QRectF preview_rect(0, 0, eval_box_width(*layer_, lt), eval_box_height(*layer_, lt));
            double fit_scale = horizontal_fit_scale(preview_font, preview_rect,
                                                    display_text_for_style(*layer_), *layer_, lt);
            lbl_text_fit_scale_->setText(fxm_tr("OBSTitles.ScalePercentFormat")
                                             .arg((int)std::round(fit_scale * 100.0)));
        }
    }

    const bool transform_is_3d = layer_->dimension_mode == LayerDimensionMode::ThreeD;
    if (transform_is_3d) {
        set_position3_kf_icon(btn_kf_pos_x_);
        set_vec3_kf_icon(btn_kf_scale_x_, layer_->scale, layer_->scale_z);
        set_vec3_kf_icon(btn_kf_origin_x_, layer_->origin_prop, layer_->anchor_z);
        set_scalar3_kf_icon(btn_kf_rotation_, layer_->rotation_x,
                            layer_->rotation_y, layer_->rotation);
        set_scalar3_kf_icon(btn_kf_orientation_x_, layer_->orientation_x,
                            layer_->orientation_y, layer_->orientation_z);
    } else {
        set_vec_kf_icon(btn_kf_pos_x_, layer_->position);
        set_vec_kf_icon(btn_kf_scale_x_, layer_->scale);
        set_vec_kf_icon(btn_kf_origin_x_, layer_->origin_prop);
        set_prop_kf_icon(btn_kf_rotation_, layer_->rotation);
    }
    if (transform_is_3d) set_position3_kf_icon(btn_kf_pos_y_);
    else set_vec_kf_icon(btn_kf_pos_y_, layer_->position);
    set_vec_kf_icon(btn_kf_scale_y_, layer_->scale);
    set_vec_kf_icon(btn_kf_transform_size_, layer_->size);
    if (transform_is_3d) set_position3_kf_icon(btn_kf_position_z_);
    else set_prop_kf_icon(btn_kf_position_z_, layer_->position_z);
    set_prop_kf_icon(btn_kf_rotation_x_, layer_->rotation_x);
    set_prop_kf_icon(btn_kf_rotation_y_, layer_->rotation_y);
    set_prop_kf_icon(btn_kf_scale_z_, layer_->scale_z);
    set_prop_kf_icon(btn_kf_anchor_z_, layer_->anchor_z);
    set_prop_kf_icon(btn_kf_orientation_y_, layer_->orientation_y);
    set_prop_kf_icon(btn_kf_orientation_z_, layer_->orientation_z);
    set_prop_kf_icon(btn_kf_opacity_, layer_->opacity);
    set_prop_kf_icon(btn_kf_material_ambient_, layer_->material_ambient);
    set_prop_kf_icon(btn_kf_material_diffuse_, layer_->material_diffuse);
    set_prop_kf_icon(btn_kf_material_specular_, layer_->material_specular);
    set_prop_kf_icon(btn_kf_material_shininess_, layer_->material_shininess);
    set_prop_kf_icon(btn_kf_material_metallic_, layer_->material_metallic);
    set_prop_kf_icon(btn_kf_material_roughness_, layer_->material_roughness);
    set_prop_kf_icon(btn_kf_material_reflection_, layer_->material_reflection_intensity);
    set_group_kf_icon(btn_kf_material_emissive_color_,
                      {&layer_->material_emissive_color_a,
                       &layer_->material_emissive_color_r,
                       &layer_->material_emissive_color_g,
                       &layer_->material_emissive_color_b});
    set_prop_kf_icon(btn_kf_material_emissive_intensity_,
                     layer_->material_emissive_intensity);
    set_prop_kf_icon(btn_kf_geometry_extrusion_depth_,
                     layer_->geometry_extrusion_depth);
    set_prop_kf_icon(btn_kf_geometry_bevel_depth_, layer_->geometry_bevel_depth);
    set_vec_kf_icon(btn_kf_origin_y_, layer_->origin_prop);
    set_prop_kf_icon(btn_kf_paragraph_indent_left_, layer_->paragraph_indent_left_prop);
    set_prop_kf_icon(btn_kf_paragraph_indent_right_, layer_->paragraph_indent_right_prop);
    set_prop_kf_icon(btn_kf_paragraph_indent_first_line_, layer_->paragraph_indent_first_line_prop);
    set_prop_kf_icon(btn_kf_font_size_, layer_->font_size_prop);
    set_prop_kf_icon(btn_kf_char_scale_x_, layer_->char_scale_x_prop);
    set_prop_kf_icon(btn_kf_char_scale_y_, layer_->char_scale_y_prop);
    set_prop_kf_icon(btn_kf_char_tracking_, layer_->char_tracking_prop);
    set_prop_kf_icon(btn_kf_baseline_shift_, layer_->baseline_shift_prop);
    set_prop_kf_icon(btn_kf_paragraph_space_before_, layer_->paragraph_space_before_prop);
    set_prop_kf_icon(btn_kf_paragraph_space_after_, layer_->paragraph_space_after_prop);
    set_vec_kf_icon(btn_kf_width_, is_image ? layer_->image_size : layer_->size);
    set_vec_kf_icon(btn_kf_image_box_size_, layer_->size);
    set_group_kf_icon(btn_kf_text_color_, {&layer_->text_color_a, &layer_->text_color_r,
                                           &layer_->text_color_g, &layer_->text_color_b});
    set_group_kf_icon(btn_kf_fill_color_, {&layer_->fill_color_a, &layer_->fill_color_r,
                                           &layer_->fill_color_g, &layer_->fill_color_b});
    if (is_text_like)
        set_group_kf_icon(btn_kf_appearance_fill_, {&layer_->text_color_a, &layer_->text_color_r,
                                                    &layer_->text_color_g, &layer_->text_color_b});
    else if (is_rect)
        set_group_kf_icon(btn_kf_appearance_fill_, {&layer_->fill_color_a, &layer_->fill_color_r,
                                                    &layer_->fill_color_g, &layer_->fill_color_b});
    set_group_kf_icon(btn_kf_appearance_stroke_,
                      {&layer_->stroke_color_a, &layer_->stroke_color_r,
                       &layer_->stroke_color_g, &layer_->stroke_color_b});
}

void PropertiesPanel::update_ticker_runtime_button()
{
    if (!btn_ticker_pause_ || !layer_ || layer_->type != LayerType::Ticker)
        return;
    const std::string title_id = title_ ? title_->id : std::string();
    const bool title_cued = title_ &&
        (title_->current_cue_row >= 0 || title_->pending_cue_row >= 0);
    const TickerRuntimeSnapshot state =
        fxm::ticker_runtime::status(title_id, *layer_, title_cued);
    btn_ticker_pause_->setIcon(obs_icon(state.paused ? "play.svg" : "pause.svg"));
    btn_ticker_pause_->setText(state.paused
        ? fxm_tr("OBSTitles.TickerPlay")
        : fxm_tr("OBSTitles.TickerPause"));
    btn_ticker_pause_->setToolTip(state.paused
        ? fxm_tr("OBSTitles.TickerPlayTooltip")
        : fxm_tr("OBSTitles.TickerPauseTooltip"));
}

void PropertiesPanel::load_values()
{
    loading_values_ = true;
    if (!layer_) {
        if (transform_box_) transform_box_->setVisible(false);
        if (video_box_) video_box_->setVisible(false);
        if (live_playback_box_) live_playback_box_->setVisible(false);
        if (audio_box_) audio_box_->setVisible(false);
        if (asset_box_) asset_box_->setVisible(false);
        for (QWidget *section : {static_cast<QWidget *>(chart_box_),
                                 static_cast<QWidget *>(chart_data_box_),
                                 static_cast<QWidget *>(chart_axes_box_),
                                 static_cast<QWidget *>(chart_labels_box_),
                                 static_cast<QWidget *>(chart_appearance_box_),
                                 static_cast<QWidget *>(chart_animation_box_)}) {
            if (!section) continue;
            section->setVisible(false);
            if (auto *panel = fxm_panel_for_content(section)) panel->setVisible(false);
        }
        if (light_options_box_) {
            light_options_box_->setVisible(false);
            if (auto *panel = fxm_panel_for_content(light_options_box_))
                panel->setVisible(false);
        }
        if (appearance_box_) appearance_box_->setVisible(false);
        if (btn_transform_defaults_) btn_transform_defaults_->setEnabled(false);
        if (btn_anchor_grid_) { btn_anchor_grid_->setProperty("active_index", 4); btn_anchor_grid_->update(); }
        text_box_->setVisible(false);
        if (type_options_box_) type_options_box_->setVisible(false);
        if (paragraph_box_) paragraph_box_->setVisible(false);
        if (dynamic_text_box_) dynamic_text_box_->setVisible(false);
        if (auto_style_box_) auto_style_box_->setVisible(false);
        if (text_animators_box_) text_animators_box_->setVisible(false);
        if (live_edit_box_) live_edit_box_->setVisible(false);
        if (bullets_box_) bullets_box_->setVisible(false);
        rect_box_->setVisible(false);
        if (btn_shape_defaults_) btn_shape_defaults_->setVisible(false);
        if (chk_size_lock_) chk_size_lock_->setVisible(false);
        if (row_shape_scale_options_) row_shape_scale_options_->setVisible(false);
        if (grp_shape_type_) {
            for (auto *button : grp_shape_type_->buttons())
                button->setVisible(false);
        }
        if (auto *shape_types_row = rect_box_ ? rect_box_->findChild<QWidget *>(QStringLiteral("OBSTitlesShapeTypeButtonsRow")) : nullptr)
            shape_types_row->setVisible(false);
        if (gradient_box_) gradient_box_->setVisible(false);
        image_box_->setVisible(false);
        if (image_box_size_box_) image_box_size_box_->setVisible(false);
        if (outline_box_) outline_box_->setVisible(false);
        if (shadow_box_) shadow_box_->setVisible(false);
        spn_px_->setValue(0.0);
        spn_py_->setValue(0.0);
        spn_rot_->setValue(0.0);
        if (cmb_dimension_mode_) cmb_dimension_mode_->setCurrentIndex(0);
        update_transform_dimension_ui(false, LayerDimensionMode::TwoD);
        if (spn_pz_) spn_pz_->setValue(0.0);
        if (spn_rot_x_) spn_rot_x_->setValue(0.0);
        if (spn_rot_y_) spn_rot_y_->setValue(0.0);
        if (spn_scale_z_) spn_scale_z_->setValue(100.0);
        if (spn_anchor_z_) spn_anchor_z_->setValue(0.0);
        if (spn_orientation_x_) spn_orientation_x_->setValue(0.0);
        if (spn_orientation_y_) spn_orientation_y_->setValue(0.0);
        if (spn_orientation_z_) spn_orientation_z_->setValue(0.0);
        spn_opacity_->setValue(1.0);
        spn_origin_x_->setValue(0.5);
        spn_origin_y_->setValue(0.5);
        if (chk_shape_scale_stroke_) chk_shape_scale_stroke_->setChecked(false);
        if (chk_shape_scale_corners_) chk_shape_scale_corners_->setChecked(false);
        txt_content_->clear();
        edit_image_path_->clear();
        if (btn_text_external_binding_) {
            QSignalBlocker block(btn_text_external_binding_);
            btn_text_external_binding_->setChecked(false);
            btn_text_external_binding_->setToolTip(QStringLiteral("Bind text content to external data"));
        }
        if (btn_image_external_binding_) {
            QSignalBlocker block(btn_image_external_binding_);
            btn_image_external_binding_->setChecked(false);
            btn_image_external_binding_->setToolTip(QStringLiteral("Bind image path to external data"));
        }
        set_image_preview_label(lbl_image_preview_, QString());
        if (cmb_image_scale_filter_) {
            QSignalBlocker block(cmb_image_scale_filter_);
            cmb_image_scale_filter_->setCurrentIndex(1);
        }
        if (cmb_image_box_mode_) {
            QSignalBlocker block(cmb_image_box_mode_);
            int mode_index = cmb_image_box_mode_->findData((int)ImageBoxMode::FitImageToBox);
            cmb_image_box_mode_->setCurrentIndex(mode_index >= 0 ? mode_index : 0);
        }
        if (chk_image_crop_when_outside_box_) {
            QSignalBlocker block(chk_image_crop_when_outside_box_);
            chk_image_crop_when_outside_box_->setChecked(false);
        }
        if (btn_image_anchor_grid_) { btn_image_anchor_grid_->setProperty("active_index", 4); btn_image_anchor_grid_->update(); }
        style_color_button(btn_text_color_, 0xFFFFFFFF);
        if (btn_text_color_) btn_text_color_->setEnabled(true);
        if (btn_kf_text_color_) btn_kf_text_color_->setEnabled(true);
        style_color_button(btn_fill_color_, 0xFF222222);
        if (cmb_fill_type_) cmb_fill_type_->setCurrentIndex(0);
        if (cmb_gradient_type_) cmb_gradient_type_->setCurrentIndex(0);
        if (cmb_gradient_spread_) cmb_gradient_spread_->setCurrentIndex(0);
        if (btn_gradient_start_color_) style_color_button(btn_gradient_start_color_, 0xFF4B6EA8);
        if (btn_gradient_end_color_) style_color_button(btn_gradient_end_color_, 0xFF1B1B1B);
        if (btn_appearance_fill_color_) { style_color_button(btn_appearance_fill_color_, 0xFF222222); btn_appearance_fill_color_->setText(QString()); }
        if (btn_appearance_stroke_color_) {
            style_color_button(btn_appearance_stroke_color_, 0xFF000000);
            btn_appearance_stroke_color_->setText(QString());
            btn_appearance_stroke_color_->setVisible(false);
        }
        if (btn_appearance_stroke_label_) btn_appearance_stroke_label_->setVisible(false);
        if (spn_appearance_stroke_width_) {
            spn_appearance_stroke_width_->setValue(0.0);
            spn_appearance_stroke_width_->setVisible(false);
        }
        if (spn_appearance_opacity_) spn_appearance_opacity_->setValue(100.0);
        if (spn_gradient_start_pos_) spn_gradient_start_pos_->setValue(0.0);
        if (spn_gradient_end_pos_) spn_gradient_end_pos_->setValue(1.0);
        if (spn_gradient_start_opacity_) spn_gradient_start_opacity_->setValue(1.0);
        if (spn_gradient_end_opacity_) spn_gradient_end_opacity_->setValue(1.0);
        if (spn_gradient_opacity_) spn_gradient_opacity_->setValue(1.0);
        if (spn_gradient_angle_) spn_gradient_angle_->setValue(0.0);
        if (spn_gradient_center_x_) spn_gradient_center_x_->setValue(0.5);
        if (spn_gradient_center_y_) spn_gradient_center_y_->setValue(0.5);
        if (spn_gradient_scale_) spn_gradient_scale_->setValue(1.0);
        if (spn_gradient_focal_x_) spn_gradient_focal_x_->setValue(0.5);
        if (spn_gradient_focal_y_) spn_gradient_focal_y_->setValue(0.5);
        spn_layer_w_->setValue(0.0);
        spn_layer_h_->setValue(0.0);
        if (spn_image_box_w_) spn_image_box_w_->setValue(0.0);
        if (spn_image_box_h_) spn_image_box_h_->setValue(0.0);
        if (spn_rect_corner_tl_) spn_rect_corner_tl_->setValue(0.0);
        if (spn_rect_corner_tr_) spn_rect_corner_tr_->setValue(0.0);
        if (spn_rect_corner_br_) spn_rect_corner_br_->setValue(0.0);
        if (spn_rect_corner_bl_) spn_rect_corner_bl_->setValue(0.0);
        if (spn_corner_bevel_roundness_) spn_corner_bevel_roundness_->setValue(100.0);
        spn_size_->setValue(72);
        if (cmb_font_style_) populate_font_style_combo(cmb_font_style_, cmb_font_->currentText(), fxm_tr("OBSTitles.Regular"));
        chk_bold_->setChecked(false);
        chk_italic_->setChecked(false);
        if (chk_font_kerning_) chk_font_kerning_->setChecked(true);
        if (spn_text_leading_) spn_text_leading_->setValue(0.0);
        if (spn_char_tracking_) spn_char_tracking_->setValue(0.0);
        if (cmb_kerning_mode_) cmb_kerning_mode_->setCurrentIndex(0);
        if (spn_kerning_value_) spn_kerning_value_->setValue(0.0);
        if (spn_scale_x_) spn_scale_x_->setValue(100.0);
        if (spn_scale_y_) spn_scale_y_->setValue(100.0);
        if (spn_transform_size_w_) spn_transform_size_w_->setValue(0.0);
        if (spn_transform_size_h_) spn_transform_size_h_->setValue(0.0);
        if (chk_scale_lock_) chk_scale_lock_->setChecked(true);
        if (chk_transform_size_lock_) chk_transform_size_lock_->setChecked(true);
        if (chk_image_box_size_lock_) chk_image_box_size_lock_->setChecked(false);
        if (spn_char_scale_x_) spn_char_scale_x_->setValue(100.0);
        if (spn_char_scale_y_) spn_char_scale_y_->setValue(100.0);
        if (spn_baseline_shift_) spn_baseline_shift_->setValue(0.0);
        for (auto *b : {btn_all_caps_, btn_small_caps_, btn_superscript_, btn_subscript_, btn_underline_,
                        btn_strikethrough_, btn_ligatures_, btn_stylistic_alternates_, btn_fractions_, btn_opentype_features_})
            if (b) b->setChecked(false);
        if (cmb_text_style_) cmb_text_style_->setCurrentIndex(0);
        if (cmb_text_overflow_) cmb_text_overflow_->setCurrentIndex(0);
        if (cmb_ticker_playback_mode_) cmb_ticker_playback_mode_->setCurrentIndex(0);
        if (btn_ticker_pause_) {
            btn_ticker_pause_->setIcon(obs_icon("pause.svg"));
            btn_ticker_pause_->setText(fxm_tr("OBSTitles.TickerPause"));
        }
        if (spn_text_fit_min_scale_) spn_text_fit_min_scale_->setValue(0.5);
        if (lbl_text_fit_scale_) lbl_text_fit_scale_->setText(fxm_tr("OBSTitles.Scale100"));
        if (chk_text_box_width_to_text_) chk_text_box_width_to_text_->setChecked(false);
        if (chk_text_box_height_to_text_) chk_text_box_height_to_text_->setChecked(false);
        if (spn_max_text_box_width_) { spn_max_text_box_width_->setValue(1920.0); spn_max_text_box_width_->setEnabled(false); }
        if (spn_max_text_box_height_) { spn_max_text_box_height_->setValue(1080.0); spn_max_text_box_height_->setEnabled(false); }
        if (grp_text_align_ && grp_text_align_->button(1)) grp_text_align_->button(1)->setChecked(true);
        if (grp_text_valign_ && grp_text_valign_->button(1)) grp_text_valign_->button(1)->setChecked(true);
        if (spn_paragraph_indent_left_) spn_paragraph_indent_left_->setValue(0.0);
        if (spn_paragraph_indent_right_) spn_paragraph_indent_right_->setValue(0.0);
        if (spn_paragraph_indent_first_line_) spn_paragraph_indent_first_line_->setValue(0.0);
        if (spn_paragraph_space_before_) spn_paragraph_space_before_->setValue(0.0);
        if (spn_paragraph_space_after_) spn_paragraph_space_after_->setValue(0.0);
        if (chk_paragraph_hyphenate_) chk_paragraph_hyphenate_->setChecked(false);
        if (cmb_anchor_) cmb_anchor_->setCurrentIndex(4);
        if (btn_anchor_grid_) { btn_anchor_grid_->setProperty("active_index", 4); btn_anchor_grid_->update(); }
        for (auto *b : {btn_kf_pos_x_, btn_kf_pos_y_, btn_kf_scale_x_, btn_kf_scale_y_,
                        btn_kf_rotation_, btn_kf_opacity_, btn_kf_origin_x_, btn_kf_origin_y_,
                        btn_kf_appearance_fill_, btn_kf_appearance_stroke_,
                        btn_kf_paragraph_indent_left_, btn_kf_paragraph_indent_right_, btn_kf_paragraph_indent_first_line_,
                        btn_kf_width_, btn_kf_transform_size_,
                        btn_kf_text_color_, btn_kf_fill_color_}) {
            if (!b) continue;
            b->setIcon(fxm_keyframe_diamond_icon(false));
            b->setProperty("active", false);
            b->setProperty("outlined", false);
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
        loading_values_ = false;
        return;
    }

    const bool is_text = layer_->type == LayerType::Text;
    const bool is_clock = layer_->type == LayerType::Clock;
    const bool is_ticker = layer_->type == LayerType::Ticker;
    const bool is_text_like = is_text || is_clock || is_ticker;
    const bool is_adjustment = layer_->type == LayerType::Adjustment;
    const bool is_color_solid = layer_->type == LayerType::ColorSolid;
    const bool is_transition_input = layer_type_is_transition_input(layer_->type);
    const bool is_fixed_canvas_layer = false;
    const bool is_rect = layer_->type == LayerType::SolidRect ||
                         layer_->type == LayerType::Shape ||
                         is_transition_input || is_color_solid;
    const bool is_image = layer_type_is_image_like(layer_->type);
    const bool is_asset = layer_->type == LayerType::Asset;
    const bool is_audio = layer_->type == LayerType::Audio;
    const bool is_video = layer_->type == LayerType::Video;
    const bool is_empty = layer_->type == LayerType::Empty;
    const bool is_light = layer_->type == LayerType::Light;
    const bool is_chart = layer_->type == LayerType::Chart;
    const bool is_scene_mask_layer = layer_->use_as_scene_mask;
    const bool supports_outline = is_text_like || is_rect || is_image;
    const bool video_has_audio = is_video && title_ && std::any_of(title_->layers.begin(), title_->layers.end(),
        [this](const std::shared_ptr<Layer> &candidate) {
            return candidate && candidate->type == LayerType::Audio &&
                   candidate->linked_media_stream &&
                   candidate->linked_media_layer_id == layer_->id;
        });
    const bool show_audio_controls = is_audio || video_has_audio;
    auto set_form_row_visible = [](QWidget *field, bool visible) {
        if (!field)
            return;
        field->setVisible(visible);
        for (QWidget *cursor = field; cursor; cursor = cursor->parentWidget()) {
            QWidget *parent = cursor->parentWidget();
            if (!parent)
                continue;
            if (auto *form = qobject_cast<QFormLayout *>(parent->layout())) {
                cursor->setVisible(visible);
                if (QWidget *label = form->labelForField(cursor))
                    label->setVisible(visible);
                break;
            }
        }
    };

    const bool show_fill_expose = (is_text_like || is_rect) && !is_scene_mask_layer;
    const bool show_stroke_expose = (is_text_like || is_rect) && supports_outline;
    const bool show_fill_single_value = show_fill_expose && layer_->expose_fill_color;
    const bool show_stroke_single_value = show_stroke_expose && layer_->expose_stroke_color;
    set_form_row_visible(chk_expose_fill_, show_fill_expose);
    if (chk_exposed_fill_single_value_)
        chk_exposed_fill_single_value_->setEnabled(show_fill_single_value);
    set_form_row_visible(chk_exposed_fill_single_value_, show_fill_single_value);
    set_form_row_visible(chk_expose_stroke_, show_stroke_expose);
    if (chk_exposed_stroke_single_value_)
        chk_exposed_stroke_single_value_->setEnabled(show_stroke_single_value);
    set_form_row_visible(chk_exposed_stroke_single_value_, show_stroke_single_value);

    if (transform_box_) transform_box_->setVisible(!is_fixed_canvas_layer && !is_audio);
    if (light_options_box_) {
        light_options_box_->setVisible(is_light);
        if (auto *panel = fxm_panel_for_content(light_options_box_))
            panel->setVisible(is_light);
    }
    if (video_box_) video_box_->setVisible(is_video);
    if (live_playback_box_) live_playback_box_->setVisible(false);
    if (audio_box_) {
        audio_box_->setVisible(show_audio_controls);
        /* The collapsible panel owns the only visible section title. Keeping
         * the inner QGroupBox untitled avoids the duplicated heading below it. */
        audio_box_->setTitle(QString());
        if (auto *panel = fxm_panel_for_content(audio_box_)) {
            panel->setTitle(is_video ? fxm_tr("OBSTitles.VideoAudio")
                                     : fxm_tr("OBSTitles.AudioLayer"));
            panel->setVisible(show_audio_controls);
        }
    }
    if (btn_audio_browse_) btn_audio_browse_->setVisible(is_audio);
    if (asset_box_) asset_box_->setVisible(is_asset);
    for (QWidget *section : {static_cast<QWidget *>(chart_box_),
                             static_cast<QWidget *>(chart_data_box_),
                             static_cast<QWidget *>(chart_axes_box_),
                             static_cast<QWidget *>(chart_labels_box_),
                             static_cast<QWidget *>(chart_appearance_box_),
                             static_cast<QWidget *>(chart_animation_box_)}) {
        if (!section) continue;
        section->setVisible(is_chart);
        if (auto *panel = fxm_panel_for_content(section)) panel->setVisible(is_chart);
    }
    if (is_chart) {
        const double chart_time = std::clamp(
            playhead_ - layer_->in_time, 0.0,
            std::max(0.0, layer_->out_time - layer_->in_time));
        const bool bar_chart = layer_->chart_type == ChartType::Bar ||
                               layer_->chart_type == ChartType::HorizontalBar;
        const bool pie_chart = layer_->chart_type == ChartType::Pie ||
                               layer_->chart_type == ChartType::Donut;
        const bool line_chart = layer_->chart_type == ChartType::Line ||
                                layer_->chart_type == ChartType::Area;
        const ChartType displayed_type = bar_chart ? ChartType::Bar : layer_->chart_type;
        const int type_index = cmb_chart_type_->findData(
            static_cast<int>(displayed_type));
        cmb_chart_type_->setCurrentIndex(type_index >= 0 ? type_index : 0);
        cmb_chart_orientation_->setCurrentIndex(
            layer_->chart_type == ChartType::HorizontalBar ? 1 : 0);
        auto set_chart_field_visible = [](QWidget *section, QWidget *field,
                                          bool visible) {
            if (!field) return;
            field->setVisible(visible);
            if (!section) return;
            for (QFormLayout *form : section->findChildren<QFormLayout *>()) {
                if (QWidget *label = form->labelForField(field))
                    label->setVisible(visible);
            }
        };
        auto set_chart_section_visible = [](QWidget *section, bool visible) {
            if (!section) return;
            section->setVisible(visible);
            if (auto *panel = fxm_panel_for_content(section)) panel->setVisible(visible);
        };
        set_chart_field_visible(chart_box_, cmb_chart_orientation_, bar_chart);
        set_chart_section_visible(chart_axes_box_, !pie_chart);
        set_chart_field_visible(chart_appearance_box_, row_chart_bar_width_, bar_chart);
        set_chart_field_visible(chart_appearance_box_, row_chart_bar_spacing_, bar_chart);
        set_chart_field_visible(chart_appearance_box_, row_chart_corner_radius_, bar_chart);
        set_chart_field_visible(chart_appearance_box_, row_chart_line_thickness_,
                                line_chart || layer_->chart_type == ChartType::Scatter);
        const int data_mode_index = cmb_chart_data_mode_->findData(
            static_cast<int>(layer_->chart_data_mode));
        cmb_chart_data_mode_->setCurrentIndex(data_mode_index >= 0 ? data_mode_index : 0);
        const bool external_chart_data =
            layer_->chart_data_mode == ChartDataMode::ExternalData;
        for (QWidget *control : {static_cast<QWidget *>(row_chart_source_),
                                 static_cast<QWidget *>(cmb_chart_table_),
                                 static_cast<QWidget *>(cmb_chart_category_),
                                 static_cast<QWidget *>(cmb_chart_series_field_),
                                 static_cast<QWidget *>(cmb_chart_secondary_field_),
                                 static_cast<QWidget *>(cmb_chart_metadata_field_),
                                 static_cast<QWidget *>(cmb_chart_color_field_),
                                 static_cast<QWidget *>(spn_chart_start_row_),
                                 static_cast<QWidget *>(spn_chart_maximum_rows_),
                                 static_cast<QWidget *>(chk_chart_ignore_empty_)}) {
            set_chart_field_visible(chart_data_box_, control, external_chart_data);
        }
        row_chart_values_->setVisible(!external_chart_data);
        tbl_chart_series_->setColumnHidden(2, !external_chart_data);
        tbl_chart_series_->setColumnHidden(4,
            !(bar_chart || pie_chart || layer_->chart_type == ChartType::Area));
        tbl_chart_series_->setColumnHidden(6,
            !(line_chart || layer_->chart_type == ChartType::Scatter));

        cmb_chart_source_->clear();
        cmb_chart_source_->addItem(QString(), QString());
        if (title_) {
            for (const ExternalDataSourceDefinition &source : title_->external_data_sources) {
                cmb_chart_source_->addItem(QString::fromStdString(source.name),
                                           QString::fromStdString(source.id));
            }
        }
        int source_index = cmb_chart_source_->findData(
            QString::fromStdString(layer_->chart_binding.source_id));
        if (source_index < 0 && !layer_->chart_binding.source_id.empty()) {
            cmb_chart_source_->addItem(
                QString::fromStdString(layer_->chart_binding.source_id),
                QString::fromStdString(layer_->chart_binding.source_id));
            source_index = cmb_chart_source_->count() - 1;
        }
        cmb_chart_source_->setCurrentIndex(std::max(0, source_index));

        const auto tables = ExternalDataManager::instance().table_snapshots(
            layer_->chart_binding.source_id);
        cmb_chart_table_->clear();
        for (const ExternalDataTableSnapshot &table : tables)
            cmb_chart_table_->addItem(QString::fromStdString(table.path));
        if (cmb_chart_table_->findText(QString::fromStdString(
                layer_->chart_binding.table_path)) < 0)
            cmb_chart_table_->addItem(
                QString::fromStdString(layer_->chart_binding.table_path));
        cmb_chart_table_->setCurrentText(
            QString::fromStdString(layer_->chart_binding.table_path));

        const ExternalDataTableSnapshot snapshot =
            ExternalDataManager::instance().table_snapshot(
                layer_->chart_binding.source_id,
                layer_->chart_binding.table_path);
        const std::vector<std::pair<QComboBox *, std::string>> mapping_fields = {
            {cmb_chart_category_, layer_->chart_binding.category_field},
            {cmb_chart_series_field_, layer_->chart_binding.series_field},
            {cmb_chart_secondary_field_, layer_->chart_binding.secondary_value_field},
            {cmb_chart_metadata_field_, layer_->chart_binding.metadata_field},
            {cmb_chart_color_field_, layer_->chart_binding.color_field}};
        for (const auto &mapping : mapping_fields) {
            mapping.first->clear();
            mapping.first->addItem(QString());
            for (const std::string &column : snapshot.columns)
                mapping.first->addItem(QString::fromStdString(column));
            if (mapping.first->findText(QString::fromStdString(mapping.second)) < 0)
                mapping.first->addItem(QString::fromStdString(mapping.second));
            mapping.first->setCurrentText(QString::fromStdString(mapping.second));
        }

        tbl_chart_series_->setRowCount(
            static_cast<int>(layer_->chart_series.size()));
        for (int row = 0; row < static_cast<int>(layer_->chart_series.size()); ++row) {
            const ChartSeries &series = layer_->chart_series[static_cast<size_t>(row)];
            auto *visible = new QTableWidgetItem();
            visible->setFlags((visible->flags() | Qt::ItemIsUserCheckable) &
                              ~Qt::ItemIsEditable);
            visible->setCheckState(series.visible ? Qt::Checked : Qt::Unchecked);
            tbl_chart_series_->setItem(row, 0, visible);
            tbl_chart_series_->setItem(row, 1, new QTableWidgetItem(
                QString::fromStdString(series.name)));
            tbl_chart_series_->setItem(row, 2, new QTableWidgetItem(
                QString::fromStdString(series.value_field)));
            auto *series_color = new QPushButton(tbl_chart_series_);
            series_color->setFixedHeight(20);
            fxm_apply_editor_field_style(series_color);
            style_color_button(series_color, series.color);
            series_color->setToolTip(fxm_tr("OBSTitles.ChartSeriesColorTooltip"));
            connect(series_color, &QPushButton::clicked, this,
                    [this, row, series_color]() {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Chart ||
                    row < 0 || row >= static_cast<int>(layer_->chart_series.size()))
                    return;
                ChartSeries &item = layer_->chart_series[static_cast<size_t>(row)];
                const QColor selected = fxm_pick_color(
                    QColor::fromRgba(static_cast<QRgb>(item.color)), this,
                    fxm_tr("OBSTitles.ChartSeriesColor"));
                if (!selected.isValid()) return;
                item.color = static_cast<uint32_t>(selected.rgba());
                item.fill_color = (item.color & 0x00FFFFFFu) |
                                  (item.fill_color & 0xFF000000u);
                style_color_button(series_color, item.color);
                emit property_changed(true);
            });
            tbl_chart_series_->setCellWidget(row, 3, series_color);
            for (int column = 4; column <= 5; ++column) {
                auto *toggle = new QTableWidgetItem();
                toggle->setFlags((toggle->flags() | Qt::ItemIsUserCheckable) &
                                 ~Qt::ItemIsEditable);
                toggle->setCheckState(
                    (column == 4 ? series.fill_enabled : series.outline_enabled)
                        ? Qt::Checked : Qt::Unchecked);
                tbl_chart_series_->setItem(row, column, toggle);
            }
            tbl_chart_series_->setItem(row, 6, new QTableWidgetItem(
                series.line_thickness < 0.0 ? QStringLiteral("Auto")
                                            : QString::number(series.line_thickness)));
        }
        tbl_chart_values_->setRowCount(static_cast<int>(layer_->chart_values.size()));
        for (int row = 0; row < static_cast<int>(layer_->chart_values.size()); ++row) {
            const ChartValue &value = layer_->chart_values[static_cast<size_t>(row)];
            auto *expose = new QTableWidgetItem();
            expose->setFlags((expose->flags() | Qt::ItemIsUserCheckable) &
                             ~Qt::ItemIsEditable);
            expose->setCheckState(value.expose_in_dock ? Qt::Checked : Qt::Unchecked);
            expose->setToolTip(fxm_tr("OBSTitles.ChartExposeTooltip"));
            tbl_chart_values_->setItem(row, 0, expose);
            tbl_chart_values_->setItem(row, 1, new QTableWidgetItem(
                QString::fromStdString(value.category)));
            auto *series_combo = new QComboBox(tbl_chart_values_);
            for (const ChartSeries &series : layer_->chart_series) {
                series_combo->addItem(QString::fromStdString(
                    series.name.empty() ? series.id : series.name),
                    QString::fromStdString(series.id));
            }
            int selected_series = series_combo->findData(
                QString::fromStdString(value.series_id));
            if (selected_series < 0 && !value.series_id.empty()) {
                series_combo->addItem(QString::fromStdString(value.series_id),
                                      QString::fromStdString(value.series_id));
                selected_series = series_combo->count() - 1;
            }
            series_combo->setCurrentIndex(std::max(0, selected_series));
            connect(series_combo, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this, row, series_combo](int index) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Chart ||
                    row < 0 || row >= static_cast<int>(layer_->chart_values.size()) ||
                    index < 0)
                    return;
                layer_->chart_values[static_cast<size_t>(row)].series_id =
                    series_combo->itemData(index).toString().toStdString();
                emit property_changed(true);
            });
            tbl_chart_values_->setCellWidget(row, 2, series_combo);
            tbl_chart_values_->setItem(row, 3, new QTableWidgetItem(
                QString::number(value.value, 'g', 15)));
            tbl_chart_values_->setItem(row, 4, new QTableWidgetItem(
                value.has_secondary_value
                    ? QString::number(value.secondary_value, 'g', 15) : QString()));
            const ChartSeries *value_series = nullptr;
            for (const ChartSeries &series : layer_->chart_series) {
                if (series.id == value.series_id || series.name == value.series_id) {
                    value_series = &series;
                    break;
                }
            }
            const uint32_t displayed_color = value.color_override
                ? value.color
                : (value_series ? value_series->color : 0xFF4C8DFFu);
            auto *value_color = new QPushButton(tbl_chart_values_);
            value_color->setFixedHeight(20);
            fxm_apply_editor_field_style(value_color);
            style_color_button(value_color, displayed_color);
            value_color->setToolTip(value.color_override
                ? fxm_tr("OBSTitles.ChartValueColorTooltip")
                : fxm_tr("OBSTitles.ChartInheritedColorTooltip"));
            value_color->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(value_color, &QPushButton::clicked, this,
                    [this, row, value_color]() {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Chart ||
                    row < 0 || row >= static_cast<int>(layer_->chart_values.size()))
                    return;
                ChartValue &item = layer_->chart_values[static_cast<size_t>(row)];
                const QColor selected = fxm_pick_color(
                    QColor::fromRgba(static_cast<QRgb>(item.color)), this,
                    fxm_tr("OBSTitles.ChartValueColor"));
                if (!selected.isValid()) return;
                item.color_override = true;
                item.color = static_cast<uint32_t>(selected.rgba());
                style_color_button(value_color, item.color);
                value_color->setToolTip(fxm_tr("OBSTitles.ChartValueColorTooltip"));
                emit property_changed(true);
            });
            connect(value_color, &QWidget::customContextMenuRequested, this,
                    [this, row, value_color](const QPoint &position) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Chart ||
                    row < 0 || row >= static_cast<int>(layer_->chart_values.size()))
                    return;
                QMenu menu(value_color);
                QAction *inherit = menu.addAction(
                    fxm_tr("OBSTitles.ChartUseSeriesColor"));
                if (menu.exec(value_color->mapToGlobal(position)) != inherit)
                    return;
                layer_->chart_values[static_cast<size_t>(row)].color_override = false;
                emit property_changed(true);
                load_values();
            });
            tbl_chart_values_->setCellWidget(row, 5, value_color);
            tbl_chart_values_->setItem(row, 6, new QTableWidgetItem(
                QString::fromStdString(value.metadata)));
        }
        chk_chart_grid_->setChecked(layer_->chart_grid_enabled);
        chk_chart_axes_->setChecked(layer_->chart_axes_enabled);
        chk_chart_axis_labels_->setChecked(layer_->chart_axis_labels_enabled);
        chk_chart_data_labels_->setChecked(layer_->chart_data_labels_enabled);
        chk_chart_legend_->setChecked(layer_->chart_legend_enabled);
        chk_chart_background_->setChecked(layer_->chart_background_enabled);
        chk_chart_fill_->setChecked(layer_->chart_fill_enabled);
        chk_chart_outline_->setChecked(layer_->chart_outline_enabled);
        chk_chart_thousands_->setChecked(layer_->chart_thousands_separator);
        const int fill_style_index = cmb_chart_fill_style_->findData(layer_->fill_type);
        cmb_chart_fill_style_->setCurrentIndex(fill_style_index >= 0 ? fill_style_index : 0);
        btn_chart_gradient_->setEnabled(layer_->fill_type == 1);
        style_gradient_button(btn_chart_gradient_, layer_->gradient_start_color,
                              layer_->gradient_end_color, layer_->gradient_type);
        btn_chart_gradient_->setText(QString());
        chk_chart_axis_labels_->setEnabled(layer_->chart_axes_enabled);
        btn_chart_axis_color_->setEnabled(layer_->chart_axes_enabled);
        btn_chart_grid_color_->setEnabled(layer_->chart_grid_enabled);
        btn_chart_background_color_->setEnabled(layer_->chart_background_enabled);
        chk_chart_ignore_empty_->setChecked(layer_->chart_binding.ignore_empty_rows);
        spn_chart_start_row_->setValue(layer_->chart_binding.start_row);
        spn_chart_maximum_rows_->setValue(layer_->chart_binding.maximum_rows);
        spn_chart_precision_->setValue(layer_->chart_decimal_precision);
        spn_chart_transition_->setValue(layer_->chart_data_transition_seconds);
        edt_chart_font_family_->setText(QString::fromStdString(layer_->chart_font_family));
        edt_chart_number_prefix_->setText(QString::fromStdString(layer_->chart_number_prefix));
        edt_chart_number_suffix_->setText(QString::fromStdString(layer_->chart_number_suffix));
        for (const auto &entry : {
                 std::pair<QPushButton *, uint32_t>{btn_chart_background_color_, layer_->chart_background_color},
                 {btn_chart_grid_color_, layer_->chart_grid_color},
                 {btn_chart_axis_color_, layer_->chart_axis_color},
                 {btn_chart_label_color_, layer_->chart_label_color}}) {
            style_color_button(entry.first, entry.second);
            entry.first->setText(QString());
        }
        spn_chart_animation_->setValue(
            layer_->chart_animation_progress.evaluate(chart_time) * 100.0);
        spn_chart_reveal_->setValue(
            layer_->chart_reveal_progress.evaluate(chart_time) * 100.0);
        spn_chart_value_transition_->setValue(
            layer_->chart_value_transition.evaluate(chart_time) * 100.0);
        spn_chart_label_opacity_->setValue(
            layer_->chart_label_opacity.evaluate(chart_time) * 100.0);
        spn_chart_element_opacity_->setValue(
            layer_->chart_element_opacity.evaluate(chart_time) * 100.0);
        spn_chart_font_size_->setValue(layer_->chart_font_size.evaluate(chart_time));
        spn_chart_line_thickness_->setValue(
            layer_->chart_line_thickness.evaluate(chart_time));
        spn_chart_bar_width_->setValue(layer_->chart_bar_width.evaluate(chart_time) * 100.0);
        spn_chart_bar_spacing_->setValue(layer_->chart_bar_spacing.evaluate(chart_time) * 100.0);
        spn_chart_corner_radius_->setValue(
            layer_->chart_corner_radius.evaluate(chart_time));
        spn_chart_padding_->setValue(layer_->chart_padding.evaluate(chart_time));
        spn_chart_margin_left_->setValue(layer_->chart_margin_left.evaluate(chart_time));
        spn_chart_margin_top_->setValue(layer_->chart_margin_top.evaluate(chart_time));
        spn_chart_margin_right_->setValue(layer_->chart_margin_right.evaluate(chart_time));
        spn_chart_margin_bottom_->setValue(layer_->chart_margin_bottom.evaluate(chart_time));
    }
    if (is_video) {
        normalize_layer_media_range_to_timeline_span(*layer_, false);
        if (spn_video_in_) spn_video_in_->setValue(layer_->video_in_point);
        if (spn_video_out_) spn_video_out_->setValue(layer_->video_out_point);
        if (cmb_video_playback_) {
            const int i = cmb_video_playback_->findData(std::clamp(layer_->video_playback_mode, 0, 1));
            cmb_video_playback_->setCurrentIndex(i >= 0 ? i : 0);
        }
        if (chk_video_time_remap_) {
            QSignalBlocker block(chk_video_time_remap_);
            chk_video_time_remap_->setChecked(layer_->video_time_remap_enabled);
        }
        if (cmb_video_interpolation_) {
            QSignalBlocker block(cmb_video_interpolation_);
            const int i = cmb_video_interpolation_->findData((int)layer_->video_frame_interpolation);
            cmb_video_interpolation_->setCurrentIndex(i >= 0 ? i : 0);
        }
        if (cmb_video_time_remap_audio_) {
            QSignalBlocker block(cmb_video_time_remap_audio_);
            const int i = cmb_video_time_remap_audio_->findData((int)layer_->video_time_remap_audio_mode);
            cmb_video_time_remap_audio_->setCurrentIndex(i >= 0 ? i : 1);
        }
        if (chk_video_optical_flow_) {
            QSignalBlocker block(chk_video_optical_flow_);
            chk_video_optical_flow_->setChecked(layer_->video_optical_flow_enabled);
        }
        if (lbl_video_optical_flow_status_) {
            const bool needs_analysis = layer_->video_frame_interpolation == VideoFrameInterpolationMode::MotionCompensated &&
                layer_->video_optical_flow_enabled;
            const int progress = fxm::video::FrameRuntime::instance().optical_flow_progress_for_layer(*layer_);
            lbl_video_optical_flow_status_->setText(needs_analysis
                ? QStringLiteral("Background analysis %1% · draft preview until ready").arg(progress)
                : QStringLiteral("Nearest/frame-blend modes do not require optical-flow analysis"));
        }
        const fxm::video::VideoFrameLoadingStatus video_status =
            fxm::video::FrameRuntime::instance().loading_status_for_layer(*layer_);
        if (lbl_video_loading_status_) {
            QString label = QString::fromStdString(video_status.label);
            if (video_status.proxy_stale)
                label += QStringLiteral(" · proxy stale");
            lbl_video_loading_status_->setText(label);
        }
        if (bar_video_loading_) {
            bar_video_loading_->setValue(std::clamp(video_status.progress_percent, 0, 100));
            bar_video_loading_->setVisible(video_status.progress_percent < 100 ||
                                           video_status.proxy_generating ||
                                           video_status.decode_pending);
        }
    }
    // if (is_audio) { legacy contract boundary; video audio now reuses the same controls.
    if (show_audio_controls) {
        normalize_layer_media_range_to_timeline_span(*layer_, false);
        const double media_in = is_video ? layer_->video_in_point : layer_->audio_in_point;
        const double media_out = is_video ? layer_->video_out_point : layer_->audio_out_point;
        const std::string media_source = is_video ? layer_->video_source : layer_->audio_source;
        if (edt_audio_source_) edt_audio_source_->setText(QString::fromStdString(media_source));
        /* Development Version 250: for a selected Video layer, the Video
         * section owns source/range/time controls and the unified Live
         * Properties section owns synchronized/independent transport. Do not
         * repeat those controls in Video Audio. Audio Layer no longer exposes
         * the redundant Preview and Range Tools rows; Range itself remains the
         * direct editable media trim. */
        set_form_row_visible(row_audio_source_, !is_video);
        set_form_row_visible(row_audio_preview_, false);
        set_form_row_visible(row_audio_range_, !is_video);
        set_form_row_visible(row_audio_range_tools_, false);
        set_form_row_visible(cmb_audio_playback_, !is_video);
        set_form_row_visible(chk_audio_loop_, !is_video);
        set_form_row_visible(chk_audio_independent_, !is_video);
        if (spn_audio_in_) spn_audio_in_->setValue(media_in);
        if (spn_audio_out_) spn_audio_out_->setValue(media_out);
        const double clip_span = std::max(0.0, layer_->out_time - layer_->in_time);
        const double clip_local = std::clamp(playhead_ - layer_->in_time, 0.0, clip_span);
        const double media_at_playhead = media_in + clip_local;
        const bool inside = playhead_ >= layer_->in_time && playhead_ <= layer_->out_time;
        if (lbl_audio_range_preview_) {
            lbl_audio_range_preview_->setText(QStringLiteral(
                "Preview In %1 s  |  Out %2 s  |  Strip %3 s")
                .arg(media_in, 0, 'f', 2)
                .arg(media_out, 0, 'f', 2)
                .arg(clip_span, 0, 'f', 2));
        }
        if (lbl_audio_range_feedback_) {
            lbl_audio_range_feedback_->setText(QStringLiteral(
                "%1 · range length follows strip · media %2 s")
                .arg(inside ? QStringLiteral("Inside") : QStringLiteral("Outside"))
                .arg(media_at_playhead, 0, 'f', 2));
            lbl_audio_range_feedback_->setProperty("insideRange", inside);
        }
        const Layer *waveform_layer = layer_.get();
        if (is_video && title_) {
            for (const auto &candidate : title_->layers) {
                if (candidate && candidate->type == LayerType::Audio &&
                    candidate->linked_media_stream &&
                    candidate->linked_media_layer_id == layer_->id) {
                    waveform_layer = candidate.get();
                    break;
                }
            }
        }
        const bool show_audio_waveform_rows = is_video;
        if (lbl_audio_waveform_status_ && waveform_layer) {
            const int waveform_progress = std::clamp(waveform_layer->audio_waveform_progress_percent, 0, 100);
            QString waveform_label = QString::fromStdString(waveform_layer->audio_waveform_progress_label);
            if (waveform_label.isEmpty()) {
                waveform_label = waveform_layer->audio_waveform_generating
                    ? QStringLiteral("Preparing waveform")
                    : (waveform_layer->audio_waveform.empty()
                        ? QStringLiteral("Waveform not generated yet")
                        : QStringLiteral("Waveform ready"));
            }
            if (waveform_layer->audio_waveform_generating)
                waveform_label += QStringLiteral(" %1%").arg(waveform_progress);
            lbl_audio_waveform_status_->setText(waveform_label);
            set_form_row_visible(lbl_audio_waveform_status_,
                                 show_audio_waveform_rows);
        }
        if (bar_audio_waveform_ && waveform_layer) {
            const int waveform_progress = std::clamp(waveform_layer->audio_waveform_progress_percent, 0, 100);
            bar_audio_waveform_->setValue(waveform_layer->audio_waveform_generating
                ? waveform_progress
                : (waveform_layer->audio_waveform.empty() ? 0 : 100));
            set_form_row_visible(
                bar_audio_waveform_,
                show_audio_waveform_rows &&
                    (waveform_layer->audio_waveform_generating ||
                     waveform_layer->audio_waveform.empty()));
        }
        const double audio_local_time = std::clamp(playhead_ - layer_->in_time, 0.0,
            std::max(0.0, layer_->out_time - layer_->in_time));
        const double volume_value = audio_gain_to_db(
            layer_->audio_volume_prop.evaluate(audio_local_time));
        const double pan_value = layer_->audio_pan_prop.evaluate(audio_local_time) * 100.0;
        if (spn_audio_volume_) spn_audio_volume_->setValue(volume_value);
        if (sld_audio_volume_) sld_audio_volume_->setValue(audio_db_to_slider(volume_value));
        if (spn_audio_pan_) spn_audio_pan_->setValue(pan_value);
        if (sld_audio_pan_) sld_audio_pan_->setValue(qRound(pan_value));
        if (btn_kf_audio_volume_) btn_kf_audio_volume_->setIcon(fxm_keyframe_diamond_icon(
            std::any_of(layer_->audio_volume_prop.keyframes.begin(), layer_->audio_volume_prop.keyframes.end(),
                [audio_local_time](const Keyframe &k){ return std::abs(k.time-audio_local_time)<0.0001; })));
        if (btn_kf_audio_pan_) btn_kf_audio_pan_->setIcon(fxm_keyframe_diamond_icon(
            std::any_of(layer_->audio_pan_prop.keyframes.begin(), layer_->audio_pan_prop.keyframes.end(),
                [audio_local_time](const Keyframe &k){ return std::abs(k.time-audio_local_time)<0.0001; })));
        fxm_refresh_keyframe_navigation(btn_kf_audio_volume_);
        fxm_refresh_keyframe_navigation(btn_kf_audio_pan_);
        if (spn_audio_fade_in_) spn_audio_fade_in_->setValue(layer_->audio_fade_in);
        if (spn_audio_fade_out_) spn_audio_fade_out_->setValue(layer_->audio_fade_out);
        if (cmb_audio_fade_curve_) { const int i=cmb_audio_fade_curve_->findData((int)layer_->audio_fade_curve); cmb_audio_fade_curve_->setCurrentIndex(i>=0?i:0); }
        const AudioPlaybackMode playback_mode = is_video && layer_->video_loop
            ? AudioPlaybackMode::Loop : layer_->audio_playback_mode;
        if (cmb_audio_playback_) { const int i=cmb_audio_playback_->findData((int)playback_mode); cmb_audio_playback_->setCurrentIndex(i>=0?i:0); }
        if (chk_audio_mute_) chk_audio_mute_->setChecked(layer_->audio_muted);
        if (chk_audio_solo_) chk_audio_solo_->setChecked(layer_->audio_solo);
        if (chk_audio_loop_) chk_audio_loop_->setChecked(is_video ? (layer_->video_loop || layer_->audio_loop) : layer_->audio_loop);
        if (chk_audio_independent_) chk_audio_independent_->setChecked(is_video ? (layer_->video_playback_mode == 1 || layer_->audio_independent) : layer_->audio_independent);
    }
    if (is_asset) {
        if (lbl_asset_name_) {
            const QString source_name = title_ && !layer_->asset_title_id.empty()
                ? [&]() {
                    for (const auto &candidate : TitleDataStore::instance().title_snapshots())
                        if (candidate && candidate->id == layer_->asset_title_id)
                            return QString::fromStdString(candidate->name);
                    return QString::fromStdString(layer_->name);
                }()
                : QString::fromStdString(layer_->name);
            lbl_asset_name_->setText(source_name);
        }
        if (!layer_->asset_animated)
            layer_->asset_playback_mode = 0;
        if (cmb_asset_playback_) {
            const int index = cmb_asset_playback_->findData(layer_->asset_playback_mode);
            cmb_asset_playback_->setCurrentIndex(index >= 0 ? index : 0);
        }
        if (spn_asset_offset_) spn_asset_offset_->setValue(layer_->asset_playback_offset);
        if (spn_asset_pause_duration_) spn_asset_pause_duration_->setValue(layer_->asset_pause_duration);
        if (spn_asset_loop_count_) spn_asset_loop_count_->setValue(std::max(1, layer_->asset_loop_count));
        if (chk_asset_loop_) chk_asset_loop_->setChecked(layer_->asset_loop);
        update_asset_playback_controls_visibility();
    }
    if (appearance_box_) appearance_box_->setVisible(
        !is_adjustment && !is_audio && !is_empty && !is_light);
    if (auto *panel = fxm_panel_for_content(appearance_box_))
        panel->setVisible(!is_adjustment && !is_audio && !is_empty && !is_light);
    if (btn_transform_defaults_) btn_transform_defaults_->setEnabled(!is_fixed_canvas_layer);
    text_box_->setVisible(is_text_like);
    if (type_options_box_) type_options_box_->setVisible(is_text_like);
    if (paragraph_box_) paragraph_box_->setVisible(is_text_like);
    if (dynamic_text_box_) dynamic_text_box_->setVisible(is_text_like);
    if (auto_style_box_) auto_style_box_->setVisible(is_text_like);
    if (text_animators_box_) text_animators_box_->setVisible(is_text_like);
    if (live_edit_box_)
        live_edit_box_->setVisible(is_rect || is_text_like || is_image ||
                                   is_adjustment || is_color_solid);
    if (bullets_box_) bullets_box_->setVisible(false);
    text_box_->setTitle(is_text ? QStringLiteral("Character") : QString());
    if (row_text_color_) row_text_color_->setVisible(false);
    if (btn_kf_text_color_) btn_kf_text_color_->setVisible(false);
    if (auto *char_grid = qobject_cast<QGridLayout *>(text_box_->layout())) {
        const bool show_text_editor = !is_text;
        if (auto *label_item = char_grid->itemAtPosition(0, 0)) {
            if (auto *label = qobject_cast<QLabel *>(label_item->widget())) {
                label->setText(is_clock ? fxm_tr("OBSTitles.DateTimeFormatLabel")
                                        : fxm_tr("OBSTitles.TextLabel"));
                label->setVisible(show_text_editor);
            }
        }
        if (txt_content_) txt_content_->setVisible(show_text_editor);
        if (btn_text_external_binding_) btn_text_external_binding_->setVisible(show_text_editor);
        if (auto *label_item = char_grid->itemAtPosition(7, 0)) {
            if (auto *label = label_item->widget())
                label->setVisible(false);
        }
        if (auto *label_item = char_grid->itemAtPosition(8, 0)) {
            if (auto *label = label_item->widget())
                label->setVisible(is_clock);
        }
        if (cmb_clock_preset_)
            cmb_clock_preset_->setVisible(is_clock);
    }
    txt_content_->setPlaceholderText(is_clock ? "H:i:s" : fxm_tr("OBSTitles.EnterTextPlaceholder"));
    if (cmb_auto_default_style_ && cmb_auto_rule_style_) {
        obsfxm::StylePresetLibrary library;
        const auto presets = library.presets(obsfxm::StylePresetKind::Text);
        auto fill_combo = [&](QComboBox *combo, bool include_none, const QString &selected) {
            QSignalBlocker blocker(combo);
            combo->clear();
            if (include_none)
                combo->addItem("None", QString());
            else
                combo->addItem("Embedded / learned style", QString());
            for (const auto &preset : presets)
                combo->addItem(preset.name + QStringLiteral(" — ") + preset.category, preset.id);
            int idx = combo->findData(selected);
            if (idx < 0 && !selected.isEmpty()) {
                combo->addItem(QStringLiteral("Missing preset — %1").arg(selected), selected);
                idx = combo->count() - 1;
            }
            combo->setCurrentIndex(idx >= 0 ? idx : 0);
        };
        fill_combo(cmb_auto_default_style_, true, QString::fromStdString(layer_->rich_text.auto_default_style_preset_id));
        const QString first_rule = layer_->rich_text.auto_style_rules.empty()
            ? QString()
            : QString::fromStdString(layer_->rich_text.auto_style_rules.front().style_preset_id);
        fill_combo(cmb_auto_rule_style_, false, first_rule);
        if (chk_auto_style_enabled_) chk_auto_style_enabled_->setChecked(layer_->rich_text.auto_style_enabled);
        auto populate_loaded_rules = [&](QListWidget *list) {
            if (!list) return;
            QSignalBlocker blocker(list);
            list->clear();
            int i = 1;
            for (const auto &rule : layer_->rich_text.auto_style_rules) {
                const QString preset_name = library.displayNameForId(QString::fromStdString(rule.style_preset_id));
                const QString display_name = auto_style_rule_descriptive_name(rule);
                const QString rule_id = QString::fromStdString(rule.rule_id.empty() ? std::to_string(i) : rule.rule_id);
                const QString details = rule.condition_type == "regex"
                    ? QString::fromStdString(rule.regex_pattern)
                    : QStringLiteral("%1 → %2").arg(
                        auto_style_marker_label(rule.start_condition, rule.start_offset, rule.start_custom_chars),
                        auto_style_marker_label(rule.end_condition, rule.end_offset ? rule.end_offset : rule.length, rule.end_custom_chars));
                auto *item = new QListWidgetItem(QStringLiteral("%1. %2%3")
                                                 .arg(i++).arg(display_name)
                                                 .arg(rule.enabled ? QString() : QStringLiteral("  [off]")), list);
                item->setData(Qt::UserRole, i - 2);
                item->setToolTip(QStringLiteral("Rule ID: %1\nPreset: %2\nPattern: %3\n%4")
                                 .arg(rule_id, preset_name, details,
                                      rule.enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled")));
            }
        };
        const int previous_rule_row = lst_auto_style_rules_ ? lst_auto_style_rules_->currentRow() : -1;
        populate_loaded_rules(lst_auto_style_rules_);
        populate_loaded_rules(lst_auto_rules_dialog_);
        if (!layer_->rich_text.auto_style_rules.empty()) {
            const int restored_rule_row = std::clamp(previous_rule_row, 0,
                static_cast<int>(layer_->rich_text.auto_style_rules.size()) - 1);
            lst_auto_style_rules_->setCurrentRow(restored_rule_row);
            if (lst_auto_rules_dialog_) lst_auto_rules_dialog_->setCurrentRow(restored_rule_row);
            const auto &rule = layer_->rich_text.auto_style_rules[static_cast<size_t>(restored_rule_row)];
            int preset_idx = cmb_auto_rule_style_->findData(QString::fromStdString(rule.style_preset_id));
            if (preset_idx < 0) preset_idx = 0;
            cmb_auto_rule_style_->setCurrentIndex(preset_idx);
            int sidx = cmb_auto_rule_start_condition_->findData(QString::fromStdString(rule.start_condition.empty() ? std::string("text_start") : rule.start_condition));
            int eidx = cmb_auto_rule_end_condition_->findData(QString::fromStdString(rule.end_condition.empty() ? std::string("character_index") : rule.end_condition));
            if (chk_auto_rule_enabled_) chk_auto_rule_enabled_->setChecked(rule.enabled);
            if (edt_auto_rule_name_) edt_auto_rule_name_->setText(QString::fromStdString(rule.display_name));
            if (sidx >= 0) cmb_auto_rule_start_condition_->setCurrentIndex(sidx);
            if (eidx >= 0) cmb_auto_rule_end_condition_->setCurrentIndex(eidx);
            spn_auto_rule_start_offset_->setValue((int)rule.start_offset);
            spn_auto_rule_end_offset_->setValue((int)(rule.end_offset ? rule.end_offset : rule.length));
            edt_auto_rule_start_chars_->setText(QString::fromStdString(rule.start_custom_chars));
            edt_auto_rule_end_chars_->setText(QString::fromStdString(rule.end_custom_chars));
            if (cmb_auto_rule_conflict_mode_) { int cidx = cmb_auto_rule_conflict_mode_->findData(QString::fromStdString(rule.conflict_mode.empty() ? std::string("override_previous") : rule.conflict_mode)); if (cidx >= 0) cmb_auto_rule_conflict_mode_->setCurrentIndex(cidx); }
            if (cmb_auto_rule_match_mode_) { int midx = cmb_auto_rule_match_mode_->findData(QString::fromStdString(rule.match_mode.empty() ? std::string("all_matches") : rule.match_mode)); if (midx >= 0) cmb_auto_rule_match_mode_->setCurrentIndex(midx); }
            if (cmb_auto_rule_generalization_) { int gidx = cmb_auto_rule_generalization_->findData(QString::fromStdString(rule.generalization_mode.empty() ? std::string("auto_merge") : rule.generalization_mode)); if (gidx >= 0) cmb_auto_rule_generalization_->setCurrentIndex(gidx); }
            if (chk_auto_rule_prevent_duplicates_) chk_auto_rule_prevent_duplicates_->setChecked(rule.prevent_duplicates);
            if (chk_auto_rule_allow_multiple_cases_) chk_auto_rule_allow_multiple_cases_->setChecked(rule.allow_multiple_cases);
            if (edt_auto_rule_excludes_) edt_auto_rule_excludes_->setText(auto_style_join_ids(rule.excludes_rule_ids));
            if (chk_auto_rule_stop_processing_) chk_auto_rule_stop_processing_->setChecked(rule.stop_processing);
            if (chk_auto_rule_require_stop_match_) chk_auto_rule_require_stop_match_->setChecked(rule.require_stop_match);
            if (chk_auto_rule_include_start_marker_) chk_auto_rule_include_start_marker_->setChecked(rule.include_start_marker);
            if (chk_auto_rule_include_end_marker_) chk_auto_rule_include_end_marker_->setChecked(rule.include_end_marker);
            if (spn_auto_rule_chars_) spn_auto_rule_chars_->setValue((int)(rule.end_offset ? rule.end_offset : rule.length));
        } else {
            if (chk_auto_rule_enabled_) chk_auto_rule_enabled_->setChecked(true);
            if (edt_auto_rule_name_) edt_auto_rule_name_->clear();
            if (cmb_auto_rule_start_condition_) cmb_auto_rule_start_condition_->setCurrentIndex(0);
            if (cmb_auto_rule_end_condition_) cmb_auto_rule_end_condition_->setCurrentIndex(cmb_auto_rule_end_condition_->findData("character_index"));
            if (spn_auto_rule_start_offset_) spn_auto_rule_start_offset_->setValue(0);
            if (spn_auto_rule_end_offset_) spn_auto_rule_end_offset_->setValue(0);
            if (edt_auto_rule_start_chars_) edt_auto_rule_start_chars_->clear();
            if (edt_auto_rule_end_chars_) edt_auto_rule_end_chars_->clear();
            if (cmb_auto_rule_conflict_mode_) cmb_auto_rule_conflict_mode_->setCurrentIndex(0);
            if (cmb_auto_rule_match_mode_) cmb_auto_rule_match_mode_->setCurrentIndex(0);
            if (cmb_auto_rule_generalization_) cmb_auto_rule_generalization_->setCurrentIndex(0);
            if (chk_auto_rule_prevent_duplicates_) chk_auto_rule_prevent_duplicates_->setChecked(true);
            if (chk_auto_rule_allow_multiple_cases_) chk_auto_rule_allow_multiple_cases_->setChecked(true);
            if (edt_auto_rule_excludes_) edt_auto_rule_excludes_->clear();
            if (chk_auto_rule_stop_processing_) chk_auto_rule_stop_processing_->setChecked(false);
            if (chk_auto_rule_require_stop_match_) chk_auto_rule_require_stop_match_->setChecked(true);
            if (chk_auto_rule_include_start_marker_) chk_auto_rule_include_start_marker_->setChecked(true);
            if (chk_auto_rule_include_end_marker_) chk_auto_rule_include_end_marker_->setChecked(false);
            if (spn_auto_rule_chars_) spn_auto_rule_chars_->setValue(0);
        }
    }
    if (spn_text_fit_min_scale_) spn_text_fit_min_scale_->setVisible(is_text_like && layer_->text_overflow_mode == 2 && !is_ticker);
    if (lbl_text_fit_scale_) lbl_text_fit_scale_->setVisible(is_text_like && layer_->text_overflow_mode == 2 && !is_ticker);
    if (auto *dynamic_form = qobject_cast<QFormLayout *>(dynamic_text_box_ ? dynamic_text_box_->layout() : nullptr)) {
        const bool show_ticker_fit = is_text_like && layer_->text_overflow_mode == 2 && !is_ticker;
        if (cmb_text_style_) {
            cmb_text_style_->setVisible(false);
            if (auto *label = dynamic_form->labelForField(cmb_text_style_)) label->setVisible(false);
        }
        if (auto *label = dynamic_form->labelForField(spn_text_fit_min_scale_))
            label->setVisible(show_ticker_fit);
        if (row_ticker_playback_) {
            row_ticker_playback_->setVisible(is_ticker);
            if (auto *label = dynamic_form->labelForField(row_ticker_playback_))
                label->setVisible(is_ticker);
        }
        if (row_ticker_completion_) {
            const bool show_completion = is_ticker && layer_->ticker_playback_mode == static_cast<int>(TickerPlaybackMode::CustomPlayback);
            row_ticker_completion_->setVisible(show_completion);
            if (auto *label = dynamic_form->labelForField(row_ticker_completion_)) label->setVisible(show_completion);
        }
        if (cmb_ticker_style_) {
            cmb_ticker_style_->setVisible(is_ticker);
            if (auto *label = dynamic_form->labelForField(cmb_ticker_style_)) label->setVisible(is_ticker);
        }
        if (spn_ticker_speed_) {
            spn_ticker_speed_->setVisible(is_ticker && layer_->ticker_style != 1);
            if (auto *label = dynamic_form->labelForField(spn_ticker_speed_)) label->setVisible(is_ticker && layer_->ticker_style != 1);
        }
        if (spn_ticker_line_hold_) {
            spn_ticker_line_hold_->setVisible(is_ticker && layer_->ticker_style == 1);
            if (auto *label = dynamic_form->labelForField(spn_ticker_line_hold_)) label->setVisible(is_ticker && layer_->ticker_style == 1);
        }
        if (cmb_ticker_direction_) {
            cmb_ticker_direction_->setVisible(is_ticker);
            if (auto *label = dynamic_form->labelForField(cmb_ticker_direction_)) label->setVisible(is_ticker);
        }
    }
    if (auto *live_form = qobject_cast<QFormLayout *>(live_edit_box_ ? live_edit_box_->layout() : nullptr)) {
        const bool show_scene_mask = layer_type_can_be_scene_mask(layer_->type);
        const bool show_expose_to_dock = is_text || is_ticker || layer_->type == LayerType::Image;
        const bool show_video_live_playback = layer_->type == LayerType::Video;
        const bool show_exposed_options = show_expose_to_dock && layer_->expose_text;
        const bool show_ignore_persistence = !is_empty;
        if (chk_scene_mask_) {
            chk_scene_mask_->setVisible(show_scene_mask);
            if (auto *label = live_form->labelForField(chk_scene_mask_))
                label->setVisible(show_scene_mask);
        }
        if (chk_expose_text_) {
            chk_expose_text_->setVisible(show_expose_to_dock);
            if (auto *label = live_form->labelForField(chk_expose_text_))
                label->setVisible(show_expose_to_dock);
        }
        if (chk_exposed_hide_if_empty_) {
            chk_exposed_hide_if_empty_->setVisible(show_exposed_options);
            chk_exposed_hide_if_empty_->setEnabled(show_exposed_options);
            if (auto *label = live_form->labelForField(chk_exposed_hide_if_empty_))
