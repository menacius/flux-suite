        /* A non-modal tool window is kept alive by a local event loop. Unlike
         * Qt::Popup/QDialog::exec(), it does not grab the mouse, so gradient
         * handles can be dragged on the canvas while this panel remains open. */
        QDialog popup(this, Qt::Tool | Qt::FramelessWindowHint);
        popup.setModal(false);
        popup.setMinimumWidth(560);
        popup.setMinimumHeight(560);
        QString popup_css = QStringLiteral(
            "QDialog{background:@panel@;border:1px solid @border@;}"
            "QTabWidget::pane{border:1px solid @border@;background:@panel@;}"
            "QTabBar::tab{color:@text@;background:@button@;border:1px solid @border@;padding:4px 9px;}"
            "QTabBar::tab:selected{background:@highlight@;color:@highlightedText@;}"
            "QTabBar::tab:disabled{color:@subtle@;background:@button@;}"
            "QLabel{color:@text@;font-size:10px;background:transparent;}"
            "QPushButton{color:@buttonText@;background:@button@;border:1px solid @border@;border-radius:2px;padding:3px 8px;font-size:10px;}"
            "QPushButton:hover{background:@hover@;}"
            "QSlider::groove:horizontal{height:12px;border:1px solid @border@;border-radius:2px;background:@section@;}"
            "QSlider::handle:horizontal{width:10px;margin:-3px 0;border:1px solid @border@;border-radius:2px;background:@text@;}"
            "QLineEdit,QSpinBox{color:@controlText@;background:@controlBg@;border:1px solid @border@;border-radius:2px;padding:2px 4px;selection-background-color:@highlight@;}"
            "QLineEdit:focus,QSpinBox:focus{border-color:@highlight@;}");
        popup_css.replace(QStringLiteral("@panel@"), panel_bg_name);
        popup_css.replace(QStringLiteral("@border@"), border_name);
        popup_css.replace(QStringLiteral("@text@"), panel_text_name);
        popup_css.replace(QStringLiteral("@button@"), button_bg_name);
        popup_css.replace(QStringLiteral("@highlight@"), highlight_name);
        popup_css.replace(QStringLiteral("@highlightedText@"), highlighted_text_name);
        popup_css.replace(QStringLiteral("@subtle@"), subtle_text_name);
        popup_css.replace(QStringLiteral("@buttonText@"), button_text_name);
        popup_css.replace(QStringLiteral("@hover@"), hover_bg_name);
        popup_css.replace(QStringLiteral("@section@"), section_bg_name);
        popup_css.replace(QStringLiteral("@controlText@"), control_text_name);
        popup_css.replace(QStringLiteral("@controlBg@"), control_bg_name);
        popup.setStyleSheet(popup_css);
        auto *root = new QVBoxLayout(&popup);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(4);
        auto *popup_header = new QHBoxLayout;
        popup_header->setContentsMargins(0, 0, 0, 0);
        popup_header->addStretch();
        auto *close_popup = new QToolButton(&popup);
        close_popup->setText(QStringLiteral("×"));
        close_popup->setToolTip(fxm_tr("OBSTitles.Close"));
        close_popup->setAutoRaise(true);
        close_popup->setFixedSize(22, 22);
        popup_header->addWidget(close_popup);
        root->addLayout(popup_header);
        auto *tabs = new QTabWidget(&popup);
        // Keep the selector as the existing tabbed popup; do not open a separate Gradient Editor window.
        root->addWidget(tabs);
        bool live_visual_dirty = false;
        auto emit_live_visual_change = [&]() {
            if (loading_values_)
                return;
            live_visual_dirty = true;
            emit live_visual_changed();
        };

        auto update_main_swatch = [this, stroke, text_fill, text_edit_active, current_text_char_format]() {
            if (stroke) {
                const bool inline_stroke = layer_ &&
                    (layer_->type == LayerType::Text || layer_->type == LayerType::Clock ||
                     layer_->type == LayerType::Ticker);
                if (inline_stroke) {
                    const RichTextStroke &rich_stroke = current_text_char_format().stroke;
                    if (rich_stroke.fill.type == 1)
                        style_gradient_button(btn_appearance_stroke_color_,
                                              rich_stroke.fill.gradient_start_color,
                                              rich_stroke.fill.gradient_end_color,
                                              rich_stroke.fill.gradient_type);
                    else
                        style_color_button(btn_appearance_stroke_color_, rich_stroke.fill.color);
                    btn_appearance_stroke_color_->setText(QString());
                    if (btn_outline_color_)
                        style_color_button(btn_outline_color_, rich_stroke.fill.color);
                    return;
                }
                if (layer_->stroke_fill_type == 2)
                    style_gradient_button(btn_appearance_stroke_color_,
                                          layer_->stroke_gradient_start_color,
                                          layer_->stroke_gradient_end_color,
                                          layer_->stroke_gradient_type);
                else {
                    const double t = std::clamp(
                        playhead_ - layer_->in_time, 0.0,
                        std::max(0.0, layer_->out_time - layer_->in_time));
                    style_color_button(btn_appearance_stroke_color_,
                                       eval_stroke_color(*layer_, t));
                }
                btn_appearance_stroke_color_->setText(QString());
                if (btn_outline_color_) {
                    const double t = std::clamp(
                        playhead_ - layer_->in_time, 0.0,
                        std::max(0.0, layer_->out_time - layer_->in_time));
                    style_color_button(btn_outline_color_,
                                       eval_stroke_color(*layer_, t));
                }
            } else {
                if (text_fill) {
                    const RichTextCharFormat fmt = current_text_char_format();
                    if (fmt.fill.type == 1)
                        style_gradient_button(btn_appearance_fill_color_,
                                              fmt.fill.gradient_start_color,
                                              fmt.fill.gradient_end_color,
                                              fmt.fill.gradient_type);
                    else
                        style_color_button(btn_appearance_fill_color_, fmt.fill.color);
                    btn_appearance_fill_color_->setText(QString());
                    if (btn_text_color_) style_color_button(btn_text_color_, fmt.fill.color);
                    return;
                }
                const double t = std::clamp(playhead_ - layer_->in_time, 0.0,
                                            std::max(0.0, layer_->out_time - layer_->in_time));
                if (layer_->fill_type == 1)
                    style_gradient_button(btn_appearance_fill_color_,
                                          layer_->gradient_start_color,
                                          layer_->gradient_end_color,
                                          layer_->gradient_type);
                else
                    style_color_button(btn_appearance_fill_color_,
                                       text_fill ? eval_text_color(*layer_, t) : eval_fill_color(*layer_, t));
                btn_appearance_fill_color_->setText(QString());
                if (text_fill && btn_text_color_) style_color_button(btn_text_color_, eval_text_color(*layer_, t));
                if (!text_fill && btn_fill_color_) style_color_button(btn_fill_color_, eval_fill_color(*layer_, t));
            }
        };
        auto apply_solid_color = [this, stroke, text_fill, text_edit_active, local_time, &emit_live_visual_change,
                                  apply_text_fill_format, apply_text_char_format, current_text_char_format,
                                  update_main_swatch](const QColor &color) {
            if (!layer_ || loading_values_ || !color.isValid()) return;
            const uint32_t argb = argb_from_color(color);
            if (stroke) {
                const bool inline_stroke =
                    (layer_->type == LayerType::Text || layer_->type == LayerType::Clock ||
                     layer_->type == LayerType::Ticker);
                if (inline_stroke) {
                    RichTextCharFormat fmt = current_text_char_format();
                    fmt.stroke.enabled = true;
                    if (fmt.stroke.width <= 0.0f)
                        fmt.stroke.width = layer_->stroke_width > 0.0f
                            ? layer_->stroke_width : 1.0f;
                    fmt.stroke.fill.type = 0;
                    fmt.stroke.fill.color = argb;
                    if (!text_edit_active()) {
                        layer_->stroke_color = argb;
                        set_stroke_color_channels_at(*layer_, local_time(), argb);
                    }
                    apply_text_char_format(fmt, RichTextCharStroke);
                } else {
                    layer_->outline_enabled = true;
                    layer_->stroke_fill_type = 1;
                    layer_->stroke_color = argb;
                    set_stroke_color_channels_at(*layer_, local_time(), argb);
                }
            } else {
                if (text_fill) {
                    RichTextCharFormat fmt = current_text_char_format();
                    fmt.fill.type = 0;
                    fmt.fill.color = argb;
                    if (!text_edit_active())
                        set_color_channels_at(*layer_, true, local_time(), argb);
                    apply_text_char_format(fmt, RichTextCharFillColor);
                } else {
                    layer_->fill_type = 0;
                    layer_->fill_color = argb;
                    set_color_channels_at(*layer_, false, local_time(), argb);
                }
            }
            update_main_swatch();
            emit_live_visual_change();
        };

        auto *color_tab = new QWidget(tabs);
        color_tab->setMinimumSize(430, 430);
        auto *color_layout = new QHBoxLayout(color_tab);
        color_layout->setContentsMargins(8, 8, 8, 8);
        color_layout->setSpacing(8);

        QColor initial = stroke
            ? color_from_argb(inline_text_stroke
                ? initial_inline_stroke.fill.color
                : eval_stroke_color(*layer_, local_time()))
            : color_from_argb(inline_text_fill
                ? initial_inline_fill.color
                : (text_fill ? eval_text_color(*layer_, local_time()) : eval_fill_color(*layer_, local_time())));
        QColor selected_color = initial.isValid() ? initial : QColor(Qt::white);
        QColor background_color = QColor(Qt::white);
        const QColor original_color = selected_color;
        bool syncing_color_controls = false;
        bool closing_for_eyedropper = false;
        QStringList recent_color_hexes = fxm_load_recent_color_hexes();
        if (title_) {
            for (const auto &hex : title_->editor_recent_color_hexes) {
                const QString value = QString::fromStdString(hex);
                if (!recent_color_hexes.contains(value, Qt::CaseInsensitive))
                    recent_color_hexes << value;
            }
        } else {
            static QStringList fallback_recent_color_hexes;
            for (const QString &hex : fallback_recent_color_hexes) {
                if (!recent_color_hexes.contains(hex, Qt::CaseInsensitive))
                    recent_color_hexes << hex;
            }
        }

        auto color_hex = [](const QColor &color) {
            return color.alpha() < 255
                ? QStringLiteral("#%1%2%3%4")
                    .arg(color.red(), 2, 16, QLatin1Char('0'))
                    .arg(color.green(), 2, 16, QLatin1Char('0'))
                    .arg(color.blue(), 2, 16, QLatin1Char('0'))
                    .arg(color.alpha(), 2, 16, QLatin1Char('0'))
                    .toUpper()
                : QStringLiteral("#%1%2%3")
                    .arg(color.red(), 2, 16, QLatin1Char('0'))
                    .arg(color.green(), 2, 16, QLatin1Char('0'))
                    .arg(color.blue(), 2, 16, QLatin1Char('0'))
                    .toUpper();
        };
        auto parse_hex = [](QString text, QColor &color) {
            text = text.trimmed();
            if (text.startsWith(QStringLiteral("#"))) text.remove(0, 1);
            if (text.size() != 6 && text.size() != 8) return false;
            bool ok = false;
            const uint value = text.toUInt(&ok, 16);
            if (!ok) return false;
            color = text.size() == 6
                ? QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 255)
                : QColor((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
            return color.isValid();
        };
        auto swatch_style = [&](const QColor &color, int size = 32) {
            return QStringLiteral(
                "QPushButton{background:%1;border:1px solid %2;border-radius:3px;"
                "min-width:%3px;min-height:%3px;max-width:%3px;max-height:%3px;padding:0;}")
                .arg(color.name(QColor::HexArgb), border_name)
                .arg(size);
        };
        auto none_swatch_style = [&](int size = 36) {
            return QStringLiteral(
                "QPushButton{background:%1;border:1px solid %2;border-radius:3px;"
                "min-width:%3px;min-height:%3px;max-width:%3px;max-height:%3px;"
                "padding:0;color:%4;font-size:24px;font-weight:bold;}")
                .arg(control_bg_name, border_name)
                .arg(size)
                .arg(highlight_name);
        };

        auto *wheel_column = new QWidget(color_tab);
        auto *wheel_layout = new QVBoxLayout(wheel_column);
        wheel_layout->setContentsMargins(0, 0, 0, 0);
        wheel_layout->setSpacing(6);

        auto *picker_row = new QWidget(wheel_column);
        auto *picker_row_layout = new QHBoxLayout(picker_row);
        picker_row_layout->setContentsMargins(0, 0, 0, 0);
        picker_row_layout->setSpacing(8);

        auto *picker_actions = new QWidget(picker_row);
        auto *picker_actions_layout = new QVBoxLayout(picker_actions);
        picker_actions_layout->setContentsMargins(0, 0, 0, 0);
        picker_actions_layout->setSpacing(6);
        auto *add_to_library_button = new QPushButton(fxm_tr("OBSTitles.AddToLibrary"), picker_actions);
        add_to_library_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        picker_actions_layout->addWidget(add_to_library_button);
        picker_actions_layout->addStretch(1);

        auto *color_picker = new color_widgets::ColorDialog(picker_row, Qt::Widget);
        fxm_prepare_embedded_color_dialog(color_picker, selected_color);
        picker_row_layout->addWidget(picker_actions);
        picker_row_layout->addWidget(color_picker, 1);
        wheel_layout->addWidget(picker_row, 1);
        connect(color_picker, &QDialog::rejected, &popup, &QDialog::reject);

        auto *swatch_row = new QWidget(wheel_column);
        auto *swatch_row_layout = new QHBoxLayout(swatch_row);
        swatch_row_layout->setContentsMargins(0, 0, 0, 0);
        swatch_row_layout->setSpacing(6);
        auto *none_button = new QPushButton(QStringLiteral("╱"), swatch_row);
        none_button->setToolTip(fxm_tr("OBSTitles.NoColor"));
        none_button->setStyleSheet(none_swatch_style(30));
        auto *background_button = new QPushButton(swatch_row);
        background_button->setToolTip(fxm_tr("OBSTitles.BackgroundColor"));
        auto *foreground_button = new QPushButton(swatch_row);
        foreground_button->setToolTip(fxm_tr("OBSTitles.ForegroundColor"));
        auto *swap_button = new QPushButton(QStringLiteral("⇄"), swatch_row);
        swap_button->setToolTip(fxm_tr("OBSTitles.SwapForegroundBackground"));
        swap_button->setFixedSize(30, 30);
        auto *eyedropper_button = new QPushButton(QStringLiteral("⌕"), swatch_row);
        eyedropper_button->setIcon(obs_icon("eyedropper.svg"));
        eyedropper_button->setText(QString());
        eyedropper_button->setToolTip(fxm_tr("OBSTitles.PickColor"));
        eyedropper_button->setFixedSize(30, 30);
        swatch_row_layout->addWidget(none_button);
        swatch_row_layout->addWidget(background_button);
        swatch_row_layout->addWidget(foreground_button);
        swatch_row_layout->addWidget(swap_button);
        swatch_row_layout->addStretch(1);
        swatch_row_layout->addWidget(eyedropper_button);
        wheel_layout->addWidget(swatch_row);

        auto *recent_label = new QLabel(fxm_tr("OBSTitles.RecentColors"), wheel_column);
        wheel_layout->addWidget(recent_label);
        auto *recent_row = new QWidget(wheel_column);
        auto *recent_layout = new QHBoxLayout(recent_row);
        recent_layout->setContentsMargins(0, 0, 0, 0);
        recent_layout->setSpacing(8);
        std::vector<QPushButton *> recent_buttons;
        for (int i = 0; i < 16; ++i) {
            auto *b = new QPushButton(recent_row);
            b->setFixedSize(20, 20);
            recent_layout->addWidget(b);
            recent_buttons.push_back(b);
        }
        recent_layout->addStretch(1);
        wheel_layout->addWidget(recent_row);

        color_layout->addWidget(wheel_column, 1);

        auto update_recent_buttons = [&]() {
            for (int i = 0; i < (int)recent_buttons.size(); ++i) {
                auto *b = recent_buttons[i];
                b->setText(QString());
                if (i < recent_color_hexes.size()) {
                    QColor c;
                    parse_hex(recent_color_hexes[i], c);
                    b->setEnabled(true);
                    b->setStyleSheet(swatch_style(c, 22));
                    b->setToolTip(fxm_color_swatch_tooltip(fxm_tr("OBSTitles.RecentColors"), c, color_hex(c)));
                } else {
                    b->setEnabled(false);
                    b->setStyleSheet(QStringLiteral(
                        "QPushButton{background:%1;border:1px solid %2;border-radius:3px;"
                        "min-width:22px;min-height:22px;max-width:22px;max-height:22px;padding:0;}")
                        .arg(section_bg_name, border_name));
                    b->setToolTip(QString());
                }
            }
        };

        auto persist_recent_colors = [&]() {
            if (title_) {
                title_->editor_recent_color_hexes.clear();
                for (const QString &hex : recent_color_hexes)
                    title_->editor_recent_color_hexes.push_back(hex.toStdString());
            } else {
                static QStringList fallback_recent_color_hexes;
                fallback_recent_color_hexes = recent_color_hexes;
            }
            fxm_save_recent_color_hexes(recent_color_hexes);
            update_recent_buttons();
            emit recent_colors_changed();
        };

        auto remove_recent_color_at = [&](int index) {
            if (index < 0 || index >= recent_color_hexes.size())
                return;
            recent_color_hexes.removeAt(index);
            persist_recent_colors();
        };

        QString last_committed_recent_hex;
        auto commit_recent_color = [&](const QColor &color, bool force) {
            if (!color.isValid() || color.alpha() <= 0)
                return;

            const QString hex = color_hex(color).toUpper();
            if (!force && hex.compare(last_committed_recent_hex, Qt::CaseInsensitive) == 0)
                return;

            for (int i = recent_color_hexes.size() - 1; i >= 0; --i) {
                if (recent_color_hexes[i].compare(hex, Qt::CaseInsensitive) == 0)
                    recent_color_hexes.removeAt(i);
            }
            recent_color_hexes.prepend(hex);
            while (recent_color_hexes.size() > 16)
                recent_color_hexes.removeLast();

            last_committed_recent_hex = hex;
            persist_recent_colors();
        };

        QTimer recent_commit_timer(&popup);
        recent_commit_timer.setSingleShot(true);
        recent_commit_timer.setInterval(450);
        connect(&recent_commit_timer, &QTimer::timeout, &popup,
                [&]() { commit_recent_color(selected_color, false); });

        auto update_color_controls = [&](bool sync_picker = true, bool refresh_recent = true) {
            syncing_color_controls = true;
            foreground_button->setStyleSheet(swatch_style(selected_color, 30));
            background_button->setStyleSheet(swatch_style(background_color, 30));
            foreground_button->setToolTip(fxm_color_swatch_tooltip(fxm_tr("OBSTitles.ForegroundColor"), selected_color, color_hex(selected_color)));
            background_button->setToolTip(fxm_color_swatch_tooltip(fxm_tr("OBSTitles.BackgroundColor"), background_color, color_hex(background_color)));
            if (sync_picker) {
                QSignalBlocker blocker(color_picker);
                color_picker->setColor(selected_color);
            }
            if (refresh_recent)
                update_recent_buttons();
            syncing_color_controls = false;
        };

        auto apply_and_sync_color = [&](const QColor &color, bool update_picker) {
            if (!color.isValid()) return;
            selected_color = color;
            update_color_controls(update_picker, false);
            apply_solid_color(color);
            if (color.alpha() > 0)
                recent_commit_timer.start();
        };

        connect(color_picker, &color_widgets::ColorDialog::colorChanged, &popup,
                [=, &apply_and_sync_color, &syncing_color_controls](const QColor &color) {
                    if (syncing_color_controls) return;
                    apply_and_sync_color(color, false);
                });
        connect(add_to_library_button, &QPushButton::clicked, &popup, [this, &selected_color]() {
            if (selected_color.isValid() && selected_color.alpha() > 0)
                emit color_library_add_requested(selected_color);
        });
        connect(none_button, &QPushButton::clicked, &popup, [=, &selected_color, &update_color_controls, &update_main_swatch, &emit_live_visual_change, &apply_text_fill_format]() {
            if (!layer_) return;
            QColor none = selected_color;
            none.setAlpha(0);
            selected_color = none;
            if (stroke) {
                layer_->outline_enabled = false;
                layer_->stroke_fill_type = 0;
            } else {
                const uint32_t argb = argb_from_color(none);
                if (text_fill) {
                    if (text_edit_active()) {
                        RichTextCharFormat fmt = current_text_char_format();
                        fmt.fill.type = 0;
                        fmt.fill.color = argb;
                        apply_text_char_format(fmt, RichTextCharFillColor);
                    } else {
                        layer_->fill_type = 0;
                        layer_->text_color = argb;
                        set_color_channels_at(*layer_, true, local_time(), argb);
                        apply_text_fill_format();
                    }
                } else {
                    layer_->fill_type = 0;
                    layer_->fill_color = argb;
                    set_color_channels_at(*layer_, false, local_time(), argb);
                }
            }
            update_color_controls();
            update_main_swatch();
            emit_live_visual_change();
        });
        connect(swap_button, &QPushButton::clicked, &popup, [=, &selected_color, &background_color, &apply_and_sync_color]() {
            const QColor new_foreground = background_color;
            background_color = selected_color;
            apply_and_sync_color(new_foreground, true);
        });
        connect(eyedropper_button, &QPushButton::clicked, &popup, [this, &popup, &closing_for_eyedropper]() {
            // Reuse the editor/sidebar eyedropper instead of spawning a second color picker.
            // Store the current popup position so the Color tab can reappear in exactly
            // the same place after the canvas pick completes.
            closing_for_eyedropper = true;
            remember_next_color_popup_position(popup.pos());
            popup.accept();
            emit color_picker_tool_requested();
        });
        for (int i = 0; i < (int)recent_buttons.size(); ++i) {
            auto *button = recent_buttons[i];
            connect(button, &QPushButton::clicked, &popup, [=, &apply_and_sync_color, &parse_hex]() {
                QColor c;
                if (i < recent_color_hexes.size() && parse_hex(recent_color_hexes[i], c)) {
                    apply_and_sync_color(c, true);
                    commit_recent_color(c, true);
                }
            });
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QPushButton::customContextMenuRequested, &popup, [=, &parse_hex](const QPoint &pos) {
                QColor c;
                if (i >= recent_color_hexes.size() || !parse_hex(recent_color_hexes[i], c))
                    return;
                QMenu menu(button);
                QAction *add_action = menu.addAction(fxm_tr("OBSTitles.AddToLibrary"));
                QAction *delete_action = menu.addAction(fxm_tr("OBSTitles.Delete"));
                QAction *selected = menu.exec(button->mapToGlobal(pos));
                if (selected == add_action)
                    emit color_library_add_requested(c);
                else if (selected == delete_action)
                    remove_recent_color_at(i);
            });
        }
        connect(&popup, &QDialog::finished, &popup, [=, &recent_commit_timer, &commit_recent_color, &selected_color, &closing_for_eyedropper]() mutable {
            recent_commit_timer.stop();
            if (!closing_for_eyedropper)
                commit_recent_color(selected_color, true);
        });

        update_color_controls();
        tabs->addTab(color_tab, fxm_tr("OBSTitles.Color"));

        auto *swatches_tab = new QWidget(tabs);
        auto *swatches_layout = new QVBoxLayout(swatches_tab);
        swatches_layout->setContentsMargins(8, 8, 8, 8);
        swatches_layout->setSpacing(6);
        auto *swatches_combo = new QComboBox(swatches_tab);
        auto *swatches_scroll = new QScrollArea(swatches_tab);
        swatches_scroll->setWidgetResizable(true);
        swatches_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *swatches_grid_widget = new ResponsiveSwatchGrid(swatches_scroll);
        swatches_scroll->setWidget(swatches_grid_widget);
        swatches_layout->addWidget(swatches_combo);
        swatches_layout->addWidget(swatches_scroll, 1);
        auto swatch_libraries = std::make_shared<std::vector<PropertiesColorLibrary>>(properties_load_color_libraries());
        for (const auto &library : *swatch_libraries)
            swatches_combo->addItem(library.name);
        auto refresh_swatches_tab = [swatches_grid_widget, swatches_combo, swatch_libraries,
                                     color_hex, swatch_style, &apply_and_sync_color, &popup]() {
            swatches_grid_widget->clearSwatches();
            const int library_index = swatches_combo->currentIndex();
            if (library_index < 0 || library_index >= (int)swatch_libraries->size())
                return;
            const auto &colors = (*swatch_libraries)[library_index].colors;
            for (int i = 0; i < (int)colors.size(); ++i) {
                const auto entry = colors[i];
                auto *button = new QPushButton(swatches_grid_widget);
                button->setFixedSize(24, 24);
                button->setText(QString());
                button->setToolTip(fxm_color_swatch_tooltip(entry.name, entry.color, color_hex(entry.color)));
                button->setStyleSheet(swatch_style(entry.color, 24));
                connect(button, &QPushButton::clicked, &popup, [=, &apply_and_sync_color]() {
                    apply_and_sync_color(entry.color, true);
                });
                swatches_grid_widget->addSwatch(button);
            }
        };
        connect(swatches_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &popup,
                [&refresh_swatches_tab](int) { refresh_swatches_tab(); });
        refresh_swatches_tab();
        tabs->addTab(swatches_tab, fxm_tr("OBSTitles.Swatches"));

        auto *gradient_tab = new QWidget(tabs);
        auto *gradient_layout = new QVBoxLayout(gradient_tab);
        gradient_tab->setMinimumSize(430, 430);
        gradient_layout->setContentsMargins(8, 8, 8, 8);
        gradient_layout->setSpacing(6);

        auto *preset_box = new QGroupBox(fxm_tr("OBSTitles.Presets"), gradient_tab);
        preset_box->setStyleSheet(QStringLiteral("QGroupBox{color:%1;background:%2;border:1px solid %3;"
                                "border-radius:2px;margin-top:15px;padding-top:8px;font-size:10px;}"
                                "QGroupBox::title{subcontrol-origin:margin;left:6px;padding:0 3px;background:%2;}")
                                .arg(panel_text_name, section_bg_name, border_name));
        auto *preset_layout = new QVBoxLayout(preset_box);
        preset_layout->setContentsMargins(6, 6, 6, 6);
        preset_layout->setSpacing(4);
        auto *preset_folders = new QWidget(preset_box);
        auto *preset_folders_layout = new QGridLayout(preset_folders);
        preset_folders_layout->setContentsMargins(0, 0, 0, 0);
        preset_folders_layout->setHorizontalSpacing(10);
        preset_folders_layout->setVerticalSpacing(4);
        const QStringList preset_groups = {fxm_tr("OBSTitles.Cloud"), fxm_tr("OBSTitles.Iridescent"),
                                           fxm_tr("OBSTitles.Pastels"), fxm_tr("OBSTitles.Neutrals")};
        for (int i = 0; i < preset_groups.size(); ++i) {
            auto *label = new QLabel(QStringLiteral("▸  📁  %1").arg(preset_groups[i]), preset_folders);
            label->setStyleSheet(QStringLiteral("QLabel{color:%1;background:transparent;font-size:10px;}").arg(panel_text_name));
            preset_folders_layout->addWidget(label, i / 2, i % 2);
        }
        // Keep presets compact: only the quick preset strip is shown.
        preset_folders->hide();
        auto *preset_strip = new ResponsiveSwatchGrid(preset_box, 28, 4);
        preset_layout->addWidget(preset_strip);
        auto *preset_actions = new QWidget(preset_box);
        auto *preset_actions_layout = new QHBoxLayout(preset_actions);
        preset_actions_layout->setContentsMargins(0, 0, 0, 0);
        preset_actions_layout->setSpacing(4);
        auto *new_preset = new QPushButton(QStringLiteral("+"), preset_actions);
        new_preset->setFixedSize(28, 24);
        new_preset->setToolTip(fxm_tr("OBSTitles.SaveGradientPreset"));
        preset_actions_layout->addWidget(new_preset);
        preset_actions_layout->addStretch(1);
        preset_layout->addWidget(preset_actions);

        auto *gradient_form = new QWidget(gradient_tab);
        auto *gradient_form_layout = new QGridLayout(gradient_form);
        gradient_form_layout->setContentsMargins(0, 0, 0, 0);
        gradient_form_layout->setHorizontalSpacing(6);
        gradient_form_layout->setVerticalSpacing(4);
        auto *name_edit = new QLineEdit(fxm_tr("OBSTitles.Custom"), gradient_form);
        name_edit->setStyleSheet(control_style);
        auto *type = new QComboBox(gradient_form);
        type->addItem(fxm_tr("OBSTitles.Linear"), 0);
        type->addItem(fxm_tr("OBSTitles.RadialGradient"), 1);
        type->addItem(fxm_tr("OBSTitles.ConicalGradient"), 2);
        type->setStyleSheet(control_style);
        auto *repeat_mode = new QComboBox(gradient_form);
        repeat_mode->addItem(fxm_tr("OBSTitles.No"), 0);
        repeat_mode->addItem(fxm_tr("OBSTitles.Repeat"), 2);
        repeat_mode->addItem(fxm_tr("OBSTitles.Reflect"), 1);
        repeat_mode->setStyleSheet(control_style);
        auto *smoothness = new QSpinBox(gradient_form);
        smoothness->setRange(0, 100);
        smoothness->setValue(100);
        smoothness->setSuffix(QStringLiteral("%"));
        smoothness->setStyleSheet(control_style);
        // Compact square-layout controls: keep only the essentials visible.
        name_edit->hide();
        gradient_form_layout->addWidget(new QLabel(fxm_tr("OBSTitles.Type"), gradient_form), 0, 0);
        gradient_form_layout->addWidget(type, 0, 1);
        gradient_form_layout->addWidget(new QLabel(fxm_tr("OBSTitles.Smooth"), gradient_form), 0, 2);
        gradient_form_layout->addWidget(smoothness, 0, 3);
        gradient_form_layout->addWidget(new QLabel(fxm_tr("OBSTitles.SpreadLabel"), gradient_form), 1, 0);
        gradient_form_layout->addWidget(repeat_mode, 1, 1);
        gradient_form_layout->setColumnStretch(1, 1);

        auto *preview = new GradientEditorPreview(gradient_tab);
        preview->setMinimumHeight(88);
        preview->setMaximumHeight(100);

        auto *type_group = new QButtonGroup(gradient_tab);
        type_group->setExclusive(true);
        // Kept for existing signal wiring; gradient type is now chosen from the Photoshop-style Type combo.

        auto *start_color = new QPushButton(gradient_tab);
        auto *end_color = new QPushButton(gradient_tab);
        auto make_spin = [&](double lo, double hi, double step, int decimals = 2) {
            auto *spin = new QDoubleSpinBox(gradient_tab);
            spin->setRange(lo, hi);
            spin->setSingleStep(step);
            spin->setDecimals(decimals);
            spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
            spin->setStyleSheet(control_style);
            return spin;
        };
        auto *start_pos = make_spin(0.0, 1.0, 0.01);
        auto *end_pos = make_spin(0.0, 1.0, 0.01);
        auto *start_opacity = make_spin(0.0, 1.0, 0.01);
        auto *end_opacity = make_spin(0.0, 1.0, 0.01);
        auto *gradient_opacity = make_spin(0.0, 1.0, 0.01);
        auto *angle = make_spin(-360.0, 360.0, 1.0, 0);
        angle->setSuffix(QStringLiteral("°"));
        auto *center_x = make_spin(-100.0, 100.0, 0.01);
        auto *center_y = make_spin(-100.0, 100.0, 0.01);
        auto *scale = make_spin(0.01, 100.0, 0.05);
        auto *focal_x = make_spin(-100.0, 100.0, 0.01);
        auto *focal_y = make_spin(-100.0, 100.0, 0.01);
        auto *reverse_gradient = new FxmSwitch(fxm_tr("OBSTitles.ReverseGradient"), gradient_tab);
        auto *dither_gradient = new FxmSwitch(fxm_tr("OBSTitles.Dither"), gradient_tab);
        for (auto *w : {gradient_opacity, angle, center_x, center_y, scale, focal_x, focal_y})
            w->hide();
        reverse_gradient->hide();
        dither_gradient->hide();

        if (stroke) {
            RichTextFill fill;
            if (inline_text_stroke) {
                fill = initial_inline_stroke.fill;
            } else {
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
            }
            type->setCurrentIndex(std::max(0, type->findData(fill.gradient_type)));
            repeat_mode->setCurrentIndex(std::max(0, repeat_mode->findData(fill.gradient_spread)));
            style_color_button(start_color, fill.gradient_start_color);
            style_color_button(end_color, fill.gradient_end_color);
            start_pos->setValue(fill.gradient_start_pos);
            end_pos->setValue(fill.gradient_end_pos);
            start_opacity->setValue(fill.gradient_start_opacity);
            end_opacity->setValue(fill.gradient_end_opacity);
            gradient_opacity->setValue(fill.gradient_opacity);
            angle->setValue(fill.gradient_angle);
            center_x->setValue(fill.gradient_center_x);
            center_y->setValue(fill.gradient_center_y);
            scale->setValue(fill.gradient_scale);
            focal_x->setValue(fill.gradient_focal_x);
            focal_y->setValue(fill.gradient_focal_y);
        } else {
            const RichTextFill fill = inline_text_fill ? initial_inline_fill
                                                       : RichTextFill();
            const int gradient_type = inline_text_fill ? fill.gradient_type : layer_->gradient_type;
            const int gradient_spread = inline_text_fill ? fill.gradient_spread : layer_->gradient_spread;
            const uint32_t start_argb = inline_text_fill ? fill.gradient_start_color : layer_->gradient_start_color;
            const uint32_t end_argb = inline_text_fill ? fill.gradient_end_color : layer_->gradient_end_color;
            type->setCurrentIndex(std::max(0, type->findData(gradient_type)));
            repeat_mode->setCurrentIndex(std::max(0, repeat_mode->findData(gradient_spread)));
            style_color_button(start_color, start_argb);
            style_color_button(end_color, end_argb);
            start_pos->setValue(inline_text_fill ? fill.gradient_start_pos : layer_->gradient_start_pos);
            end_pos->setValue(inline_text_fill ? fill.gradient_end_pos : layer_->gradient_end_pos);
            start_opacity->setValue(inline_text_fill ? fill.gradient_start_opacity : layer_->gradient_start_opacity);
            end_opacity->setValue(inline_text_fill ? fill.gradient_end_opacity : layer_->gradient_end_opacity);
            gradient_opacity->setValue(inline_text_fill ? fill.gradient_opacity : layer_->gradient_opacity);
            angle->setValue(inline_text_fill ? fill.gradient_angle : layer_->gradient_angle);
            center_x->setValue(inline_text_fill ? fill.gradient_center_x : layer_->gradient_center_x);
            center_y->setValue(inline_text_fill ? fill.gradient_center_y : layer_->gradient_center_y);
            scale->setValue(inline_text_fill ? fill.gradient_scale : layer_->gradient_scale);
            focal_x->setValue(inline_text_fill ? fill.gradient_focal_x : layer_->gradient_focal_x);
            focal_y->setValue(inline_text_fill ? fill.gradient_focal_y : layer_->gradient_focal_y);
        }
        start_color->setText(QString());
        end_color->setText(QString());

        auto *stops_box = new QGroupBox(fxm_tr("OBSTitles.Stops"), gradient_tab);
        stops_box->setStyleSheet(preset_box->styleSheet());
        auto *stops_layout = new QVBoxLayout(stops_box);
        stops_layout->setContentsMargins(5, 5, 5, 5);
        stops_layout->setSpacing(3);
        auto make_stop_row = [&](int stop_index, QPushButton *color_button, QDoubleSpinBox *pos_spin,
                                 QDoubleSpinBox *opacity_spin) {
            auto *row = new QWidget(stops_box);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(6);
            color_button->setFixedSize(28, 18);
            layout->addWidget(new QLabel(stop_index < 2 ? (stop_index == 0 ? fxm_tr("OBSTitles.Start") : fxm_tr("OBSTitles.End"))
                                                        : fxm_tr("OBSTitles.Stop"), row));
            layout->addWidget(color_button);
            pos_spin->setSuffix(QStringLiteral("%"));
            pos_spin->setRange(0.0, 100.0);
            pos_spin->setDecimals(0);
            pos_spin->setFixedWidth(58);
            opacity_spin->setSuffix(QStringLiteral("%"));
            opacity_spin->setRange(0.0, 100.0);
            opacity_spin->setDecimals(0);
            opacity_spin->setFixedWidth(58);
            auto *delete_button = new QPushButton(QStringLiteral("×"), row);
            delete_button->setFixedSize(22, 22);
            delete_button->setEnabled(false);
            delete_button->setToolTip(fxm_tr("OBSTitles.StopDeleteLockedTooltip"));
            layout->addWidget(new QLabel(fxm_tr("OBSTitles.Location"), row));
            layout->addWidget(pos_spin);
            layout->addWidget(new QLabel(fxm_tr("OBSTitles.Opacity"), row));
            layout->addWidget(opacity_spin);
            layout->addWidget(delete_button);
            row->setProperty("stop_index", stop_index);
            stops_layout->addWidget(row);
            return row;
        };
        auto *stop0_row = make_stop_row(0, start_color, start_pos, start_opacity);
        auto *stop1_row = make_stop_row(1, end_color, end_pos, end_opacity);
        // Start/end controls remain as hidden backing controls for existing layer serialization.
        stop0_row->hide();
        stop1_row->hide();
        start_pos->setValue(start_pos->value() * 100.0);
        end_pos->setValue(end_pos->value() * 100.0);
        start_opacity->setValue(start_opacity->value() * 100.0);
        end_opacity->setValue(end_opacity->value() * 100.0);
