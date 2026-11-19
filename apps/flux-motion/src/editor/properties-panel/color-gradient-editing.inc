        auto *selected_stop_row = new QWidget(stops_box);
        auto *selected_stop_layout = new QGridLayout(selected_stop_row);
        selected_stop_layout->setContentsMargins(0, 0, 0, 0);
        selected_stop_layout->setHorizontalSpacing(6);
        selected_stop_layout->setVerticalSpacing(5);
        auto *selected_stop_label = new QLabel(fxm_tr("OBSTitles.NoStopSelected"), selected_stop_row);
        auto *selected_stop_color = new QPushButton(selected_stop_row);
        selected_stop_color->setFixedSize(28, 18);
        auto *selected_stop_location = make_spin(0.0, 100.0, 1.0, 0);
        selected_stop_location->setSuffix(QStringLiteral("%"));
        selected_stop_location->setFixedWidth(58);
        auto *selected_stop_opacity = make_spin(0.0, 100.0, 1.0, 0);
        selected_stop_opacity->setSuffix(QStringLiteral("%"));
        selected_stop_opacity->setFixedWidth(58);
        auto *selected_stop_delete = new QPushButton(QStringLiteral("×"), selected_stop_row);
        selected_stop_delete->setFixedSize(22, 22);
        selected_stop_delete->setToolTip(fxm_tr("OBSTitles.DeleteSelectedStopTooltip"));
        selected_stop_layout->addWidget(selected_stop_label, 0, 0, 1, 5);
        selected_stop_layout->addWidget(new QLabel(fxm_tr("OBSTitles.Color"), selected_stop_row), 1, 0);
        selected_stop_layout->addWidget(selected_stop_color, 1, 1);
        selected_stop_layout->addWidget(new QLabel(fxm_tr("OBSTitles.Location"), selected_stop_row), 1, 2);
        selected_stop_layout->addWidget(selected_stop_location, 1, 3);
        selected_stop_layout->addWidget(selected_stop_delete, 1, 4);
        selected_stop_layout->addWidget(new QLabel(fxm_tr("OBSTitles.Opacity"), selected_stop_row), 2, 2);
        selected_stop_layout->addWidget(selected_stop_opacity, 2, 3);
        stops_layout->addWidget(selected_stop_row);

        bool syncing_selected_stop_controls = false;
        auto *stop_actions = new QWidget(stops_box);
        auto *stop_actions_layout = new QHBoxLayout(stop_actions);
        stop_actions_layout->setContentsMargins(0, 0, 0, 0);
        stop_actions_layout->setSpacing(5);
        auto *add_stop = new QPushButton(fxm_tr("OBSTitles.AddStop"), stop_actions);
        auto *duplicate_stop = new QPushButton(fxm_tr("OBSTitles.Duplicate"), stop_actions);
        auto *sort_stops = new QPushButton(fxm_tr("OBSTitles.Sort"), stop_actions);
        auto *delete_stop = new QPushButton(fxm_tr("OBSTitles.Delete"), stop_actions);
        delete_stop->setToolTip(fxm_tr("OBSTitles.DeleteStopTooltip"));
        duplicate_stop->setToolTip(fxm_tr("OBSTitles.DuplicateStopTooltip"));
        add_stop->setToolTip(fxm_tr("OBSTitles.AddStopTooltip"));
        stop_actions_layout->addWidget(add_stop);
        stop_actions_layout->addWidget(duplicate_stop);
        stop_actions_layout->addWidget(sort_stops);
        stop_actions_layout->addWidget(delete_stop);
        stop_actions_layout->addStretch(1);
        stops_layout->addWidget(stop_actions);
        auto *gradient_color_picker = new color_widgets::ColorDialog(stops_box, Qt::Widget);
        fxm_prepare_embedded_color_dialog(
            gradient_color_picker,
            color_from_argb(stroke
                ? (inline_text_stroke ? initial_inline_stroke.fill.gradient_start_color
                                      : layer_->stroke_gradient_start_color)
                : (inline_text_fill ? initial_inline_fill.gradient_start_color
                                    : layer_->gradient_start_color)));
        properties_limit_gradient_stop_color_dialog(gradient_color_picker);
        if (auto *buttons = gradient_color_picker->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"))) {
            for (auto *button : buttons->buttons()) {
                if (buttons->buttonRole(button) == QDialogButtonBox::ActionRole) {
                    connect(button, &QAbstractButton::clicked, &popup, [this, &popup]() {
                        remember_next_color_popup_position(popup.pos());
                        popup.accept();
                        emit color_picker_tool_requested();
                    });
                }
            }
        }
        gradient_color_picker->setVisible(true);
        gradient_color_picker->setEnabled(false);
        stops_layout->addWidget(gradient_color_picker);
        int active_gradient_stop = -1;
        bool syncing_gradient_color_picker = false;
        auto set_gradient_color_picker_target = [&](int stop_index) {
            if (stop_index < 0 || stop_index >= preview->stop_count()) {
                active_gradient_stop = -1;
                if (layer_ && external_gradient_layer_id_ == layer_->id)
                    external_gradient_stop_index_ = -1;
                gradient_color_picker->setEnabled(false);
                return;
            }

            active_gradient_stop = stop_index;
            external_gradient_layer_id_ = layer_ ? layer_->id : std::string();
            external_gradient_stop_index_ = stop_index;
            external_gradient_stroke_ = stroke;
            external_gradient_inline_ = inline_text_fill || inline_text_stroke;
            gradient_color_picker->setEnabled(true);
            QColor color = color_from_argb(preview->stop_color_argb(stop_index));
            color.setAlphaF(preview->stop_opacity(stop_index));
            syncing_gradient_color_picker = true;
            {
                QSignalBlocker blocker(gradient_color_picker);
                gradient_color_picker->setColor(color);
            }
            syncing_gradient_color_picker = false;
        };

        gradient_layout->addWidget(gradient_form);
        gradient_layout->addWidget(preview);
        fxm_add_panel_section(gradient_layout, stops_box);
        fxm_add_panel_section(gradient_layout, preset_box);
        tabs->addTab(gradient_tab, fxm_tr("OBSTitles.Gradient"));
        // With the Color tab hidden, open directly on Gradient.
        tabs->setCurrentWidget(gradient_tab);

        auto preview_change_in_progress = std::make_shared<bool>(false);

        auto sync_stop_rows = [=, &syncing_selected_stop_controls]() {
            const int selected = preview->selected_stop();
            const bool has_selection = selected >= 0 && selected < preview->stop_count();
            const bool can_delete_selected = has_selection && preview->stop_count() > 2;

            syncing_selected_stop_controls = true;
            selected_stop_label->setText(has_selection
                ? fxm_tr("OBSTitles.SelectedStop").arg(selected < 2 ? (selected == 0 ? fxm_tr("OBSTitles.StartLower") : fxm_tr("OBSTitles.EndLower"))
                                                               : fxm_tr("OBSTitles.IntermediateLower"))
                : fxm_tr("OBSTitles.NoStopSelected"));
            selected_stop_color->setEnabled(has_selection);
            selected_stop_location->setEnabled(has_selection);
            selected_stop_opacity->setEnabled(has_selection);
            selected_stop_delete->setEnabled(can_delete_selected);
            delete_stop->setEnabled(can_delete_selected);
            duplicate_stop->setEnabled(has_selection);

            if (has_selection) {
                style_color_button(selected_stop_color, preview->stop_color_argb(selected));
                selected_stop_color->setText(QString());
                selected_stop_location->setValue(preview->stop_position(selected) * 100.0);
                selected_stop_opacity->setValue(preview->stop_opacity(selected) * 100.0);
            } else {
                selected_stop_color->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:1px solid %2;border-radius:2px;padding:0;}")
                                                   .arg(control_bg_name, border_name));
                selected_stop_color->setText(QString());
                selected_stop_location->setValue(0.0);
                selected_stop_opacity->setValue(0.0);
            }
            syncing_selected_stop_controls = false;
        };
        bool syncing_gradient_model = false;
        auto current_gradient_fill = [=]() {
            RichTextFill fill;
            if (stroke) {
                if (inline_text_stroke)
                    return current_text_char_format().stroke.fill;
                fill.type = layer_->stroke_fill_type == 2 ? 1 : 0;
                fill.color = eval_stroke_color(*layer_, local_time());
                fill.gradient_type = layer_->stroke_gradient_type;
                fill.gradient_spread = layer_->stroke_gradient_spread;
                fill.gradient_start_color = layer_->stroke_gradient_start_color;
                fill.gradient_end_color = layer_->stroke_gradient_end_color;
                fill.gradient_start_pos = layer_->stroke_gradient_start_pos;
                fill.gradient_end_pos = layer_->stroke_gradient_end_pos;
                fill.gradient_start_opacity = layer_->stroke_gradient_start_opacity;
                fill.gradient_end_opacity = layer_->stroke_gradient_end_opacity;
                fill.gradient_opacity = layer_->stroke_gradient_opacity;
                fill.gradient_angle = layer_->stroke_gradient_angle;
                fill.gradient_center_x = layer_->stroke_gradient_center_x;
                fill.gradient_center_y = layer_->stroke_gradient_center_y;
                fill.gradient_scale = layer_->stroke_gradient_scale;
                fill.gradient_focal_x = layer_->stroke_gradient_focal_x;
                fill.gradient_focal_y = layer_->stroke_gradient_focal_y;
                fill.gradient_stops = layer_->stroke_gradient_stops;
                return fill;
            }
            if (inline_text_fill)
                return current_text_char_format().fill;
            fill.type = layer_->fill_type;
            fill.color = text_fill ? layer_->text_color : layer_->fill_color;
            fill.gradient_type = layer_->gradient_type;
            fill.gradient_spread = layer_->gradient_spread;
            fill.gradient_start_color = layer_->gradient_start_color;
            fill.gradient_end_color = layer_->gradient_end_color;
            fill.gradient_start_pos = layer_->gradient_start_pos;
            fill.gradient_end_pos = layer_->gradient_end_pos;
            fill.gradient_start_opacity = layer_->gradient_start_opacity;
            fill.gradient_end_opacity = layer_->gradient_end_opacity;
            fill.gradient_opacity = layer_->gradient_opacity;
            fill.gradient_angle = layer_->gradient_angle;
            fill.gradient_center_x = layer_->gradient_center_x;
            fill.gradient_center_y = layer_->gradient_center_y;
            fill.gradient_scale = layer_->gradient_scale;
            fill.gradient_focal_x = layer_->gradient_focal_x;
            fill.gradient_focal_y = layer_->gradient_focal_y;
            fill.gradient_stops = layer_->gradient_stops;
            return fill;
        };
        auto update_preview = [=, &syncing_gradient_model]() {
            if (!layer_ || syncing_gradient_model)
                return;
            QScopedValueRollback<bool> sync_guard(syncing_gradient_model, true);
            const RichTextFill fill = current_gradient_fill();
            {
                QSignalBlocker b0(type), b1(repeat_mode), b2(start_pos), b3(end_pos),
                    b4(start_opacity), b5(end_opacity), b6(gradient_opacity),
                    b7(angle), b8(center_x), b9(center_y), b10(scale),
                    b11(focal_x), b12(focal_y);
                const int type_index = type->findData(fill.gradient_type);
                if (type_index >= 0) type->setCurrentIndex(type_index);
                const int spread_index = repeat_mode->findData(fill.gradient_spread);
                if (spread_index >= 0) repeat_mode->setCurrentIndex(spread_index);
                start_pos->setValue(fill.gradient_start_pos * 100.0);
                end_pos->setValue(fill.gradient_end_pos * 100.0);
                start_opacity->setValue(fill.gradient_start_opacity * 100.0);
                end_opacity->setValue(fill.gradient_end_opacity * 100.0);
                gradient_opacity->setValue(fill.gradient_opacity);
                angle->setValue(fill.gradient_angle);
                center_x->setValue(fill.gradient_center_x);
                center_y->setValue(fill.gradient_center_y);
                scale->setValue(fill.gradient_scale);
                focal_x->setValue(fill.gradient_focal_x);
                focal_y->setValue(fill.gradient_focal_y);
            }
            preview->set_gradient(fill.gradient_type,
                                  fill.gradient_start_color,
                                  fill.gradient_end_color,
                                  fill.gradient_start_pos,
                                  fill.gradient_end_pos,
                                  fill.gradient_start_opacity,
                                  fill.gradient_end_opacity,
                                  fill.gradient_opacity,
                                  fill.gradient_angle,
                                  fill.gradient_center_x,
                                  fill.gradient_center_y,
                                  fill.gradient_scale);
            preview->set_extra_stops(
                (inline_text_fill || inline_text_stroke)
                    ? fill.gradient_stops
                    : (stroke ? layer_->stroke_gradient_stops : layer_->gradient_stops));
            style_color_button(start_color, fill.gradient_start_color);
            style_color_button(end_color, fill.gradient_end_color);
            start_color->setText(QString());
            end_color->setText(QString());
            sync_stop_rows();
        };
        auto apply_gradient = [=, &popup, &emit_live_visual_change,
                               &syncing_gradient_model]() {
            if (!layer_ || loading_values_ || syncing_gradient_model) return;
            if (stroke) {
                if (inline_text_stroke) {
                    RichTextCharFormat fmt = current_text_char_format();
                    fmt.stroke.enabled = true;
                    if (fmt.stroke.width <= 0.0f)
                        fmt.stroke.width = layer_->stroke_width > 0.0f
                            ? layer_->stroke_width : 1.0f;
                    fmt.stroke.fill.type = 1;
                    fmt.stroke.fill.gradient_type = type->currentData().toInt();
                    fmt.stroke.fill.gradient_spread = repeat_mode->currentData().toInt();
                    fmt.stroke.fill.gradient_start_color = preview->stop_color_argb(0);
                    fmt.stroke.fill.gradient_end_color = preview->stop_color_argb(1);
                    fmt.stroke.fill.gradient_start_pos = (float)(start_pos->value() / 100.0);
                    fmt.stroke.fill.gradient_end_pos = (float)(end_pos->value() / 100.0);
                    fmt.stroke.fill.gradient_start_opacity = (float)(start_opacity->value() / 100.0);
                    fmt.stroke.fill.gradient_end_opacity = (float)(end_opacity->value() / 100.0);
                    fmt.stroke.fill.gradient_opacity = (float)gradient_opacity->value();
                    fmt.stroke.fill.gradient_angle = (float)angle->value();
                    fmt.stroke.fill.gradient_center_x = (float)center_x->value();
                    fmt.stroke.fill.gradient_center_y = (float)center_y->value();
                    fmt.stroke.fill.gradient_scale = (float)scale->value();
                    fmt.stroke.fill.gradient_focal_x = (float)focal_x->value();
                    fmt.stroke.fill.gradient_focal_y = (float)focal_y->value();
                    fmt.stroke.fill.gradient_stops = preview->extra_stops();
                    apply_text_char_format(fmt, RichTextCharStroke);
                } else {
                    layer_->outline_enabled = true;
                    layer_->stroke_fill_type = 2;
                    layer_->stroke_gradient_type = type->currentData().toInt();
                    layer_->stroke_gradient_spread = repeat_mode->currentData().toInt();
                    layer_->stroke_gradient_start_color = preview->stop_color_argb(0);
                    layer_->stroke_gradient_end_color = preview->stop_color_argb(1);
                    layer_->stroke_gradient_start_pos = (float)(start_pos->value() / 100.0);
                    layer_->stroke_gradient_end_pos = (float)(end_pos->value() / 100.0);
                    layer_->stroke_gradient_start_opacity = (float)(start_opacity->value() / 100.0);
                    layer_->stroke_gradient_end_opacity = (float)(end_opacity->value() / 100.0);
                    layer_->stroke_gradient_opacity = (float)gradient_opacity->value();
                    layer_->stroke_gradient_angle = (float)angle->value();
                    layer_->stroke_gradient_center_x = (float)center_x->value();
                    layer_->stroke_gradient_center_y = (float)center_y->value();
                    layer_->stroke_gradient_scale = (float)scale->value();
                    layer_->stroke_gradient_focal_x = (float)focal_x->value();
                    layer_->stroke_gradient_focal_y = (float)focal_y->value();
                    layer_->stroke_gradient_stops = preview->extra_stops();
                }
            } else {
                if (inline_text_fill) {
                    if (text_fill) {
                        RichTextCharFormat fmt = current_text_char_format();
                        fmt.fill.type = 1;
                        fmt.fill.gradient_type = type->currentData().toInt();
                        fmt.fill.gradient_spread = repeat_mode->currentData().toInt();
                        fmt.fill.gradient_start_color = preview->stop_color_argb(0);
                        fmt.fill.gradient_end_color = preview->stop_color_argb(1);
                        fmt.fill.gradient_start_pos = (float)(start_pos->value() / 100.0);
                        fmt.fill.gradient_end_pos = (float)(end_pos->value() / 100.0);
                        fmt.fill.gradient_start_opacity = (float)(start_opacity->value() / 100.0);
                        fmt.fill.gradient_end_opacity = (float)(end_opacity->value() / 100.0);
                        fmt.fill.gradient_opacity = (float)gradient_opacity->value();
                        fmt.fill.gradient_angle = (float)angle->value();
                        fmt.fill.gradient_center_x = (float)center_x->value();
                        fmt.fill.gradient_center_y = (float)center_y->value();
                        fmt.fill.gradient_scale = (float)scale->value();
                        fmt.fill.gradient_focal_x = (float)focal_x->value();
                        fmt.fill.gradient_focal_y = (float)focal_y->value();
                        fmt.fill.gradient_stops = preview->extra_stops();
                        apply_text_char_format(fmt, RichTextCharFillColor);
                    }
                } else {
                    layer_->fill_type = 1;
                    layer_->gradient_type = type->currentData().toInt();
                    layer_->gradient_spread = repeat_mode->currentData().toInt();
                    layer_->gradient_start_color = preview->stop_color_argb(0);
                    layer_->gradient_end_color = preview->stop_color_argb(1);
                    layer_->gradient_start_pos = (float)(start_pos->value() / 100.0);
                    layer_->gradient_end_pos = (float)(end_pos->value() / 100.0);
                    layer_->gradient_start_opacity = (float)(start_opacity->value() / 100.0);
                    layer_->gradient_end_opacity = (float)(end_opacity->value() / 100.0);
                    layer_->gradient_opacity = (float)gradient_opacity->value();
                    layer_->gradient_angle = (float)angle->value();
                    layer_->gradient_center_x = (float)center_x->value();
                    layer_->gradient_center_y = (float)center_y->value();
                    layer_->gradient_scale = (float)scale->value();
                    layer_->gradient_focal_x = (float)focal_x->value();
                    layer_->gradient_focal_y = (float)focal_y->value();
                    layer_->gradient_stops = preview->extra_stops();
                    if (text_fill)
                        apply_text_fill_format();
                }
            }
            if (!*preview_change_in_progress && !inline_text_fill && !inline_text_stroke)
                update_preview();
            else
                sync_stop_rows();
            update_main_swatch();
            emit_live_visual_change();
        };

        /* The gradient popup and the Styles dock now use the same persistent
         * preset library. Every target that opens this selector (shape/text
         * fill, layer stroke, and inline rich-text fill/stroke) therefore sees
         * exactly the same compact swatches and can save its current gradient
         * back to the shared collection. */
        auto gradient_preset_library = std::make_shared<obsfxm::StylePresetLibrary>();
        auto apply_gradient_preset = [=, &apply_gradient](const obsfxm::StylePreset &preset) {
            RichTextFill fill = current_gradient_fill();
            std::vector<GradientStop> extra_stops;
            if (!obsfxm::StylePresetLibrary::gradientPresetToFill(preset, fill, &extra_stops))
                return;

            {
                QSignalBlocker b0(type), b1(repeat_mode), b2(start_pos), b3(end_pos),
                    b4(start_opacity), b5(end_opacity), b6(gradient_opacity),
                    b7(angle), b8(center_x), b9(center_y), b10(scale),
                    b11(focal_x), b12(focal_y);
                const int type_index = type->findData(fill.gradient_type);
                if (type_index >= 0) type->setCurrentIndex(type_index);
                const int spread_index = repeat_mode->findData(fill.gradient_spread);
                if (spread_index >= 0) repeat_mode->setCurrentIndex(spread_index);
                start_pos->setValue(fill.gradient_start_pos * 100.0);
                end_pos->setValue(fill.gradient_end_pos * 100.0);
                start_opacity->setValue(fill.gradient_start_opacity * 100.0);
                end_opacity->setValue(fill.gradient_end_opacity * 100.0);
                gradient_opacity->setValue(fill.gradient_opacity);
                angle->setValue(fill.gradient_angle);
                center_x->setValue(fill.gradient_center_x);
                center_y->setValue(fill.gradient_center_y);
                scale->setValue(fill.gradient_scale);
                focal_x->setValue(fill.gradient_focal_x);
                focal_y->setValue(fill.gradient_focal_y);
            }
            preview->set_gradient(fill.gradient_type,
                                  fill.gradient_start_color,
                                  fill.gradient_end_color,
                                  fill.gradient_start_pos,
                                  fill.gradient_end_pos,
                                  fill.gradient_start_opacity,
                                  fill.gradient_end_opacity,
                                  fill.gradient_opacity,
                                  fill.gradient_angle,
                                  fill.gradient_center_x,
                                  fill.gradient_center_y,
                                  fill.gradient_scale);
            preview->set_extra_stops(extra_stops);
            preview->set_selected_stop(-1);
            apply_gradient();
        };

        auto refresh_gradient_preset_swatches = std::make_shared<std::function<void()>>();
        *refresh_gradient_preset_swatches = [=, &popup, &apply_gradient_preset]() {
            gradient_preset_library->load();
            preset_strip->clearSwatches();
            for (const auto &preset : gradient_preset_library->presets(obsfxm::StylePresetKind::Gradient)) {
                auto *button = new QToolButton(preset_strip);
                button->setObjectName(QStringLiteral("FluxMotionGradientPresetSwatch"));
                button->setFixedSize(28, 28);
                button->setAutoRaise(false);
                button->setIcon(QIcon(obsfxm::StylePresetLibrary::thumbnail(preset, QSize(24, 24))));
                button->setIconSize(QSize(24, 24));
                button->setToolTip(obsfxm::StylePresetLibrary::gradientDescription(preset));
                button->setStyleSheet(QStringLiteral(
                    "QToolButton{padding:1px;border:1px solid %1;border-radius:3px;background:%2;}"
                    "QToolButton:hover{border:1px solid %3;}"
                    "QToolButton:pressed{background:%4;}")
                    .arg(border_name, control_bg_name, panel_text_name, section_bg_name));
                connect(button, &QToolButton::clicked, &popup, [=, &apply_gradient_preset]() {
                    apply_gradient_preset(preset);
                });
                button->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(button, &QToolButton::customContextMenuRequested, &popup,
                        [=](const QPoint &pos) {
                            if (obsfxm::StylePresetLibrary::isBuiltIn(preset))
                                return;
                            QMenu menu(button);
                            QAction *remove_action = menu.addAction(fxm_tr("OBSTitles.DeleteStylePreset"));
                            if (menu.exec(button->mapToGlobal(pos)) == remove_action)
                                gradient_preset_library->remove(preset.id);
                        });
                preset_strip->addSwatch(button);
            }
        };

        obsfxm::StylePresetLibrary::subscribe(&popup, [refresh_gradient_preset_swatches]() {
            if (*refresh_gradient_preset_swatches)
                (*refresh_gradient_preset_swatches)();
        });
        connect(new_preset, &QPushButton::clicked, &popup, [=, &popup]() {
            bool ok = false;
            const QString name = QInputDialog::getText(
                &popup, fxm_tr("OBSTitles.SaveGradientPreset"),
                fxm_tr("OBSTitles.StylePresetName"), QLineEdit::Normal,
                QString(), &ok).trimmed();
            if (!ok || name.isEmpty())
                return;
            const QString category = QInputDialog::getText(
                &popup, fxm_tr("OBSTitles.StylePresetCategory"),
                fxm_tr("OBSTitles.StylePresetCategory"), QLineEdit::Normal,
                QStringLiteral("User"), &ok).trimmed();
            if (!ok)
                return;

            RichTextFill fill = current_gradient_fill();
            fill.type = 1;
            const std::vector<GradientStop> extra_stops =
                (inline_text_fill || inline_text_stroke)
                    ? fill.gradient_stops
                    : (stroke ? layer_->stroke_gradient_stops
                              : layer_->gradient_stops);
            gradient_preset_library->upsert(obsfxm::StylePresetLibrary::makeGradientPreset(
                fill, extra_stops, name,
                category.isEmpty() ? QStringLiteral("User") : category));
        });
        (*refresh_gradient_preset_swatches)();

        auto apply_gradient_stop_color = [=](int stop_index, const QColor &picked) {
            if (!picked.isValid() || stop_index < 0 || stop_index >= preview->stop_count())
                return;
            preview->set_stop_color(stop_index, picked);
        };
        auto show_stop_color_popup = [=, &set_gradient_color_picker_target](int stop_index, const QPoint &global_pos) {
            Q_UNUSED(global_pos);
            if (stop_index < 0 || stop_index >= preview->stop_count())
                return;
            set_gradient_color_picker_target(stop_index);
        };

        connect(tabs, &QTabWidget::currentChanged, &popup, [=, &selected_color](int idx) {
            emit gradient_editor_active_changed(idx == 2);
            if (idx == 0)
                apply_solid_color(selected_color);
            else if (idx == 2)
                apply_gradient();
        });
        connect(this, &PropertiesPanel::gradient_model_refresh_requested,
                &popup, [=]() { update_preview(); });
        connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), &popup, [=](int){ apply_gradient(); });
        connect(repeat_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), &popup, [=](int){ apply_gradient(); });
        connect(type_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                &popup, [=](QAbstractButton *button) {
                    type->setCurrentIndex(std::max(0, type->findData(type_group->id(button))));
                });
        connect(gradient_color_picker, &color_widgets::ColorDialog::colorChanged, &popup,
                [=, &active_gradient_stop, &syncing_gradient_color_picker, &apply_gradient_stop_color](const QColor &color) {
                    if (syncing_gradient_color_picker)
                        return;
                    apply_gradient_stop_color(active_gradient_stop, color);
                });
        preview->selection_changed = [=, &set_gradient_color_picker_target](int stop_index) {
            sync_stop_rows();
            if (gradient_color_picker->isVisible())
                set_gradient_color_picker_target(stop_index);
        };
        preview->gradient_changed = [=]() {
            *preview_change_in_progress = true;
            {
                QSignalBlocker b0(start_pos), b1(end_pos), b2(start_opacity), b3(end_opacity);
                start_pos->setValue(preview->stop_position(0) * 100.0);
                end_pos->setValue(preview->stop_position(1) * 100.0);
                start_opacity->setValue(preview->stop_opacity(0) * 100.0);
                end_opacity->setValue(preview->stop_opacity(1) * 100.0);
            }
            if (stroke && !inline_text_stroke) {
                layer_->stroke_gradient_start_color = preview->stop_color_argb(0);
                layer_->stroke_gradient_end_color = preview->stop_color_argb(1);
                layer_->stroke_gradient_stops = preview->extra_stops();
            } else if (!inline_text_fill) {
                layer_->gradient_start_color = preview->stop_color_argb(0);
                layer_->gradient_end_color = preview->stop_color_argb(1);
                layer_->gradient_stops = preview->extra_stops();
            }
            style_color_button(start_color, preview->stop_color_argb(0));
            style_color_button(end_color, preview->stop_color_argb(1));
            start_color->setText(QString());
            end_color->setText(QString());
            apply_gradient();
            *preview_change_in_progress = false;
        };
        preview->color_popup_requested = [=, &show_stop_color_popup](int stop_index, const QPoint &global_pos) {
            show_stop_color_popup(stop_index, global_pos);
        };
        connect(selected_stop_color, &QPushButton::clicked, &popup, [=, &show_stop_color_popup]() {
            const int selected = preview->selected_stop();
            if (selected < 0) return;
            show_stop_color_popup(selected, preview->stop_global_anchor(selected));
        });
        connect(selected_stop_location, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &popup,
                [=, &syncing_selected_stop_controls](double value) {
                    if (syncing_selected_stop_controls) return;
                    const int selected = preview->selected_stop();
                    if (selected < 0) return;
                    preview->set_stop_position(selected, value / 100.0);
                });
        connect(selected_stop_opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &popup,
                [=, &syncing_selected_stop_controls](double value) {
                    if (syncing_selected_stop_controls) return;
                    const int selected = preview->selected_stop();
                    if (selected < 0) return;
                    preview->set_stop_opacity(selected, value / 100.0);
                });
        connect(selected_stop_delete, &QPushButton::clicked, &popup, [=]() {
            const int selected = preview->selected_stop();
            if (selected < 0 || preview->stop_count() <= 2) return;
            preview->remove_stop(selected);
        });
        for (auto *spin : {start_pos, end_pos, start_opacity, end_opacity, gradient_opacity,
                           angle, center_x, center_y, scale, focal_x, focal_y}) {
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &popup, [=](double){ apply_gradient(); });
        }
        connect(start_color, &QPushButton::clicked, &popup, [=, &show_stop_color_popup]() {
            preview->set_selected_stop(0);
            show_stop_color_popup(0, preview->stop_global_anchor(0));
        });
        connect(end_color, &QPushButton::clicked, &popup, [=, &show_stop_color_popup]() {
            preview->set_selected_stop(1);
            show_stop_color_popup(1, preview->stop_global_anchor(1));
        });
        connect(duplicate_stop, &QPushButton::clicked, &popup, [=]() {
            int src = preview->selected_stop();
            if (src < 0) return;
            QColor color = color_from_argb(preview->stop_color_argb(src));
            color.setAlphaF(preview->stop_opacity(src));
            const double pos = std::clamp(preview->stop_position(src) + 0.08, 0.0, 1.0);
            preview->add_stop(pos, color);
        });
        connect(add_stop, &QPushButton::clicked, &popup, [=]() {
            preview->add_stop(0.5);
        });
        connect(delete_stop, &QPushButton::clicked, &popup, [=]() {
            const int selected = preview->selected_stop();
            if (selected < 0 || preview->stop_count() <= 2) return;
            preview->remove_stop(selected);
        });
        connect(sort_stops, &QPushButton::clicked, &popup, [=]() {
            if (start_pos->value() <= end_pos->value()) return;
            const double p0 = start_pos->value(), p1 = end_pos->value();
            const double o0 = start_opacity->value(), o1 = end_opacity->value();
            const uint32_t c0 = preview->stop_color_argb(0);
            const uint32_t c1 = preview->stop_color_argb(1);
            start_pos->setValue(p1); end_pos->setValue(p0);
            start_opacity->setValue(o1); end_opacity->setValue(o0);
            if (stroke && !inline_text_stroke) {
                layer_->stroke_gradient_start_color = c1;
                layer_->stroke_gradient_end_color = c0;
            } else if (!inline_text_fill) {
                layer_->gradient_start_color = c1;
                layer_->gradient_end_color = c0;
            }
            preview->set_stop_color(0, color_from_argb(c1));
            preview->set_stop_color(1, color_from_argb(c0));
            apply_gradient();
        });

        update_preview();
        const bool is_gradient_value = stroke
            ? (inline_text_stroke ? initial_inline_stroke.fill.type == 1
                                  : layer_->stroke_fill_type == 2)
            : (inline_text_fill ? initial_inline_fill.type == 1
                                                                  : layer_->fill_type == 1);
        tabs->setCurrentIndex(is_gradient_value ? 2 : 0);
        emit gradient_editor_active_changed(is_gradient_value);
        live_visual_dirty = false;
        auto *source_button = stroke ? btn_appearance_stroke_color_ : btn_appearance_fill_color_;
        popup.adjustSize();
        const QPoint cursor_pos = QCursor::pos();
        QPoint desired_pos(cursor_pos.x() + 14, cursor_pos.y() - popup.height() / 2);
        if (source_button && source_button->rect().contains(source_button->mapFromGlobal(cursor_pos))) {
            const QPoint button_right = source_button->mapToGlobal(QPoint(source_button->width() + 4, 0));
            desired_pos = QPoint(button_right.x(), button_right.y());
        }
        if (pending_color_popup_position_valid_) {
            desired_pos = pending_color_popup_position_;
            pending_color_popup_position_valid_ = false;
        }
        popup.move(clamp_popup_position_to_screen(desired_pos, popup.size(), source_button));
        QEventLoop popup_loop;
        connect(close_popup, &QToolButton::clicked, &popup, &QDialog::close);
        connect(&popup, &QDialog::finished, &popup_loop, &QEventLoop::quit);
        popup.show();
        popup.raise();
        popup.activateWindow();
        popup_loop.exec();
        emit gradient_editor_active_changed(false);
        if (live_visual_dirty)
            emit_change();
    };
    btn_appearance_fill_color_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn_appearance_fill_color_, &QWidget::customContextMenuRequested,
            this, [this, emit_change](const QPoint &pos) {
                if (!layer_ || loading_values_) return;
                QMenu menu(btn_appearance_fill_color_);
                QAction *expose = menu.addAction(QStringLiteral("Expose Fill to Dock"));
                expose->setCheckable(true);
                expose->setChecked(layer_->expose_fill_color && !layer_->use_as_scene_mask);
                expose->setEnabled(!layer_->use_as_scene_mask);
                QAction *single = menu.addAction(QStringLiteral("Single value"));
                single->setCheckable(true);
                single->setChecked(layer_->exposed_fill_single_value && !layer_->use_as_scene_mask);
                single->setEnabled(layer_->expose_fill_color && !layer_->use_as_scene_mask);
                QAction *chosen = menu.exec(btn_appearance_fill_color_->mapToGlobal(pos));
                if (!chosen) return;
                if (chosen == expose) layer_->expose_fill_color = !layer_->use_as_scene_mask && expose->isChecked();
                if (chosen == single) layer_->exposed_fill_single_value = !layer_->use_as_scene_mask && single->isChecked();
                load_values();
                emit_change();
            });
    connect(btn_appearance_fill_color_, &QPushButton::clicked,
            this, [open_color_selector]() { open_color_selector(false); });
    connect(btn_chart_gradient_, &QPushButton::clicked,
            this, [this, open_color_selector]() {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Chart)
                    return;
                layer_->fill_type = 1;
                open_color_selector(false);
                load_values();
            });
    btn_appearance_stroke_color_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn_appearance_stroke_color_, &QWidget::customContextMenuRequested,
            this, [this, emit_change](const QPoint &pos) {
                if (!layer_ || loading_values_) return;
                QMenu menu(btn_appearance_stroke_color_);
                QAction *expose = menu.addAction(QStringLiteral("Expose Stroke to Dock"));
                expose->setCheckable(true);
                expose->setChecked(layer_->expose_stroke_color);
                QAction *single = menu.addAction(QStringLiteral("Single value"));
                single->setCheckable(true);
                single->setChecked(layer_->exposed_stroke_single_value);
                single->setEnabled(layer_->expose_stroke_color);
                QAction *chosen = menu.exec(btn_appearance_stroke_color_->mapToGlobal(pos));
                if (!chosen) return;
                if (chosen == expose) layer_->expose_stroke_color = expose->isChecked();
                if (chosen == single) layer_->exposed_stroke_single_value = single->isChecked();
                load_values();
                emit_change();
            });
    connect(btn_appearance_stroke_color_, &QPushButton::clicked,
            this, [open_color_selector]() { open_color_selector(true); });
    connect(stroke_options_trigger, &QPushButton::clicked,
            this, [this, can_edit, emit_change, local_time, text_edit_active,
                   apply_text_char_format, current_text_char_format,
                   control_style, checkbox_style, themed_dialog_style]() {
                if (!can_edit()) return;
                const bool text_like = layer_->type == LayerType::Text ||
                                       layer_->type == LayerType::Clock ||
                                       layer_->type == LayerType::Ticker;
                const bool inline_stroke = text_like;
                const RichTextStroke displayed_stroke = inline_stroke
                    ? current_text_char_format().stroke
                    : layer_stroke_for_editor(*layer_);

                QDialog popup(this, Qt::Popup | Qt::FramelessWindowHint);
                popup.setModal(true);
                popup.setStyleSheet(themed_dialog_style);
                auto *root = new QVBoxLayout(&popup);
                root->setContentsMargins(8, 8, 8, 8);
                root->setSpacing(6);

                auto *weight_row = new QWidget(&popup);
                auto *weight_layout = new QHBoxLayout(weight_row);
                weight_layout->setContentsMargins(0, 0, 0, 0);
                weight_layout->setSpacing(6);
                auto *weight_label = new QLabel(fxm_tr("OBSTitles.WeightColon"), weight_row);
                auto *weight = new QDoubleSpinBox(weight_row);
                weight->setRange(0.0, 200.0);
                weight->setDecimals(0);
                weight->setSingleStep(1.0);
                weight->setSuffix(QStringLiteral(" px"));
                weight->setFixedWidth(82);
                weight->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
                weight->setStyleSheet(control_style);
                weight->setValue(displayed_stroke.width);
                weight_layout->addWidget(weight_label);
                weight_layout->addWidget(weight);
                weight_layout->addStretch();
                root->addWidget(weight_row);

                auto *offset_row = new QWidget(&popup);
                auto *offset_layout = new QHBoxLayout(offset_row);
                offset_layout->setContentsMargins(0, 0, 0, 0);
                offset_layout->setSpacing(6);
                auto *offset_label = new QLabel(fxm_tr("OBSTitles.StrokeOffsetColon"), offset_row);
                auto *offset = new QDoubleSpinBox(offset_row);
                offset->setRange(-16384.0, 16384.0);
                offset->setDecimals(2);
                offset->setSingleStep(0.1);
                offset->setSuffix(QStringLiteral(" px"));
                offset->setFixedWidth(96);
                offset->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
                offset->setStyleSheet(control_style);
                offset->setValue(layer_->stroke_offset_prop.is_animated()
                    ? layer_->stroke_offset_prop.evaluate(local_time())
                    : (double)layer_->stroke_offset);
                auto *offset_key = new QToolButton(offset_row);
                offset_key->setIcon(fxm_keyframe_diamond_icon(
                    keyframe_at_time(layer_->stroke_offset_prop, local_time()),
                    layer_->stroke_offset_prop.is_animated()));
                offset_key->setToolTip(QStringLiteral("Toggle Stroke Offset keyframe"));
                fxm_style_keyframe_button(offset_key);
                auto *offset_navigation = fxm_make_keyframe_controls(
                    offset_key, offset_row,
                    [this]() {
                        std::vector<double> times;
                        if (layer_)
                            for (const Keyframe &key : layer_->stroke_offset_prop.keyframes)
                                times.push_back(key.time);
                        return times;
                    }, local_time,
                    [this](double time) {
                        if (layer_)
                            emit keyframe_navigation_requested(layer_->in_time + time);
                    });
                offset_layout->addWidget(offset_label);
                offset_layout->addWidget(offset);
                offset_layout->addStretch();
                offset_layout->addWidget(offset_navigation);
                root->addWidget(offset_row);

                auto make_button = [](const QString &text, const QString &tip, QWidget *parent) {
                    auto *button = new QToolButton(parent);
                    button->setText(text);
                    button->setToolTip(tip);
                    button->setCheckable(true);
                    button->setFixedSize(28, 22);
                    return button;
                };
                auto add_button_group_row = [&](const QString &label_text, const QList<QToolButton *> &buttons,
                                                QWidget *extra = nullptr) {
                    auto *row = new QWidget(&popup);
                    auto *layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    layout->setSpacing(4);
                    auto *label = new QLabel(label_text, row);
                    label->setFixedWidth(46);
                    layout->addWidget(label);
                    for (auto *button : buttons)
                        layout->addWidget(button);
                    if (extra) {
                        layout->addSpacing(8);
                        layout->addWidget(extra);
                    }
                    layout->addStretch();
                    root->addWidget(row);
                };

                auto *cap_butt = make_button(fxm_tr("OBSTitles.Butt"), fxm_tr("OBSTitles.CapStyleUnsupported"), &popup);
                auto *cap_round = make_button(fxm_tr("OBSTitles.Rnd"), fxm_tr("OBSTitles.CapStyleUnsupported"), &popup);
                auto *cap_square = make_button(fxm_tr("OBSTitles.Sqr"), fxm_tr("OBSTitles.CapStyleUnsupported"), &popup);
                for (auto *button : {cap_butt, cap_round, cap_square})
                    button->setEnabled(false);
                add_button_group_row(fxm_tr("OBSTitles.CapColon"), {cap_butt, cap_round, cap_square});

                auto *corner_group = new QButtonGroup(&popup);
                corner_group->setExclusive(true);
                auto *corner_miter = make_button(fxm_tr("OBSTitles.M"), fxm_tr("OBSTitles.Miter"), &popup);
                auto *corner_round = make_button(fxm_tr("OBSTitles.R"), fxm_tr("OBSTitles.Round"), &popup);
                auto *corner_bevel = make_button(fxm_tr("OBSTitles.B"), fxm_tr("OBSTitles.Bevel"), &popup);
                corner_group->addButton(corner_miter, 0);
                corner_group->addButton(corner_round, 1);
                corner_group->addButton(corner_bevel, 2);
                if (auto *button = corner_group->button(std::clamp(displayed_stroke.join_style, 0, 2)))
                    button->setChecked(true);
                auto *limit = new QSpinBox(&popup);
                limit->setRange(1, 100);
                limit->setValue(10);
                limit->setFixedWidth(52);
                limit->setEnabled(false);
                limit->setStyleSheet(control_style);
                auto *limit_wrap = new QWidget(&popup);
                auto *limit_layout = new QHBoxLayout(limit_wrap);
                limit_layout->setContentsMargins(0, 0, 0, 0);
                limit_layout->setSpacing(4);
                limit_layout->addWidget(new QLabel(fxm_tr("OBSTitles.LimitColon"), limit_wrap));
                limit_layout->addWidget(limit);
                add_button_group_row(fxm_tr("OBSTitles.CornerColon"), {corner_miter, corner_round, corner_bevel}, limit_wrap);

                auto *order_group = new QButtonGroup(&popup);
                order_group->setExclusive(true);
                auto *order_back = make_button(fxm_tr("OBSTitles.Back"), fxm_tr("OBSTitles.StrokeBehindFillTooltip"), &popup);
                auto *order_front = make_button(fxm_tr("OBSTitles.Front"), fxm_tr("OBSTitles.StrokeInFrontFillTooltip"), &popup);
                order_group->addButton(order_back, 0);
                order_group->addButton(order_front, 1);
                if (auto *button = order_group->button(displayed_stroke.on_front ? 1 : 0))
                    button->setChecked(true);
                add_button_group_row(fxm_tr("OBSTitles.StrokeOrderColon"), {order_back, order_front});

                auto *alignment_group = new QButtonGroup(&popup);
                alignment_group->setExclusive(true);
                auto *alignment_outer = make_button(fxm_tr("OBSTitles.Outer"), fxm_tr("OBSTitles.OuterStrokeTooltip"), &popup);
                auto *alignment_mid = make_button(fxm_tr("OBSTitles.Mid"), fxm_tr("OBSTitles.MidStrokeTooltip"), &popup);
                auto *alignment_inner = make_button(fxm_tr("OBSTitles.Inner"), fxm_tr("OBSTitles.InnerStrokeTooltip"), &popup);
                for (auto *button : {alignment_outer, alignment_mid, alignment_inner})
                    button->setFixedWidth(42);
                alignment_group->addButton(alignment_outer, 0);
                alignment_group->addButton(alignment_mid, 1);
                alignment_group->addButton(alignment_inner, 2);
                if (auto *button = alignment_group->button(std::clamp(displayed_stroke.alignment, 0, 2)))
                    button->setChecked(true);
                add_button_group_row(fxm_tr("OBSTitles.StrokeAlignmentColon"),
                                     {alignment_outer, alignment_mid, alignment_inner});

                auto *dash_row = new QWidget(&popup);
                auto *dash_layout = new QHBoxLayout(dash_row);
                dash_layout->setContentsMargins(0, 0, 0, 0);
                dash_layout->setSpacing(4);
                auto *dashed = new FxmSwitch(fxm_tr("OBSTitles.DashedLine"), dash_row);
                dashed->setEnabled(false);
                dashed->setStyleSheet(checkbox_style);
                dashed->setToolTip(fxm_tr("OBSTitles.DashedStrokesUnsupported"));
                dash_layout->addWidget(dashed);
                dash_layout->addStretch();
                root->addWidget(dash_row);

                auto *dash_values = new QWidget(&popup);
                auto *dash_values_layout = new QHBoxLayout(dash_values);
                dash_values_layout->setContentsMargins(0, 0, 0, 0);
                dash_values_layout->setSpacing(4);
                for (const QString &label : {fxm_tr("OBSTitles.Dash"), fxm_tr("OBSTitles.Gap"),
                                             fxm_tr("OBSTitles.Dash"), fxm_tr("OBSTitles.Gap"),
                                             fxm_tr("OBSTitles.Dash"), fxm_tr("OBSTitles.Gap")}) {
                    auto *field = new QSpinBox(dash_values);
                    field->setRange(0, 999);
                    field->setValue(label == fxm_tr("OBSTitles.Dash") ? 12 : 0);
                    if (label == fxm_tr("OBSTitles.Dash"))
                        field->setSuffix(QStringLiteral(" pt"));
                    field->setFixedWidth(48);
                    field->setEnabled(false);
                    field->setStyleSheet(control_style);
                    dash_values_layout->addWidget(field);
                }
                root->addWidget(dash_values);

                connect(offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        &popup, [this, local_time, emit_change](double v) {
                            if (!layer_ || loading_values_) return;
                            layer_->stroke_offset = (float)v;
                            set_animated_value(layer_->stroke_offset_prop, local_time(), v);
                            emit_change();
                        });
                connect(offset_key, &QToolButton::clicked, &popup,
                        [this, offset, offset_key, local_time, emit_change]() {
                            if (!layer_ || loading_values_) return;
                            const double t = local_time();
                            toggle_keyframe(layer_->stroke_offset_prop, t, offset->value());
                            offset_key->setIcon(fxm_keyframe_diamond_icon(
                                keyframe_at_time(layer_->stroke_offset_prop, t),
                                layer_->stroke_offset_prop.is_animated()));
                            fxm_refresh_keyframe_navigation(offset_key);
                            emit_change();
                        });
                offset_key->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(offset_key, &QWidget::customContextMenuRequested, &popup,
                        [this, offset_key, emit_change](const QPoint &position) {
                    QMenu menu(offset_key);
                    QAction *clear = menu.addAction(
                        fxm_tr("OBSTitles.DeleteAllKeyframes"));
                    clear->setEnabled(layer_ &&
                                      !layer_->stroke_offset_prop.keyframes.empty());
                    if (menu.exec(offset_key->mapToGlobal(position)) != clear || !layer_)
                        return;
                    layer_->stroke_offset_prop.keyframes.clear();
                    fxm_refresh_keyframe_navigation(offset_key);
                    emit_change();
                });
                connect(weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        &popup, [this, inline_stroke, apply_text_char_format,
                                 current_text_char_format, local_time, emit_change](double v) {
                            if (!layer_ || loading_values_) return;
                            if (inline_stroke) {
                                RichTextCharFormat fmt = current_text_char_format();
                                fmt.stroke.width = (float)v;
                                fmt.stroke.enabled = v > 0.0;
                                if (v > 0.0 && fmt.stroke.fill.color == 0)
                                    fmt.stroke.fill.color = layer_->stroke_color;
                                apply_text_char_format(fmt, RichTextCharStroke);
                            } else {
                                layer_->stroke_width = (float)v;
                                set_animated_value(layer_->stroke_width_prop,
                                                   local_time(), v);
                                layer_->outline_enabled =
                                    v > 0.0 || layer_->stroke_width_prop.is_animated();
                                if (v > 0.0 && layer_->stroke_fill_type == 0)
                                    layer_->stroke_fill_type = 1;
                            }
                            if (spn_appearance_stroke_width_) {
                                QSignalBlocker block(spn_appearance_stroke_width_);
                                spn_appearance_stroke_width_->setValue(v);
                            }
                            if (spn_outline_width_) {
                                QSignalBlocker block(spn_outline_width_);
                                spn_outline_width_->setValue(v);
                            }
                            emit_change();
                        });
                connect(corner_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                        &popup, [this, corner_group, inline_stroke,
                                 apply_text_char_format, current_text_char_format,
                                 emit_change](QAbstractButton *button) {
                            if (!layer_ || loading_values_) return;
                            const int join_style = corner_group->id(button);
                            if (inline_stroke) {
                                RichTextCharFormat fmt = current_text_char_format();
                                fmt.stroke.join_style = join_style;
                                apply_text_char_format(fmt, RichTextCharStroke);
                            } else {
                                layer_->outline_join_style = join_style;
                            }
                            if (cmb_outline_join_) {
                                QSignalBlocker block(cmb_outline_join_);
                                int idx = cmb_outline_join_->findData(join_style);
                                cmb_outline_join_->setCurrentIndex(idx >= 0 ? idx : join_style);
                            }
                            emit_change();
                        });
                connect(order_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                        &popup, [this, order_group, inline_stroke,
                                 apply_text_char_format, current_text_char_format,
                                 emit_change](QAbstractButton *button) {
                            if (!layer_ || loading_values_) return;
                            const bool on_front = order_group->id(button) == 1;
                            if (inline_stroke) {
                                RichTextCharFormat fmt = current_text_char_format();
                                fmt.stroke.on_front = on_front;
                                apply_text_char_format(fmt, RichTextCharStroke);
                            } else {
                                layer_->outline_on_front = on_front;
                            }
                            if (cmb_outline_position_) {
                                QSignalBlocker block(cmb_outline_position_);
                                int idx = cmb_outline_position_->findData(on_front ? 1 : 0);
                                cmb_outline_position_->setCurrentIndex(idx >= 0 ? idx : (on_front ? 1 : 0));
                            }
                            emit_change();
                        });
                connect(alignment_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
                        &popup, [this, alignment_group, inline_stroke,
                                 apply_text_char_format, current_text_char_format,
                                 emit_change](QAbstractButton *button) {
                            if (!layer_ || loading_values_) return;
                            const int alignment = std::clamp(alignment_group->id(button), 0, 2);
                            if (inline_stroke) {
                                RichTextCharFormat fmt = current_text_char_format();
                                fmt.stroke.alignment = alignment;
                                apply_text_char_format(fmt, RichTextCharStroke);
                            } else {
                                layer_->outline_alignment = alignment;
                            }
                            emit_change();
                        });
