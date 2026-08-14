    act_canvas_border_visible_ = view_menu->addAction(fxm_tr("OBSTitles.CanvasBorder"));
    act_canvas_border_visible_->setCheckable(true);
    act_canvas_border_visible_->setChecked(true);
    act_canvas_border_visible_->setToolTip(fxm_tr("OBSTitles.CanvasBorderTooltip"));
    connect(act_canvas_border_visible_, &QAction::toggled, this, [this](bool visible) {
        if (canvas_) canvas_->set_canvas_border_visible(visible);
    });

    act_clear_guides_ = view_menu->addAction(fxm_tr("OBSTitles.ClearGuides"));
    act_clear_guides_->setToolTip(fxm_tr("OBSTitles.ClearGuidesTooltip"));
    connect(act_clear_guides_, &QAction::triggered, this, [this]() {
        if (canvas_) canvas_->clear_user_guides();
    });
    view_menu->addSeparator();

    act_snap_enabled_ = view_menu->addAction(fxm_tr("OBSTitles.Snap"));
    act_snap_enabled_->setCheckable(true);
    act_snap_enabled_->setChecked(true);
    register_editor_shortcut(act_snap_enabled_,
                             {QKeySequence(Qt::CTRL | Qt::Key_Semicolon)});
    act_snap_enabled_->setToolTip(fxm_tr("OBSTitles.SnapTooltip"));
    connect(act_snap_enabled_, &QAction::toggled, this, [this](bool enabled) {
        if (canvas_) canvas_->set_snap_enabled(enabled);
    });

    auto *snap_to_menu = view_menu->addMenu(fxm_tr("OBSTitles.SnapTo"));
    auto add_snap_to_action = [this, snap_to_menu](const QString &text, bool checked, auto setter) {
        QAction *action = snap_to_menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        connect(action, &QAction::toggled, this, [this, setter](bool enabled) {
            if (canvas_) (canvas_->*setter)(enabled);
        });
        return action;
    };
    add_snap_to_action(fxm_tr("OBSTitles.Guides"), true, &CanvasPreview::set_snap_to_guides);
    add_snap_to_action(fxm_tr("OBSTitles.Grid"), false, &CanvasPreview::set_snap_to_grid);
    add_snap_to_action(fxm_tr("OBSTitles.ObjectEdges"), true, &CanvasPreview::set_snap_to_object_edges);
    add_snap_to_action(fxm_tr("OBSTitles.ObjectCenters"), true, &CanvasPreview::set_snap_to_object_centers);
    add_snap_to_action(fxm_tr("OBSTitles.CanvasBounds"), true, &CanvasPreview::set_snap_to_canvas_bounds);
    add_snap_to_action(fxm_tr("OBSTitles.SpacingAlignment"), true, &CanvasPreview::set_snap_to_spacing);

    create_docked_panel_menu(menu_bar);

    auto *help_menu = menu_bar->addMenu(fxm_tr("OBSTitles.HelpMenu"));
    QAction *about_action = help_menu->addAction(obs_icon("about.svg"), fxm_tr("OBSTitles.About"));
    connect(about_action, &QAction::triggered, this, &TitleEditor::show_about);
    setMenuBar(menu_bar);

    editor_status_bar_ = new QStatusBar(this);
    editor_status_bar_->setObjectName(QStringLiteral("FluxMotionEditorStatusBar"));
    editor_status_bar_->setSizeGripEnabled(true);
    status_diagnostics_label_ = new QLabel(editor_status_bar_);
    status_diagnostics_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    status_diagnostics_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    editor_status_bar_->addWidget(status_diagnostics_label_, 1);
    auto *diagnostics_timer = new QTimer(editor_status_bar_);
    diagnostics_timer->setInterval(1000);
    diagnostics_timer->setTimerType(Qt::CoarseTimer);
    connect(diagnostics_timer, &QTimer::timeout, this, [this]() {
        if (!playing_)
            update_footer_diagnostics();
    });
    diagnostics_timer->start();
    status_activity_label_ = new QLabel(editor_status_bar_);
    status_activity_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_activity_label_->setMinimumWidth(210);
    status_activity_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    editor_status_bar_->addPermanentWidget(status_activity_label_);
    setStatusBar(editor_status_bar_);

    /* ── Toolbar ── */
    build_toolbar();
    root->addWidget(toolbar_);

    /* ── Title name label bar ── */
    const QPalette editor_pal = qApp->palette();
    const QColor editor_window = editor_pal.color(QPalette::Window);
    const QColor editor_base = editor_pal.color(QPalette::Base);
    const QColor editor_text = editor_pal.color(QPalette::WindowText);
    const QColor editor_field_text = editor_pal.color(QPalette::Text);
    const QColor editor_button = editor_pal.color(QPalette::Button);
    const QColor editor_button_text = editor_pal.color(QPalette::ButtonText);
    const QColor editor_border = editor_pal.color(QPalette::Mid);
    const QColor editor_highlight = editor_pal.color(QPalette::Highlight);
    const QColor editor_hover = editor_button.lightness() < 128 ? editor_button.lighter(125) : editor_button.darker(108);

    auto *title_bar = new QWidget(this);
    title_bar->setStyleSheet(QStringLiteral("background:%1;color:%2;")
                                 .arg(editor_window.name(QColor::HexRgb),
                                      editor_text.name(QColor::HexRgb)));
    auto *title_bar_layout = new QHBoxLayout(title_bar);
    title_bar_layout->setContentsMargins(0, 3, 0, 3);
    title_bar_layout->setSpacing(6);
    title_bar_layout->addStretch(1);
    dirty_indicator_ = new QLabel(title_bar);
    dirty_indicator_->setFixedSize(10, 10);
    dirty_indicator_->setStyleSheet(QStringLiteral("background:%1;border-radius:5px;").arg(QColor(0xE3, 0x33, 0x33).name(QColor::HexRgb)));
    dirty_indicator_->setToolTip(fxm_tr("OBSTitles.UnsavedChangesIndicator"));
    dirty_indicator_->hide();
    title_bar_layout->addWidget(dirty_indicator_, 0, Qt::AlignVCenter);
    title_lbl_ = new QLabel("—", title_bar);
    title_lbl_->setAlignment(Qt::AlignCenter);
    QFont tf = title_lbl_->font();
    tf.setPointSize(tf.pointSize() + 1);
    tf.setBold(true);
    title_lbl_->setFont(tf);
    title_lbl_->setStyleSheet(QStringLiteral("color:%1;").arg(editor_text.name(QColor::HexRgb)));
    title_lbl_->setToolTip(fxm_tr("OBSTitles.DoubleClickRename"));
    title_bar_layout->addWidget(title_lbl_, 0, Qt::AlignVCenter);
    title_name_edit_ = new QLineEdit(title_bar);
    title_name_edit_->setAlignment(Qt::AlignCenter);
    title_name_edit_->setFont(tf);
    title_name_edit_->setMinimumWidth(180);
    title_name_edit_->setStyleSheet(QStringLiteral(
        "QLineEdit{color:%1;background:%2;border:1px solid %3;border-radius:3px;padding:1px 6px;}")
        .arg(editor_field_text.name(QColor::HexRgb),
             editor_base.name(QColor::HexRgb),
             editor_highlight.name(QColor::HexRgb)));
    title_name_edit_->hide();
    connect(title_name_edit_, &QLineEdit::returnPressed, this, [this]() {
        commit_title_name_edit(true);
    });
    title_bar_layout->addWidget(title_name_edit_, 0, Qt::AlignVCenter);
    title_bar_layout->addStretch(1);
    root->addWidget(title_bar);

    /* ── Upper split: Canvas (dockable property panels live in QMainWindow dock areas) ── */
    auto *upper_split = new QSplitter(Qt::Horizontal, central);

    title_props_ = new TitlePropertiesPanel(this);
    title_props_->set_document_format_editable(
        execution_context_.host != fxm::editor::EditorHostKind::ObsPlugin);
    title_props_->setMinimumWidth(240);

    auto *canvas_panel = new QWidget(upper_split);
    canvas_panel->setStyleSheet(QStringLiteral("background:%1;color:%2;")
                                    .arg(editor_window.name(QColor::HexRgb),
                                         editor_text.name(QColor::HexRgb)));
    auto *canvas_layout = new QVBoxLayout(canvas_panel);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    canvas_layout->setSpacing(0);
    canvas_ = new CanvasPreview(canvas_panel);
    canvas_->setMinimumSize(300, 200);
    if (effects_panel_) {
        connect(effects_panel_, &EffectsPanel::extension_canvas_handles_changed,
                canvas_, &CanvasPreview::set_extension_canvas_handles,
                Qt::UniqueConnection);
        connect(canvas_, &CanvasPreview::extension_canvas_handle_moved,
                effects_panel_, &EffectsPanel::set_extension_canvas_handle_position,
                Qt::UniqueConnection);
        canvas_->set_extension_canvas_handles(effects_panel_->extension_canvas_handles());
    }
    if (act_rulers_visible_) act_rulers_visible_->setChecked(canvas_->rulers_visible());
    if (act_guides_visible_) act_guides_visible_->setChecked(canvas_->guides_visible());
    if (act_guides_locked_) act_guides_locked_->setChecked(canvas_->guides_locked());
    if (act_guide_coordinates_) act_guide_coordinates_->setChecked(canvas_->show_guide_coordinates());
    if (act_canvas_border_visible_) act_canvas_border_visible_->setChecked(canvas_->canvas_border_visible());
    canvas_layout->addWidget(canvas_, 1);

    auto *canvas_zoom_bar = new QWidget(canvas_panel);
    canvas_zoom_bar->setFixedHeight(34);
    canvas_zoom_bar->setStyleSheet(QStringLiteral(
        "QWidget{background:%1;border-top:1px solid %2;color:%3;}"
        "QPushButton,QToolButton{color:%4;background:%5;border:1px solid %2;border-radius:3px;padding:3px 8px;}"
        "QPushButton:hover,QToolButton:hover{background:%6;}"
        "QToolButton::menu-indicator{image:none;}"
        "QSpinBox{color:%7;background:%8;border:1px solid %2;border-radius:3px;padding:2px 6px;}"
        "QSpinBox::up-button,QSpinBox::down-button{width:0;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:%8;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;background:%4;border-radius:6px;}"
        "QSlider::sub-page:horizontal{background:%9;border-radius:2px;}")
        .arg(editor_window.name(QColor::HexRgb),
             editor_border.name(QColor::HexRgb),
             editor_text.name(QColor::HexRgb),
             editor_button_text.name(QColor::HexRgb),
             editor_button.name(QColor::HexRgb),
             editor_hover.name(QColor::HexRgb),
             editor_field_text.name(QColor::HexRgb),
             editor_base.name(QColor::HexRgb),
             editor_highlight.name(QColor::HexRgb)));
    auto *canvas_zoom_layout = new QHBoxLayout(canvas_zoom_bar);
    canvas_zoom_layout->setContentsMargins(10, 0, 10, 0);
    canvas_zoom_layout->setSpacing(8);
    auto *canvas_zoom_out = new QPushButton(canvas_zoom_bar);
    canvas_zoom_out->setIcon(obs_icon("zoom-out.svg"));
    canvas_zoom_out->setFixedWidth(30);
    auto *canvas_zoom_slider = new QSlider(Qt::Horizontal, canvas_zoom_bar);
    canvas_zoom_slider->setRange(5, 1600);
    canvas_zoom_slider->setValue(canvas_->zoom_percent());
    canvas_zoom_slider->setMinimumWidth(140);
    canvas_zoom_slider->setMaximumWidth(300);
    auto *canvas_zoom_in = new QPushButton(canvas_zoom_bar);
    canvas_zoom_in->setIcon(obs_icon("zoom-in.svg"));
    canvas_zoom_in->setFixedWidth(30);
    auto *canvas_zoom_percent = new QSpinBox(canvas_zoom_bar);
    canvas_zoom_percent->setRange(5, 1600);
    canvas_zoom_percent->setSuffix("%");
    canvas_zoom_percent->setAlignment(Qt::AlignCenter);
    canvas_zoom_percent->setButtonSymbols(QAbstractSpinBox::NoButtons);
    canvas_zoom_percent->setFixedWidth(72);
    canvas_zoom_percent->setValue(canvas_->zoom_percent());
    auto *fit_canvas = new LongPressToolButton(canvas_zoom_bar);
    fit_canvas->setText("Fit");
    fit_canvas->setCheckable(true);
    fit_canvas->setChecked(canvas_->fit_zoom_active());
    fit_canvas->setPopupMode(QToolButton::DelayedPopup);
    fit_canvas->setStyleSheet(QStringLiteral(
        "QToolButton:checked {"
        " background-color: palette(highlight); color: palette(highlighted-text);"
        " border: 1px solid palette(highlight); border-radius: 3px; }"
        "QToolButton::menu-indicator{image:none;width:0px;}"));
    auto *fit_canvas_menu = new QMenu(fit_canvas);
    auto add_canvas_zoom_action = [fit_canvas_menu](const QString &text, int percent) {
        QAction *action = fit_canvas_menu->addAction(text);
        action->setData(percent);
        return action;
    };
    QAction *fit_action = fit_canvas_menu->addAction("Fit");
    fit_action->setData(-1);
    QAction *fit_100_action = fit_canvas_menu->addAction("Fit up to 100%");
    fit_100_action->setData(-2);
    add_canvas_zoom_action("50%", 50);
    add_canvas_zoom_action("100%", 100);
    add_canvas_zoom_action("200%", 200);
    add_canvas_zoom_action("400%", 400);
    add_canvas_zoom_action("800%", 800);
    add_canvas_zoom_action("1600%", 1600);
    fit_canvas->setMenu(fit_canvas_menu);
    QSettings canvas_settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    canvas_settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    const int saved_checkerboard_pattern = std::clamp(
        canvas_settings.value(QString::fromUtf8(kEditorCanvasTransparencyKey), 1).toInt(), 0, 5);
    const bool saved_safe_guides_visible =
        canvas_settings.value(QString::fromUtf8(kEditorSafeGuidesVisibleKey), false).toBool();
    canvas_settings.endGroup();

    auto *checkerboard = new QToolButton(canvas_zoom_bar);
    checkerboard->setText("Background");
    checkerboard->setPopupMode(QToolButton::InstantPopup);
    auto *checkerboard_menu = new QMenu(checkerboard);
    auto *checkerboard_group = new QActionGroup(checkerboard_menu);
    checkerboard_group->setExclusive(true);
    auto add_checkerboard_action = [checkerboard_menu, checkerboard_group, saved_checkerboard_pattern](const QString &text, int pattern) {
        QAction *action = checkerboard_menu->addAction(text);
        action->setData(pattern);
        action->setCheckable(true);
        action->setChecked(pattern == saved_checkerboard_pattern);
        checkerboard_group->addAction(action);
        return action;
    };
    add_checkerboard_action("Light", 0);
    add_checkerboard_action("Medium", 1);
    add_checkerboard_action("Dark", 2);
    add_checkerboard_action("Solid white", 3);
    add_checkerboard_action("Solid black", 4);
    add_checkerboard_action("Solid grey", 5);
    QAction *stinger_scene_background_action =
        add_checkerboard_action("Scene A/B", 6);
    stinger_scene_background_action->setToolTip(
        "Show Scene A before the transition point and Scene B at/after it.");
    stinger_scene_background_action->setVisible(false);
    checkerboard->setMenu(checkerboard_menu);
    canvas_->set_checkerboard_pattern(saved_checkerboard_pattern);

    const auto refresh_canvas_background_control =
        [this, checkerboard, checkerboard_group, stinger_scene_background_action]() {
            const bool point_switch = title_ &&
                title_->graphic_type == TitleGraphicType::Stinger &&
                title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint;
            stinger_scene_background_action->setVisible(point_switch);
            const bool use_scene_background = point_switch &&
                title_->stinger_editor_background !=
                    StingerEditorBackground::CanvasTransparency;

            QAction *selected = nullptr;
            if (use_scene_background) {
                selected = stinger_scene_background_action;
            } else {
                const int pattern = canvas_ ? canvas_->checkerboard_pattern() : 1;
                for (QAction *candidate : checkerboard_group->actions()) {
                    if (candidate != stinger_scene_background_action &&
                        candidate->data().toInt() == pattern) {
                        selected = candidate;
                        break;
                    }
                }
            }
            if (selected) {
                QSignalBlocker blocker(checkerboard_group);
                selected->setChecked(true);
                checkerboard->setText(QStringLiteral("Background: %1").arg(selected->text()));
                checkerboard->setToolTip(QStringLiteral("Canvas background: %1").arg(selected->text()));
            }
        };
    connect(checkerboard_menu, &QMenu::aboutToShow, this,
            refresh_canvas_background_control);
    refresh_canvas_background_control();
    if (act_safe_guides_) {
        act_safe_guides_->setChecked(saved_safe_guides_visible);
        canvas_->set_safe_guides_visible(saved_safe_guides_visible);
    }
    canvas_zoom_layout->addWidget(canvas_zoom_out);
    canvas_zoom_layout->addWidget(canvas_zoom_slider);
    canvas_zoom_layout->addWidget(canvas_zoom_in);
    canvas_zoom_layout->addWidget(canvas_zoom_percent);
    canvas_zoom_layout->addWidget(fit_canvas);
    canvas_zoom_layout->addWidget(checkerboard);
    auto *safe_guides = new QToolButton(canvas_zoom_bar);
    safe_guides->setDefaultAction(act_safe_guides_);
    safe_guides->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    safe_guides->setStyleSheet(QStringLiteral(
        "QToolButton:checked {"
        " background-color: palette(highlight); color: palette(highlighted-text);"
        " border: 1px solid palette(highlight); border-radius: 3px; }"));
    canvas_zoom_layout->addWidget(safe_guides);

    auto *overlay_graphics = new QToolButton(canvas_zoom_bar);
    overlay_graphics->setObjectName(QStringLiteral("editorOverlayGraphicsButton"));
    overlay_graphics->setIcon(obs_icon("bounding-box-light.svg"));
    overlay_graphics->setIconSize(QSize(16, 16));
    overlay_graphics->setCheckable(true);
    overlay_graphics->setChecked(canvas_->overlay_graphics_visible());
    overlay_graphics->setToolTip(fxm_tr("OBSTitles.OverlayGraphicsTooltip"));
    overlay_graphics->setToolButtonStyle(Qt::ToolButtonIconOnly);
    overlay_graphics->setStyleSheet(QStringLiteral(
        "QToolButton#editorOverlayGraphicsButton:checked {"
        " background-color: palette(highlight); color: palette(highlighted-text);"
        " border: 1px solid palette(highlight); border-radius: 3px; }"));
    canvas_zoom_layout->addWidget(overlay_graphics);
    connect(overlay_graphics, &QToolButton::toggled, canvas_,
            &CanvasPreview::set_overlay_graphics_visible);

    auto *adaptive_rendering = new LongPressToolButton(canvas_zoom_bar);
    adaptive_rendering->setObjectName(QStringLiteral("editorAdaptiveRenderingButton"));
    adaptive_rendering->setIcon(obs_icon("lightning.svg"));
    adaptive_rendering->setIconSize(QSize(16, 16));
    adaptive_rendering->setCheckable(true);
    adaptive_rendering->setChecked(canvas_->adaptive_rendering_enabled());
    adaptive_rendering->setToolTip(fxm_tr("OBSTitles.AdaptiveRenderingTooltip"));
    adaptive_rendering->setToolButtonStyle(Qt::ToolButtonIconOnly);
    adaptive_rendering->setPopupMode(QToolButton::DelayedPopup);
    adaptive_rendering->setStyleSheet(QStringLiteral(
        "QToolButton#editorAdaptiveRenderingButton:checked {"
        " background-color: palette(highlight); color: palette(highlighted-text);"
        " border: 1px solid palette(highlight); border-radius: 3px; }"));

    auto *adaptive_menu = new QMenu(adaptive_rendering);
    auto *adaptive_group = new QActionGroup(adaptive_menu);
    adaptive_group->setExclusive(true);
    const auto add_quality = [&](const QString &label, CanvasPreview::AdaptiveQualityMode mode) {
        QAction *action = adaptive_menu->addAction(label);
        action->setCheckable(true);
        action->setData(static_cast<int>(mode));
        adaptive_group->addAction(action);
        if (canvas_->adaptive_quality_mode() == mode)
            action->setChecked(true);
        return action;
    };
    add_quality(QStringLiteral("Auto"), CanvasPreview::AdaptiveQualityMode::Auto);
    add_quality(QStringLiteral("75%"), CanvasPreview::AdaptiveQualityMode::Percent75);
    add_quality(QStringLiteral("50%"), CanvasPreview::AdaptiveQualityMode::Percent50);
    add_quality(QStringLiteral("37,5%"), CanvasPreview::AdaptiveQualityMode::Percent37_5);
    add_quality(QStringLiteral("25%"), CanvasPreview::AdaptiveQualityMode::Percent25);
    adaptive_rendering->setMenu(adaptive_menu);
    canvas_zoom_layout->addWidget(adaptive_rendering);

    connect(adaptive_rendering, &QToolButton::toggled, canvas_,
            &CanvasPreview::set_adaptive_rendering_enabled);
    connect(adaptive_group, &QActionGroup::triggered, this, [this, adaptive_rendering](QAction *action) {
        const auto mode = static_cast<CanvasPreview::AdaptiveQualityMode>(action->data().toInt());
        canvas_->set_adaptive_quality_mode(mode);
        adaptive_rendering->setToolTip(
            fxm_tr("OBSTitles.AdaptiveRenderingTooltip") +
            QStringLiteral("\n") + fxm_tr("OBSTitles.AdaptiveRenderingQuality") +
            QStringLiteral(": ") + canvas_->adaptive_quality_label());
    });
    adaptive_rendering->setToolTip(
        fxm_tr("OBSTitles.AdaptiveRenderingTooltip") +
        QStringLiteral("\n") + fxm_tr("OBSTitles.AdaptiveRenderingQuality") +
        QStringLiteral(": ") + canvas_->adaptive_quality_label());

    /* Editor-only 3D controls. These manipulate the CanvasPreview camera and
     * layer transform gizmos; they never change the title's active render
     * camera or the OBS source output. The bar sits above the canvas and is
     * hidden for documents that contain no authored 3D layers. */
    auto *editor_3d_bar = new QWidget(canvas_panel);
    editor_3d_bar->setObjectName(QStringLiteral("editor3DToolbar"));
    editor_3d_bar->setFixedHeight(34);
    editor_3d_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editor_3d_bar->setStyleSheet(QStringLiteral(
        "QWidget#editor3DToolbar{background:%1;border-bottom:1px solid %2;color:%3;}"
        "QWidget#editor3DToolbar QLabel{background:transparent;border:none;color:%3;}"
        "QWidget#editor3DToolbar QPushButton,QWidget#editor3DToolbar QToolButton{"
        "color:%4;background:%5;border:1px solid %2;border-radius:3px;padding:3px 8px;}"
        "QWidget#editor3DToolbar QPushButton:hover,QWidget#editor3DToolbar QToolButton:hover{background:%6;}"
        "QComboBox{color:palette(text);background:palette(base);border:1px solid palette(mid);"
        "border-radius:3px;padding:2px 6px;min-height:20px;}"
        "QComboBox::drop-down{border:none;width:18px;}"
        "QWidget#editor3DToolbar QToolButton:checked{"
        "background:palette(highlight);color:palette(highlighted-text);"
        "border-color:palette(highlight);}"
        "QWidget#editor3DToolbar QToolButton:checked:hover{"
        "background:palette(highlight);color:palette(highlighted-text);}")
        .arg(editor_window.name(QColor::HexRgb),
             editor_border.name(QColor::HexRgb),
             editor_text.name(QColor::HexRgb),
             editor_button_text.name(QColor::HexRgb),
             editor_button.name(QColor::HexRgb),
             editor_hover.name(QColor::HexRgb)));
    auto *editor_3d_layout = new QHBoxLayout(editor_3d_bar);
    editor_3d_layout->setContentsMargins(10, 0, 10, 0);
    editor_3d_layout->setSpacing(6);

    auto *editor_3d_view_label = new QLabel(QStringLiteral("3D View:"), editor_3d_bar);
    auto *editor_3d_view = new QComboBox(editor_3d_bar);
    const auto add_editor_view = [editor_3d_view](const QString &label, CanvasPreview::Editor3DView view) {
        editor_3d_view->addItem(label, static_cast<int>(view));
    };
    add_editor_view(QStringLiteral("Active Camera"), CanvasPreview::Editor3DView::ActiveCamera);
    add_editor_view(QStringLiteral("Front"), CanvasPreview::Editor3DView::Front);
    add_editor_view(QStringLiteral("Back"), CanvasPreview::Editor3DView::Back);
    add_editor_view(QStringLiteral("Left"), CanvasPreview::Editor3DView::Left);
    add_editor_view(QStringLiteral("Right"), CanvasPreview::Editor3DView::Right);
    add_editor_view(QStringLiteral("Top"), CanvasPreview::Editor3DView::Top);
    add_editor_view(QStringLiteral("Bottom"), CanvasPreview::Editor3DView::Bottom);
    add_editor_view(QStringLiteral("Custom Perspective"), CanvasPreview::Editor3DView::CustomPerspective);
    editor_3d_view->setCurrentIndex(editor_3d_view->findData(
        static_cast<int>(canvas_->editor_3d_view())));
    editor_3d_view->setMinimumWidth(132);

    auto *gizmo_group = new QButtonGroup(editor_3d_bar);
    gizmo_group->setExclusive(true);
    const auto make_gizmo_button = [editor_3d_bar, gizmo_group](
        const char *icon_name, const QString &tooltip, CanvasPreview::GizmoMode mode) {
        auto *button = new QToolButton(editor_3d_bar);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setCheckable(true);
        button->setToolTip(tooltip);
        button->setFixedWidth(34);
        gizmo_group->addButton(button, static_cast<int>(mode));
        return button;
    };
    auto *gizmo_move = make_gizmo_button("move.svg",
        QStringLiteral("Move gizmo (W)"), CanvasPreview::GizmoMode::Move);
    auto *gizmo_rotate = make_gizmo_button("rotate.svg",
        QStringLiteral("Rotate gizmo (E)"), CanvasPreview::GizmoMode::Rotate);
    auto *gizmo_scale = make_gizmo_button("scale.svg",
        QStringLiteral("Scale gizmo (R)"), CanvasPreview::GizmoMode::Scale);
    if (auto *current = gizmo_group->button(static_cast<int>(canvas_->gizmo_mode())))
        current->setChecked(true);

    auto *gizmo_space = new QComboBox(editor_3d_bar);
    gizmo_space->addItem(QStringLiteral("Local Axes"), static_cast<int>(TransformAxisSpace::Local));
    gizmo_space->addItem(QStringLiteral("Parent Axes"), static_cast<int>(TransformAxisSpace::Parent));
    gizmo_space->addItem(QStringLiteral("World Axes"), static_cast<int>(TransformAxisSpace::World));
    gizmo_space->setCurrentIndex(gizmo_space->findData(
        static_cast<int>(canvas_->gizmo_space())));
    gizmo_space->setMinimumWidth(82);

    auto *frame_selected_3d = new QPushButton(QStringLiteral("Frame Selected"), editor_3d_bar);
    frame_selected_3d->setToolTip(QStringLiteral("Frame selected 3D layers (F)"));
    auto *frame_all_3d = new QPushButton(QStringLiteral("Frame All"), editor_3d_bar);
    frame_all_3d->setToolTip(QStringLiteral("Frame all 3D layers (Shift+F)"));
    auto *depth_debug_3d = new QToolButton(editor_3d_bar);
    depth_debug_3d->setText(QStringLiteral("Depth"));
    depth_debug_3d->setCheckable(true);
    depth_debug_3d->setChecked(canvas_->depth_debug_visible());
    depth_debug_3d->setToolTip(QStringLiteral(
        "Show editor-only camera-depth and depth-state overlay"));
    auto *normals_debug_3d = new QToolButton(editor_3d_bar);
    normals_debug_3d->setText(QStringLiteral("Normals"));
    normals_debug_3d->setCheckable(true);
    normals_debug_3d->setChecked(canvas_->normals_visible());
    normals_debug_3d->setToolTip(QStringLiteral(
        "Show editor-only front/back normal diagnostics"));

    editor_3d_layout->addWidget(editor_3d_view_label);
    editor_3d_layout->addWidget(editor_3d_view);
    editor_3d_layout->addSpacing(4);
    editor_3d_layout->addWidget(gizmo_move);
    editor_3d_layout->addWidget(gizmo_rotate);
    editor_3d_layout->addWidget(gizmo_scale);
    editor_3d_layout->addWidget(gizmo_space);
    editor_3d_layout->addWidget(frame_selected_3d);
    editor_3d_layout->addWidget(frame_all_3d);
    editor_3d_layout->addWidget(depth_debug_3d);
    editor_3d_layout->addWidget(normals_debug_3d);
    editor_3d_layout->addStretch(1);
    editor_3d_toolbar_ = editor_3d_bar;
    canvas_layout->insertWidget(0, editor_3d_bar);
    update_3d_toolbar_visibility();
    canvas_zoom_layout->addStretch(1);
    canvas_layout->addWidget(canvas_zoom_bar);

    connect(editor_3d_view, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, editor_3d_view](int index) {
        if (!canvas_ || index < 0) return;
        canvas_->set_editor_3d_view(static_cast<CanvasPreview::Editor3DView>(
            editor_3d_view->itemData(index).toInt()));
    });
    connect(canvas_, &CanvasPreview::editor_3d_view_changed, this,
            [editor_3d_view](int view) {
        const int index = editor_3d_view->findData(view);
        if (index < 0 || editor_3d_view->currentIndex() == index) return;
        QSignalBlocker blocker(editor_3d_view);
        editor_3d_view->setCurrentIndex(index);
    });
    connect(gizmo_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [this, gizmo_group](QAbstractButton *button) {
        if (!canvas_ || !button) return;
        canvas_->set_3d_gizmo_mode(static_cast<CanvasPreview::GizmoMode>(
            gizmo_group->id(button)));
    });
    connect(canvas_, &CanvasPreview::gizmo_mode_changed, this,
            [gizmo_group](int mode) {
        if (auto *button = gizmo_group->button(mode)) {
            QSignalBlocker blocker(gizmo_group);
            button->setChecked(true);
        }
    });
    connect(gizmo_space, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, gizmo_space](int index) {
        if (!canvas_ || index < 0) return;
        canvas_->set_3d_gizmo_space(static_cast<TransformAxisSpace>(
            gizmo_space->itemData(index).toInt()));
    });
    connect(canvas_, &CanvasPreview::gizmo_space_changed, this,
            [gizmo_space](int space) {
        const int index = gizmo_space->findData(space);
        if (index < 0 || gizmo_space->currentIndex() == index) return;
        QSignalBlocker blocker(gizmo_space);
        gizmo_space->setCurrentIndex(index);
    });
    connect(frame_selected_3d, &QPushButton::clicked, canvas_,
            &CanvasPreview::frame_3d_selection);
    connect(frame_all_3d, &QPushButton::clicked, canvas_,
            &CanvasPreview::frame_3d_all);
    connect(depth_debug_3d, &QToolButton::toggled, canvas_,
            &CanvasPreview::set_3d_depth_debug_visible);
    connect(normals_debug_3d, &QToolButton::toggled, canvas_,
            &CanvasPreview::set_3d_normals_visible);

    connect(canvas_zoom_slider, &QSlider::valueChanged, canvas_, &CanvasPreview::set_zoom_percent);
    connect(canvas_zoom_percent, qOverload<int>(&QSpinBox::valueChanged), canvas_, &CanvasPreview::set_zoom_percent);
    connect(canvas_, &CanvasPreview::zoom_percent_changed, this, [this, canvas_zoom_slider, canvas_zoom_percent, fit_canvas](int percent) {
        QSignalBlocker slider_blocker(canvas_zoom_slider);
        QSignalBlocker spin_blocker(canvas_zoom_percent);
        canvas_zoom_slider->setValue(percent);
        canvas_zoom_percent->setValue(percent);
        QSignalBlocker fit_blocker(fit_canvas);
        fit_canvas->setChecked(canvas_->fit_zoom_active());
    });
    connect(canvas_zoom_out, &QPushButton::clicked, this, [this]() {
        canvas_->set_zoom_percent((int)std::round(canvas_->zoom_percent() / 1.18));
    });
    connect(canvas_zoom_in, &QPushButton::clicked, this, [this]() {
        canvas_->set_zoom_percent((int)std::round(canvas_->zoom_percent() * 1.18));
    });
    connect(fit_canvas, &QToolButton::clicked, this, [this, fit_canvas]() {
        canvas_->fit_canvas(false);
        fit_canvas->setText(QStringLiteral("Fit"));
        QSignalBlocker fit_blocker(fit_canvas);
        fit_canvas->setChecked(canvas_->fit_zoom_active());
    });
    connect(fit_canvas_menu, &QMenu::triggered, this, [this, fit_canvas](QAction *action) {
        int value = action->data().toInt();
        fit_canvas->setText(action->text());
        if (value == -1) canvas_->fit_canvas(false);
        else if (value == -2) canvas_->fit_canvas(true);
        else canvas_->set_zoom_percent(value);
        QSignalBlocker fit_blocker(fit_canvas);
        fit_canvas->setChecked(canvas_->fit_zoom_active());
    });
    connect(checkerboard_menu, &QMenu::triggered, this,
            [this, refresh_canvas_background_control](QAction *action) {
                if (!action || !canvas_)
                    return;
                const int selection = action->data().toInt();
                const bool point_switch = title_ &&
                    title_->graphic_type == TitleGraphicType::Stinger &&
                    title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint;
                if (selection == 6) {
                    if (!point_switch)
                        return;
                    if (title_->stinger_editor_background !=
                        StingerEditorBackground::FollowSwitchPoint) {
                        title_->stinger_editor_background =
                            StingerEditorBackground::FollowSwitchPoint;
                        on_title_modified(true);
                    }
                    force_next_title_visual_update();
                    canvas_->update();
                } else {
                    canvas_->set_checkerboard_pattern(selection);
                    if (point_switch &&
                        title_->stinger_editor_background !=
                            StingerEditorBackground::CanvasTransparency) {
                        title_->stinger_editor_background =
                            StingerEditorBackground::CanvasTransparency;
                        on_title_modified(true);
                        force_next_title_visual_update();
                    }
                }
                refresh_canvas_background_control();
            });
    upper_split->addWidget(canvas_panel);

    props_ = new PropertiesPanel(this);
    props_->setMinimumWidth(180);
    upper_split->setStretchFactor(0, 1);

    if (execution_context_.host == fxm::editor::EditorHostKind::Standalone) {
        project_panel_ = new fxm::editor::ProjectPanel(this);
        project_panel_->setOpenHandler(
            [this](const std::string &id) { open_project_title(id); });
        project_panel_->setCreateHandler(
            [this]() { create_project_title(); });
        project_panel_->setDuplicateHandler(
            [this](const std::vector<std::string> &ids) {
                duplicate_project_titles(ids);
            });
        project_panel_->setRenameHandler(
            [this](const std::string &id) { rename_project_title(id); });
        project_panel_->setDeleteHandler(
            [this](const std::vector<std::string> &ids) {
                delete_project_titles(ids);
            });
        project_panel_->setImportHandler(
            [this]() { import_project_titles(); });
        project_panel_->setExportHandler(
            [this](const std::vector<std::string> &ids) {
                export_project_titles(ids);
            });
        project_panel_->setInsertHandler([this](const std::string &id) {
            const QPointF point = canvas_ ? canvas_->view_center_canvas_point()
                : QPointF(title_ ? title_->width * 0.5 : 960.0,
                          title_ ? title_->height * 0.5 : 540.0);
            insert_asset_layer(id, point);
        });
        project_panel_->setReorderHandler(
            [this](const std::vector<std::string> &ids) {
                reorder_project_titles(ids);
            });
        project_dock_ = create_editor_dock(
            QStringLiteral("FluxMotionProjectDock"),
            QStringLiteral("Project"), project_panel_);
        project_dock_->setMinimumWidth(230);
    }

    graphic_props_dock_ = create_editor_dock(QString::fromUtf8(kGraphicPropertiesDockObjectName),
                                             fxm_tr("OBSTitles.GraphicProperties"),
                                             title_props_);
    auto *three_d_scene_scroll = new QScrollArea(this);
    three_d_scene_scroll->setObjectName(QStringLiteral("Fxm3DSceneDockScroll"));
    three_d_scene_scroll->setWidgetResizable(true);
    three_d_scene_scroll->setMinimumWidth(260);
    three_d_scene_scroll->setFrameShape(QFrame::NoFrame);
    three_d_scene_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    three_d_scene_scroll->setWidget(title_props_->scene_controls_widget());
    three_d_scene_dock_ = create_editor_dock(QString::fromUtf8(kThreeDSceneDockObjectName),
                                             QStringLiteral("3D Camera, Lights & Environment"),
                                             three_d_scene_scroll);
    layer_props_dock_ = create_editor_dock(QString::fromUtf8(kLayerPropertiesDockObjectName),
                                           fxm_tr("OBSTitles.LayerProperties"),
                                           props_);
    effects_dock_ = create_editor_dock(QString::fromUtf8(kEffectsDockObjectName),
                                       fxm_tr("OBSTitles.EffectSettings"),
                                       create_effects_panel());
    effects_presets_dock_ = create_editor_dock(QString::fromUtf8(kEffectsPresetsDockObjectName),
                                               fxm_tr("OBSTitles.EffectsAndPresets"),
                                               create_effects_presets_panel());
    styles_dock_ = create_editor_dock(QString::fromUtf8(kStylesDockObjectName),
                                      fxm_tr("OBSTitles.Libraries"),
                                      create_styles_panel());
    prerender_dock_ = create_editor_dock(QString::fromUtf8(kPrerenderDockObjectName),
                                         fxm_tr("OBSTitles.PlaybackAndCache"),
                                         create_prerender_panel());
    editor_audio_dock_ = create_editor_dock(QString::fromUtf8(kEditorAudioDockObjectName),
                                            QStringLiteral("Editor Audio"),
                                            create_editor_audio_panel());
    editor_audio_dock_->setMinimumWidth(78);
    editor_audio_dock_->setMaximumWidth(112);
    connect(editor_audio_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool visible) {
                if (!editor_audio_meter_timer_)
                    return;
                if (visible) {
                    if (!editor_audio_meter_timer_->isActive())
                        editor_audio_meter_timer_->start();
                    update_editor_audio_meter();
                } else {
                    editor_audio_meter_timer_->stop();
                }
            });
    tools_sidebar_ = new ToolsSidebar(this);
    tools_dock_ = create_editor_dock(QStringLiteral("FluxMotionToolsDock"),
                                     fxm_tr("OBSTitles.Tools"),
                                     tools_sidebar_);
    tools_dock_->setMinimumWidth(46);
    tools_dock_->setMaximumWidth(64);
    if (project_dock_)
        addDockWidget(Qt::LeftDockWidgetArea, project_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, graphic_props_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, three_d_scene_dock_);
    tabifyDockWidget(graphic_props_dock_, three_d_scene_dock_);
    if (project_dock_)
        splitDockWidget(project_dock_, graphic_props_dock_, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, tools_dock_);
    addDockWidget(Qt::RightDockWidgetArea, layer_props_dock_);
    splitDockWidget(tools_dock_, layer_props_dock_, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, styles_dock_);
    splitDockWidget(graphic_props_dock_, styles_dock_, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, effects_presets_dock_);
    tabifyDockWidget(styles_dock_, effects_presets_dock_);
    addDockWidget(Qt::RightDockWidgetArea, effects_dock_);
    splitDockWidget(layer_props_dock_, effects_dock_, Qt::Horizontal);
    addDockWidget(Qt::RightDockWidgetArea, editor_audio_dock_);
    splitDockWidget(layer_props_dock_, editor_audio_dock_, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, prerender_dock_);
    tabifyDockWidget(editor_audio_dock_, prerender_dock_);
    editor_audio_dock_->raise();
    tools_dock_->raise();
    graphic_props_dock_->raise();
    if (project_dock_)
        project_dock_->raise();
    layer_props_dock_->raise();
    styles_dock_->raise();
    resizeDocks({styles_dock_, graphic_props_dock_}, {320, 320}, Qt::Horizontal);

    /* ── Timeline editor: full-width transport | LayerStack + Timeline | full-width zoom ── */
    auto *timeline_editor = new QWidget(this);
    auto *timeline_editor_layout = new QVBoxLayout(timeline_editor);
    timeline_editor_layout->setContentsMargins(0, 0, 0, 0);
    timeline_editor_layout->setSpacing(0);

    auto *timeline_transport = new QWidget(timeline_editor);
    timeline_transport->setFixedHeight(34);
    const QPalette timeline_pal = qApp->palette();
    const QColor timeline_window = timeline_pal.color(QPalette::Window);
    const QColor timeline_base = timeline_pal.color(QPalette::Base);
    const QColor timeline_text = timeline_pal.color(QPalette::WindowText);
    const QColor timeline_button = timeline_pal.color(QPalette::Button);
    const QColor timeline_button_text = timeline_pal.color(QPalette::ButtonText);
    const QColor timeline_border = timeline_pal.color(QPalette::Mid);
    const QColor timeline_highlight = timeline_pal.color(QPalette::Highlight);
    const QColor timeline_hover = timeline_button.lightness() < 128 ? timeline_button.lighter(125) : timeline_button.darker(108);
    timeline_transport->setStyleSheet(QStringLiteral(
        "QWidget{background:%1;border-bottom:1px solid %2;color:%3;}"
        "QToolButton{color:%4;background:transparent;padding:3px 7px;border:none;}"
        "QToolButton:hover{background:%5;border-radius:2px;}"
        "QToolButton:checked{background:%6;color:%7;border-radius:2px;}")
        .arg(timeline_window.name(QColor::HexRgb),
             timeline_border.name(QColor::HexRgb),
             timeline_text.name(QColor::HexRgb),
             timeline_button_text.name(QColor::HexRgb),
             timeline_hover.name(QColor::HexRgb),
             timeline_highlight.name(QColor::HexRgb),
             timeline_pal.color(QPalette::HighlightedText).name(QColor::HexRgb)));
    auto *transport_layout = new QHBoxLayout(timeline_transport);
    transport_layout->setContentsMargins(8, 0, 8, 0);
    transport_layout->setSpacing(2);
    auto make_transport_button = [timeline_transport](QAction *action) {
        auto *button = new QToolButton(timeline_transport);
        button->setDefaultAction(action);
        button->setIconSize(QSize(14, 14));
        button->setAutoRaise(true);
        return button;
    };
    transport_layout->addStretch(1);
    time_lbl_ = new QLabel("0.000 s", timeline_transport);
    time_lbl_->setMinimumWidth(116);
    time_lbl_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time_lbl_->setStyleSheet(QStringLiteral(
        "background:%1;color:%2;border-radius:2px;padding:2px 6px;")
        .arg(timeline_highlight.name(QColor::HexRgb),
             timeline_text.name(QColor::HexRgb)));
    transport_layout->addWidget(time_lbl_, 0, Qt::AlignVCenter);
    transport_layout->addSpacing(8);
    transport_layout->addWidget(make_transport_button(act_go_start_));
    transport_layout->addWidget(make_transport_button(act_prev_kf_));
    transport_layout->addWidget(make_transport_button(act_step_back_));
    transport_layout->addWidget(make_transport_button(act_rew_));
    transport_layout->addWidget(make_transport_button(act_play_));
    QAction *step_forward_action = new QAction(obs_icon("step-forward.svg"), fxm_tr("OBSTitles.StepForward"), timeline_transport);
    connect(step_forward_action, &QAction::triggered, this, &TitleEditor::step_forward);
    transport_layout->addWidget(make_transport_button(step_forward_action));
    transport_layout->addWidget(make_transport_button(act_next_kf_));
    transport_layout->addWidget(make_transport_button(act_go_end_));
    transport_layout->addStretch(1);
    timeline_editor_layout->addWidget(timeline_transport);

    auto *lower_split = new QSplitter(Qt::Horizontal, timeline_editor);

    auto *layers_panel = new QWidget(lower_split);
    auto *layers_layout = new QVBoxLayout(layers_panel);
    layers_layout->setContentsMargins(0, 0, 0, 0);
    layers_layout->setSpacing(0);

    layers_ = new LayerStack(layers_panel);
    layers_->setMinimumHeight(140);
    layers_layout->addWidget(layers_, 1);
    layers_panel->setMinimumWidth(layers_->minimumWidth());
    layers_panel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    lower_split->addWidget(layers_panel);

    auto *timeline_panel = new QWidget(lower_split);
    auto *timeline_panel_layout = new QVBoxLayout(timeline_panel);
    timeline_panel_layout->setContentsMargins(0, 0, 0, 0);
    timeline_panel_layout->setSpacing(0);

    timeline_ = new TimelineWidget(timeline_panel);
    timeline_->setMinimumHeight(140);
    timeline_panel_layout->addWidget(timeline_, 1);

    auto *timeline_zoom_bar = new QWidget(timeline_panel);
    timeline_zoom_bar->setFixedHeight(34);
    timeline_zoom_bar->setStyleSheet(QStringLiteral(
        "QWidget{background:%1;border-top:1px solid %2;color:%3;}"
        "QPushButton{color:%4;background:%5;border:1px solid %2;border-radius:3px;padding:3px 8px;}"
        "QPushButton:hover{background:%6;}"
        "QPushButton:checked{background:%8;color:%9;border-color:%8;}")
        .arg(timeline_window.name(QColor::HexRgb),
             timeline_border.name(QColor::HexRgb),
             timeline_text.name(QColor::HexRgb),
             timeline_button_text.name(QColor::HexRgb),
             timeline_button.name(QColor::HexRgb),
             timeline_hover.name(QColor::HexRgb),
             timeline_base.name(QColor::HexRgb),
             timeline_highlight.name(QColor::HexRgb),
             timeline_pal.color(QPalette::HighlightedText).name(QColor::HexRgb)));
    auto *zoom_layout = new QHBoxLayout(timeline_zoom_bar);
    zoom_layout->setContentsMargins(10, 0, 10, 0);
    zoom_layout->setSpacing(8);
    auto *graph_editor_toggle = new QPushButton(fxm_tr("OBSTitles.GraphEditor"), timeline_zoom_bar);
    graph_editor_toggle->setIcon(obs_icon("graph.svg"));
    graph_editor_toggle->setIconSize(QSize(16, 16));
    graph_editor_toggle->setCheckable(true);
    graph_editor_toggle->setToolTip(fxm_tr("OBSTitles.GraphEditorTooltip"));
    auto *graph_view = new QComboBox(timeline_zoom_bar);
    graph_view->addItem(fxm_tr("OBSTitles.ValueGraph"), 0);
    graph_view->addItem(fxm_tr("OBSTitles.SpeedGraph"), 1);
    graph_view->setMinimumWidth(110);

    auto *graph_channels = new QWidget(timeline_zoom_bar);
    graph_channels->setObjectName(QStringLiteral("graphChannelToggles"));
    auto *graph_channels_layout = new QHBoxLayout(graph_channels);
    graph_channels_layout->setContentsMargins(0, 0, 0, 0);
    graph_channels_layout->setSpacing(2);
    auto *graph_channel_group = new QButtonGroup(graph_channels);
    graph_channel_group->setExclusive(true);
    auto make_graph_channel = [&](const QString &label, int channel,
                                  const QString &tooltip) {
        auto *button = new QPushButton(label, graph_channels);
        button->setObjectName(QStringLiteral("graphChannel%1").arg(label));
        button->setCheckable(true);
        button->setFixedWidth(label == QStringLiteral("All") ? 40 : 28);
        button->setToolTip(tooltip);
        graph_channel_group->addButton(button, channel);
        graph_channels_layout->addWidget(button);
        return button;
    };
    auto *graph_channel_x = make_graph_channel(QStringLiteral("X"), 0,
        QStringLiteral("Edit only the X channel in the Graph Editor."));
    auto *graph_channel_y = make_graph_channel(QStringLiteral("Y"), 1,
        QStringLiteral("Edit only the Y channel in the Graph Editor."));
    auto *graph_channel_z = make_graph_channel(QStringLiteral("Z"), 2,
        QStringLiteral("Edit only the Z channel in the Graph Editor."));
    auto *graph_channel_fourth = make_graph_channel(QStringLiteral("A/W"), 4,
        QStringLiteral("Edit the fourth channel (Alpha or W) in the Graph Editor."));
    auto *graph_channel_all = make_graph_channel(QStringLiteral("All"), 3,
        QStringLiteral("Show and edit all vector channels together."));
    auto apply_graph_channel_color = [this](QPushButton *button, int component) {
        if (!button) return;
        const QColor color = timeline_->graph_channel_color(component);
        const QColor checked_text = color.lightness() < 145 ? QColor(Qt::white)
                                                            : QColor(24, 24, 24);
        button->setStyleSheet(QStringLiteral(
            "QPushButton:checked{background:%1;color:%2;border-color:%1;}"
            "QPushButton:checked:hover{background:%3;border-color:%3;}")
            .arg(color.name(QColor::HexRgb),
                 checked_text.name(QColor::HexRgb),
                 color.lighter(116).name(QColor::HexRgb)));
    };
    apply_graph_channel_color(graph_channel_x, 0);
    apply_graph_channel_color(graph_channel_y, 1);
    apply_graph_channel_color(graph_channel_z, 2);
    apply_graph_channel_color(graph_channel_fourth, 3);
    graph_channel_all->setChecked(true);
    graph_channels->setVisible(false);
    auto *fit_graphs = new QPushButton(fxm_tr("OBSTitles.FitGraphsToView"), timeline_zoom_bar);
    auto *fit_graph_selection = new QPushButton(fxm_tr("OBSTitles.FitGraphSelection"), timeline_zoom_bar);
    graph_view->setVisible(false);
    fit_graphs->setVisible(false);
    fit_graph_selection->setVisible(false);

    auto *timeline_scroll_zoom =
        new TimelineScrollZoomBar(timeline_, timeline_zoom_bar);
    zoom_layout->addWidget(graph_editor_toggle);
    zoom_layout->addWidget(graph_view);
    zoom_layout->addWidget(graph_channels);
    zoom_layout->addWidget(fit_graphs);
    zoom_layout->addWidget(fit_graph_selection);
    zoom_layout->addWidget(timeline_scroll_zoom, 2);
    connect(graph_editor_toggle, &QPushButton::toggled, timeline_,
            &TimelineWidget::set_graph_editor_enabled);
    connect(timeline_, &TimelineWidget::graph_editor_enabled_changed, this,
            [this, graph_editor_toggle, graph_view, graph_channels,
             fit_graphs, fit_graph_selection](bool enabled) {
        QSignalBlocker blocker(graph_editor_toggle);
        graph_editor_toggle->setChecked(enabled);
        graph_view->setVisible(enabled);
        graph_channels->setVisible(enabled && timeline_->graph_channel_count() > 1);
        fit_graphs->setVisible(enabled);
        fit_graph_selection->setVisible(enabled);
    });
    connect(graph_view, QOverload<int>::of(&QComboBox::currentIndexChanged), timeline_,
            &TimelineWidget::set_graph_view_mode);
    connect(timeline_, &TimelineWidget::graph_view_mode_changed, this,
            [graph_view](int mode) {
        QSignalBlocker blocker(graph_view);
        graph_view->setCurrentIndex(std::clamp(mode, 0, graph_view->count() - 1));
    });
    connect(graph_channel_group, &QButtonGroup::idClicked, timeline_,
            &TimelineWidget::set_graph_channel_mode);
    connect(timeline_, &TimelineWidget::graph_channel_mode_changed, this,
            [graph_channel_group](int mode) {
        if (QAbstractButton *button = graph_channel_group->button(mode)) {
            QSignalBlocker blocker(graph_channel_group);
            button->setChecked(true);
        }
    });
    connect(timeline_, &TimelineWidget::graph_channel_count_changed, this,
            [this, graph_channels, graph_channel_x, graph_channel_y,
             graph_channel_z, graph_channel_fourth, graph_channel_all,
             apply_graph_channel_color](int count) {
        graph_channel_x->setText(timeline_->graph_channel_label(0));
        graph_channel_y->setText(timeline_->graph_channel_label(1));
        graph_channel_z->setText(timeline_->graph_channel_label(2));
        graph_channel_fourth->setText(timeline_->graph_channel_label(3));
        apply_graph_channel_color(graph_channel_x, 0);
        apply_graph_channel_color(graph_channel_y, 1);
        apply_graph_channel_color(graph_channel_z, 2);
        apply_graph_channel_color(graph_channel_fourth, 3);
        graph_channel_x->setToolTip(QStringLiteral("Edit only the %1 channel in the Graph Editor.")
                                        .arg(graph_channel_x->text()));
        graph_channel_y->setToolTip(QStringLiteral("Edit only the %1 channel in the Graph Editor.")
                                        .arg(graph_channel_y->text()));
        graph_channel_z->setToolTip(QStringLiteral("Edit only the %1 channel in the Graph Editor.")
                                        .arg(graph_channel_z->text()));
        graph_channel_fourth->setToolTip(QStringLiteral("Edit only the %1 channel in the Graph Editor.")
                                             .arg(graph_channel_fourth->text()));
        graph_channel_x->setVisible(count >= 2);
        graph_channel_y->setVisible(count >= 2);
        graph_channel_z->setVisible(count >= 3);
        graph_channel_fourth->setVisible(count >= 4);
        graph_channel_all->setVisible(count >= 2);
        graph_channels->setVisible(timeline_->graph_editor_enabled() && count >= 2);
    });
    connect(fit_graphs, &QPushButton::clicked, timeline_, &TimelineWidget::fit_graph_to_view);
    connect(fit_graph_selection, &QPushButton::clicked, timeline_,
            &TimelineWidget::fit_graph_selection);
    timeline_panel_layout->addWidget(timeline_zoom_bar);
    lower_split->addWidget(timeline_panel);

    if (auto *scroll_bar = layers_->vertical_scroll_bar()) {
        connect(scroll_bar, &QScrollBar::valueChanged, timeline_, &TimelineWidget::set_vertical_scroll);
        connect(timeline_, &TimelineWidget::vertical_scroll_delta_requested, this,
                [scroll_bar](int delta) { scroll_bar->setValue(scroll_bar->value() + delta); });
    }
    /* Start at the smallest usable Layer List width and assign every remaining
     * pixel to the timeline strips. The handle stays movable for users who
     * prefer a wider metadata list. */
    lower_split->setStretchFactor(0, 0);
    lower_split->setStretchFactor(1, 1);
    lower_split->setSizes({layers_->minimumWidth(), 100000});
    lower_split->setCollapsible(0, false);
    lower_split->setCollapsible(1, false);
    timeline_editor_layout->addWidget(lower_split, 1);

    root->addWidget(upper_split, 1);

    timeline_editor->setMinimumHeight(210);
    timeline_dock_ = create_editor_dock(QString::fromUtf8(kTimelineDockObjectName),
                                        fxm_tr("OBSTitles.Timeline"),
                                        timeline_editor);
    timeline_dock_->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    timeline_dock_->setMinimumHeight(220);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    connect(&CacheManager::instance(), &CacheManager::frameReady, this,
            [this](const QString &title_id, int frame) {
                if (!title_ || title_id != QString::fromStdString(title_->id)) return;
                const int current_frame =
                    CacheManager::instance().frameIndexForTitleTime(
                        *title_, playhead_);
                if (canvas_ && frame == current_frame) canvas_->refresh_preview();
                if (timeline_) timeline_->update();
                try_start_playback_after_prerender();
            });
    connect(&CacheManager::instance(), &CacheManager::cacheStatesChanged, this,
            [this](const QString &title_id, int, int) {
                if (!title_id.isEmpty() && title_ && title_id != QString::fromStdString(title_->id)) return;
                if (timeline_) timeline_->update();
                try_start_playback_after_prerender();
            });
    connect(&fxm::video::FrameRuntime::instance(),
            &fxm::video::FrameRuntime::frameReady, this,
            [this](const QString &layer_id) {
                if (!title_) return;
                const auto layer = title_->find_layer(layer_id.toStdString());
                if (!layer || layer->type != LayerType::Video) return;
                /* Video decode is asynchronous. A decoded frame may arrive after
                 * the current paint transaction both while paused and during
                 * realtime playback, so the canvas must be invalidated whenever
                 * the frame for the current Video layer is published. */
                if (canvas_)
                    canvas_->refresh_preview();
            }, Qt::QueuedConnection);

    load_editor_layout();

    /* ── Connect sub-widget signals ── */
    connect(layers_, &LayerStack::layer_selected,
            this, &TitleEditor::on_layer_selected);
    connect(layers_, &LayerStack::layers_selected,
            this, [this](const std::vector<std::string> &ids) {
                synchronize_layer_selection(ids);
            });

    connect(layers_, &LayerStack::add_layer_requested,
            this, [this](LayerType type) {
                if (!title_) return;
                if (type == LayerType::Image) {
                    // Image layers use the same draw-first workflow as toolbar objects:
                    // activate the image tool, then draw the image box on the canvas.
                    if (tools_sidebar_)
                        tools_sidebar_->activate_image_tool();
                    else if (canvas_)
                        canvas_->set_image_tool_active();
                    return;
                }
                QString media_path;
                if (type == LayerType::Audio) {
                    media_path = QFileDialog::getOpenFileName(
                        this, fxm_tr("OBSTitles.ImportAudio"), QString(),
                        fxm_tr("OBSTitles.AudioFilesFilter"));
                    if (media_path.isEmpty()) return;
                } else if (type == LayerType::Video) {
                    media_path = QFileDialog::getOpenFileName(
                        this, fxm_tr("OBSTitles.ImportVideo"), QString(),
                        fxm_tr("OBSTitles.VideoFilesFilter"));
                    if (media_path.isEmpty()) return;
                }
                fxm::video::MediaInfo video_media;
                if (type == LayerType::Video) {
                    video_media = fxm::video::probe_media(media_path.toStdString());
                    if (!video_media.valid) {
                        QMessageBox::warning(
                            this, fxm_tr("OBSTitles.ImportVideo"),
                            video_media.error.empty()
                                ? fxm_tr("OBSTitles.VideoOpenFailed")
                                : QString::fromStdString(video_media.error));
                        return;
                    }
                }
                auto l = create_basic_layer(
                    type, (type == LayerType::Audio || type == LayerType::Video)
                        ? QFileInfo(media_path).completeBaseName() : QString());
                if (!l) return;
                if (type == LayerType::Audio) {
                    const QString audio_path = media_path;
                    l->audio_source = audio_path.toStdString();
                    l->audio_out_point = std::max(0.0, l->out_time - l->in_time);
                    QFile media(audio_path);
                    if (media.open(QIODevice::ReadOnly)) {
                        const QByteArray bytes = media.readAll();
                        constexpr int peak_pairs = 512;
                        l->audio_waveform.reserve(peak_pairs * 2);
                        for (int bucket = 0; bucket < peak_pairs; ++bucket) {
                            const qsizetype begin = bytes.size() * bucket / peak_pairs;
                            const qsizetype end = bytes.size() * (bucket + 1) / peak_pairs;
                            float lo = 0.0f, hi = 0.0f;
                            for (qsizetype i = begin; i < end; ++i) {
                                const float sample = (static_cast<unsigned char>(bytes.at(i)) - 127.5f) / 127.5f;
                                lo = std::min(lo, sample);
                                hi = std::max(hi, sample);
                            }
                            l->audio_waveform.push_back(lo);
                            l->audio_waveform.push_back(hi);
                        }
                    }
                } else if (type == LayerType::Video) {
                    const fxm::video::MediaInfo &media = video_media;
                    l->group_collapsed = true;
                    l->video_source = media_path.toStdString();
                    l->video_stream_index = media.video_stream_index;
                    l->video_media_duration = media.duration;
                    l->video_out_point = media.duration;
                    l->video_frame_rate = media.frame_rate;
                    l->video_pixel_width = media.width;
                    l->video_pixel_height = media.height;
                    l->video_has_alpha = media.has_alpha;
                    l->video_has_hdr = media.has_hdr;
                    l->video_source_time = AnimatedProperty{"video_source_time", l->video_in_point};
                    l->video_source_time.keyframes = {
                        Keyframe{0.0, l->video_in_point, EasingType::Linear},
                        Keyframe{std::max(0.0, l->video_out_point - l->video_in_point),
                                 l->video_out_point, EasingType::Linear}
                    };
                    l->video_time_remap_enabled = false;
                    l->video_time_remap_loop_segments.clear();
                    l->video_time_remap_curve_fingerprint.clear();
                    l->video_optical_flow_cache_fingerprint.clear();
                    l->image_width = std::max(1, media.width);
                    l->image_height = std::max(1, media.height);
                    l->image_size.static_value.x = l->image_width;
                    l->image_size.static_value.y = l->image_height;
                    const double aspect = static_cast<double>(l->image_width) /
                        std::max(1.0f, l->image_height);
                    l->rect_width = std::min<double>(title_->width * 0.6,
                                                     l->image_width);
                    l->rect_height = l->rect_width / std::max(0.0001, aspect);
                    if (l->rect_height > title_->height * 0.6) {
                        l->rect_height = title_->height * 0.6;
                        l->rect_width = l->rect_height * aspect;
                    }
                    l->size.static_value.x = l->rect_width;
                    l->size.static_value.y = l->rect_height;
                    if (media.duration > 0.0)
                        l->out_time = std::min(title_->duration, media.duration);
                    l->video_out_point = std::min(
                        media.duration > 0.0 ? media.duration
                                             : std::max(0.0, l->out_time - l->in_time),
                        std::max(0.0, l->out_time - l->in_time));
                }
                title_->add_layer(l);
                if (type == LayerType::Video) {
                    int ordinal = 1;
                    for (const auto &stream : video_media.audio_streams) {
                        QString stream_name = stream.title.empty()
                            ? fxm_tr("OBSTitles.VideoAudioStream").arg(ordinal)
                            : QString::fromStdString(stream.title);
                        if (!stream.language.empty())
                            stream_name += QStringLiteral(" [%1]").arg(
                                QString::fromStdString(stream.language));
                        auto audio = create_basic_layer(LayerType::Audio, stream_name);
                        if (!audio) continue;
                        audio->parent_id = l->id;
                        audio->linked_media_layer_id = l->id;
                        audio->linked_media_stream = true;
                        audio->media_stream_label = stream_name.toStdString();
                        audio->audio_source = media_path.toStdString();
                        audio->audio_stream_index = stream.stream_index;
                        audio->audio_sample_rate = stream.sample_rate;
                        audio->audio_channels = stream.channels;
                        audio->audio_media_duration = l->video_media_duration;
                        audio->audio_in_point = l->video_in_point;
                        audio->audio_out_point = l->video_out_point;
                        audio->audio_independent = false;
                        audio->in_time = l->in_time;
                        audio->out_time = l->out_time;
                        title_->add_layer(audio);
                        ++ordinal;
                    }
                    synchronize_video_audio_streams(*title_, l->id);
                }
                if (type == LayerType::Text)
                    pending_text_layer_auto_names_.insert(l->id);
                layers_->refresh();
                on_layer_selected(l->id);
                force_next_title_visual_update();
                on_title_modified();
                if (type == LayerType::Text && canvas_)
                    canvas_->begin_text_edit_for_layer(l->id);
            });

    connect(layers_, &LayerStack::add_light_requested,
            this, [this](TitleLightType light_type) {
                if (!title_) return;
                const auto light_count = std::count_if(
                    title_->layers.begin(), title_->layers.end(),
                    [](const auto &layer) { return layer && layer->type == LayerType::Light; });
                if (light_count >= 4) return;
                auto layer = std::make_shared<Layer>();
                layer->id = TitleDataStore::make_uuid();
                layer->type = LayerType::Light;
                layer->dimension_mode = LayerDimensionMode::ThreeD;
                const char *base = light_type == TitleLightType::Ambient ? "Ambient Light" :
                                   light_type == TitleLightType::Point ? "Point Light" :
                                   light_type == TitleLightType::Spot ? "Spot Light" :
                                   light_type == TitleLightType::Parallel ? "Parallel Light" :
                                   "Environment Light";
                int ordinal = 1;
                std::string candidate;
                do { candidate = std::string(base) + " " + std::to_string(ordinal++); }
                while (std::any_of(title_->layers.begin(), title_->layers.end(),
                    [&](const auto &other) { return other && other->name == candidate; }));
                layer->name = candidate;
                layer->position.static_value = {title_->width * 0.5, title_->height * 0.5};
                layer->position_z.static_value = -1000.0;
                layer->light.id = layer->id;
                layer->light.name = layer->name;
                layer->light.type = light_type;
                layer->light.position.static_value = {0.0, 0.0, 0.0};
                layer->light.target.static_value = {0.0, 0.0, 1000.0};
                layer->light.casts_shadows = light_type == TitleLightType::Spot ||
                                              light_type == TitleLightType::Parallel;
                title_->add_layer(layer);
                title_->default_light_enabled = false;
                layers_->refresh();
                on_layer_selected(layer->id);
                if (timeline_) { timeline_->set_title(title_); timeline_->update(); }
                if (title_props_) title_props_->set_title(title_);
                force_next_title_visual_update();
                on_title_modified();
            });

    connect(layers_, &LayerStack::add_camera_requested,
            this, [this]() {
                if (!title_) return;
                TitleCamera camera;
                camera.id = TitleDataStore::make_uuid();
                const size_t custom_camera_count = static_cast<size_t>(std::count_if(
                    title_->cameras.begin(), title_->cameras.end(),
                    [](const TitleCamera &existing) {
                        return existing.asset_space_owner_id.empty() &&
                               (existing.id != "default" ||
                                !existing.use_canvas_default);
                    }));
                camera.name = QStringLiteral("Camera %1")
                                  .arg(custom_camera_count + 1).toStdString();
                camera.use_canvas_default = false;
                camera.position_x.static_value = title_->width * 0.5;
                camera.position_y.static_value = title_->height * 0.5;
                camera.position_z.static_value = -1000.0;
                camera.position_3d.static_value = {
                    camera.position_x.static_value,
                    camera.position_y.static_value,
                    camera.position_z.static_value};
                camera.target_x.static_value = title_->width * 0.5;
                camera.target_y.static_value = title_->height * 0.5;
                camera.target_z.static_value = 0.0;
                camera.target_3d.static_value = {
                    camera.target_x.static_value,
                    camera.target_y.static_value,
                    camera.target_z.static_value};
                const std::string camera_id = camera.id;
                title_->cameras.push_back(std::move(camera));
                /* A camera created from Add Layer is immediately the render
                 * camera.  An already-authored switch track receives a key at
                 * the current playhead; otherwise its base value changes. */
                if (title_->active_camera.is_animated())
                    title_->active_camera.set(playhead_, camera_id);
                else
                    title_->active_camera.static_value = camera_id;
                title_->active_camera_id = camera_id;
                if (layers_) layers_->refresh();
                if (timeline_) {
                    timeline_->set_title(title_);
                    timeline_->update();
                }
                if (title_props_) title_props_->set_title(title_);
                force_next_title_visual_update();
                on_title_modified();
            });

    connect(layers_, &LayerStack::clone_layer_requested,
            this, [this](const std::string &lid) {
                if (!title_) return;
                auto selected = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
                if (std::find(selected.begin(), selected.end(), lid) == selected.end())
                    on_layer_selected(lid);
                duplicate_selected_layers();
            });

    connect(layers_, &LayerStack::copy_layer_requested,
            this, [this](const std::string &lid) {
                auto selected = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
                if (std::find(selected.begin(), selected.end(), lid) == selected.end())
                    on_layer_selected(lid);
                copy_selected_layer();
            });

    connect(layers_, &LayerStack::paste_layer_requested,
            this, [this](const std::string &anchor_id) {
                if (!anchor_id.empty()) on_layer_selected(anchor_id);
                paste_layer_from_clipboard();
            });

    connect(layers_, &LayerStack::delete_layer_requested,
            this, [this](const std::string &lid) {
                auto selected = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
                if (std::find(selected.begin(), selected.end(), lid) == selected.end())
                    on_layer_selected(lid);
                delete_selected_layer();
            });

    connect(layers_, &LayerStack::layer_visibility_changed,
            this, [this](const std::string &lid, bool visible) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->visible = visible;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_audio_mute_changed,
            this, [this](const std::string &lid, bool muted) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->audio_muted = muted;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_audio_volume_changed,
            this, [this](const std::string &lid, float volume) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->audio_volume = std::clamp(volume, 0.0f, 2.0f);
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_lock_changed,
            this, [this](const std::string &lid, bool locked) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->locked = locked;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::property_keyframe_toggled,
            this, [this](const std::string &lid, const std::string &property_name) {
                if (!title_) return;
                if (timeline_owner_locked(*title_, lid)) return;
                const double owner_in = timeline_owner_in_time(*title_, lid);
                const double owner_out = timeline_owner_out_time(*title_, lid);
                const double lt = std::clamp(playhead_ - owner_in, 0.0,
                                             std::max(0.0, owner_out - owner_in));
                auto prop = timeline_property_for_owner(*title_, lid, property_name);
                if (!prop) return;
                prop.toggle_at(lt);
                layers_->refresh();
                timeline_->set_title(title_);
                on_title_modified();
            });

    connect(layers_, &LayerStack::property_graph_target_requested,
            this, [this](const std::string &owner_id,
                         const std::string &property_name,
                         int channel_mode) {
                if (!timeline_) return;
                timeline_->select_graph_property(owner_id, property_name,
                                                 channel_mode);
            });

    connect(layers_, &LayerStack::property_value_changed,
            this, [this](const std::string &lid, const std::string &property_name,
                         double x, double y, double z) {
                if (!title_) return;
                if (timeline_owner_locked(*title_, lid)) return;
                const double owner_in = timeline_owner_in_time(*title_, lid);
                const double owner_out = timeline_owner_out_time(*title_, lid);
                const double lt = std::clamp(playhead_ - owner_in, 0.0,
                                             std::max(0.0, owner_out - owner_in));
                auto prop = timeline_property_for_owner(*title_, lid, property_name);
                if (!prop || prop.is_discrete()) return;
                if (property_name == "scale") { x /= 100.0; y /= 100.0; z /= 100.0; }
                if (property_name == "opacity" || property_name == "char_scale_x" || property_name == "char_scale_y")
                    x /= 100.0;
                if (prop.vector3) {
                    Vec3Value next = prop.vector3->evaluate(lt);
                    next.x = x;
                    next.y = y;
                    next.z = z;
                    prop.activate_vector3();
                    set_animated_value(*prop.vector3, lt, next);
                } else if (prop.is_scalar_group()) {
                    const double values[] = {x, y, z};
                    for (size_t channel = 0; channel < prop.scalar_group.size() && channel < 3; ++channel) {
                        if (prop.scalar_group[channel])
                            set_animated_value(*prop.scalar_group[channel], lt, values[channel]);
                    }
                } else if (prop.vector) {
                    Vec2Value next = prop.vector->evaluate(lt);
                    next.x = x;
                    next.y = y;
                    next.z = z;
                    set_animated_value(*prop.vector, lt, next);
                } else if (prop.scalar) {
                    if (property_name == "camera_projection") x = x >= 0.5 ? 1.0 : 0.0;
                    set_animated_value(*prop.scalar, lt, x);
                    if (property_name == "camera_projection") {
                        for (Keyframe &key : prop.scalar->keyframes) {
                            if (std::abs(key.time - lt) > 1.0 / 240.0) continue;
                            key.easing = EasingType::Hold;
                            key.temporal_mode = TemporalInterpolationMode::Hold;
                            key.temporal_velocity_explicit = true;
                        }
                    }
                }
                timeline_->update();
                if (layers_) layers_->set_playhead(playhead_);
                if (canvas_) canvas_->update();
                on_title_modified(false);
            });

    connect(layers_, &LayerStack::property_temporal_mode_changed,
            this, [this](const std::string &lid, const std::string &property_name, int mode_value) {
                if (!title_) return;
                if (timeline_owner_locked(*title_, lid)) return;
                const double owner_in = timeline_owner_in_time(*title_, lid);
                const double owner_out = timeline_owner_out_time(*title_, lid);
                const double lt = std::clamp(playhead_ - owner_in, 0.0,
                    std::max(0.0, owner_out - owner_in));
                auto prop = timeline_property_for_owner(*title_, lid, property_name);
                if (!prop || prop.is_hold_only()) return;
                {
                    const double tolerance = std::max(1e-6, obs_frame_duration() * 0.5 + 1e-6);
                    for (size_t index = 0; index < prop.keyframe_count(); ++index) {
                        if (std::abs(prop.keyframe_time(index) - lt) > tolerance) continue;
                        const auto mode = static_cast<TemporalInterpolationMode>(std::clamp(
                            mode_value, (int)TemporalInterpolationMode::Linear,
                            (int)TemporalInterpolationMode::ManualBezier));
                        prop.apply_temporal_mode(index, mode);
                        timeline_->update();
                        layers_->set_playhead(playhead_);
                        on_title_modified();
                        return;
                    }
                }
            });

    connect(layers_, &LayerStack::property_easy_ease_requested,
            this, [this](const std::string &lid, const std::string &property_name,
                         bool ease_in, bool ease_out) {
                if (!title_ || timeline_owner_locked(*title_, lid)) return;
                const double owner_in = timeline_owner_in_time(*title_, lid);
                const double owner_out = timeline_owner_out_time(*title_, lid);
                const double lt = std::clamp(playhead_ - owner_in, 0.0,
                    std::max(0.0, owner_out - owner_in));
                auto prop = timeline_property_for_owner(*title_, lid, property_name);
                if (!prop || prop.is_hold_only()) return;
                const double tolerance = std::max(1e-6, obs_frame_duration() * 0.5 + 1e-6);
                for (size_t index = 0; index < prop.keyframe_count(); ++index) {
                    if (std::abs(prop.keyframe_time(index) - lt) > tolerance) continue;
                    prop.apply_easy_ease(index, ease_in, ease_out);
                    timeline_->update();
                    layers_->set_playhead(playhead_);
                    on_title_modified();
                    return;
                }
            });

    connect(layers_, &LayerStack::property_velocity_requested,
            this, [this](const std::string &lid, const std::string &property_name) {
                if (!title_ || !timeline_ || timeline_owner_locked(*title_, lid)) return;
                auto prop = timeline_property_for_owner(*title_, lid, property_name);
                if (!prop || prop.is_hold_only()) return;
                const double owner_in = timeline_owner_in_time(*title_, lid);
                const double owner_out = timeline_owner_out_time(*title_, lid);
                const double lt = std::clamp(playhead_ - owner_in, 0.0,
                    std::max(0.0, owner_out - owner_in));
                timeline_->show_property_velocity_dialog(lid, property_name, lt);
                if (layers_) layers_->set_playhead(playhead_);
            });

    connect(layers_, &LayerStack::layer_expand_changed,
            this, [this](const std::string &lid, bool expanded) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->properties_expanded = expanded;
                    layers_->refresh();
                    timeline_->set_title(title_);
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::camera_expand_changed,
            this, [this](const std::string &owner_id, bool expanded) {
                if (!title_) return;
                if (owner_id == kTitleCameraSwitchOwner) {
                    title_->camera_switches_expanded = expanded;
                } else if (TitleCamera *camera = timeline_camera_from_owner(
                               *title_, owner_id)) {
                    camera->timeline_expanded = expanded;
                } else if (TitleLight *light = timeline_light_from_owner(
                               *title_, owner_id)) {
                    light->timeline_expanded = expanded;
                } else {
                    return;
                }
                layers_->refresh();
                timeline_->set_title(title_);
                /* Disclosure state is persisted, but it does not alter pixels
                 * and therefore must not invalidate prerender/cache content. */
                on_title_modified(false);
            });

    connect(layers_, &LayerStack::property_channels_expanded_changed,
            this, [this](const std::string &owner_id,
                         const std::string &property_name, bool expanded) {
                if (!title_) return;
                const std::string key = timeline_property_channel_disclosure_key(
                    owner_id, property_name);
                if (expanded)
                    title_->expanded_property_channels.insert(key);
                else
                    title_->expanded_property_channels.erase(key);
                layers_->refresh();
                timeline_->set_title(title_);
                /* Disclosure is editor state only and does not invalidate
                 * rendered pixels, proxies, or prerender cache entries. */
                on_title_modified(false);
            });

    connect(layers_, &LayerStack::group_expansion_state_changed,
            this, [this](const std::string &lid, int state) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid);
                    layer && (layer->type == LayerType::Group ||
                              layer->type == LayerType::Video)) {
                    state = std::clamp(state, 0, 2);
                    layer->properties_expanded = state >= 1;
                    layer->group_collapsed = state < 2;
                    layers_->refresh();
                    timeline_->set_title(title_);
                    synchronize_layer_selection({lid});
                    on_title_modified(false);
                }
            });
    connect(layers_, &LayerStack::group_layers_requested,
            this, &TitleEditor::group_selected_layers);
    connect(layers_, &LayerStack::ungroup_layers_requested,
            this, &TitleEditor::ungroup_selected_layers);
    connect(layers_, &LayerStack::add_to_group_requested,
            this, &TitleEditor::add_selected_layers_to_group);
    connect(layers_, &LayerStack::remove_from_group_requested,
            this, &TitleEditor::remove_selected_layers_from_group);
    connect(layers_, &LayerStack::layer_rows_dropped,
            this, &TitleEditor::drop_layer_list_rows);

    connect(layers_, &LayerStack::layer_parent_changed,
            this, [this](const std::string &lid, const std::string &parent_id) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    std::string next_parent_id = parent_id;
                    if (next_parent_id == layer->id ||
                        editor_parenting_would_cycle(title_, layer->id,
                                                     next_parent_id))
                        next_parent_id.clear();
                    if (next_parent_id == layer->transform_parent_id)
                        return;
                    if (!editor_reparent_layer_with_parent_bind(
                            title_, *layer, layer->parent_id, next_parent_id,
                            playhead_)) {
                        FXM_LOG_WARNING("Grouping", QStringLiteral(
                            "Transform-parent change skipped layer=%1 reason=bind-reparent-failed")
                            .arg(QString::fromStdString(layer->id)));
                        return;
                    }
                    layers_->refresh();
                    force_next_title_visual_update();
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_matte_visibility_changed,
            this, [this](const std::string &lid, MatteVisibilityMode mode) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->matte_visibility_mode = mode;
                    layer->visible = mode != MatteVisibilityMode::HiddenInactive;
                    force_next_title_visual_update();
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_mask_changed,
            this, [this](const std::string &lid, const std::string &mask_source_id, MaskMode mask_mode) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    const std::string previous_source_id = layer->mask_source_id;
                    const MaskMode previous_mode = layer->mask_mode;
                    bool source_was_already_matte =
                        !mask_source_id.empty() &&
                        layer->mask_source_id == mask_source_id &&
                        layer->mask_mode != MaskMode::None;
                    if (!source_was_already_matte && !mask_source_id.empty()) {
                        for (const auto &candidate : title_->layers) {
                            if (!candidate || candidate->id == lid)
                                continue;
                            if (candidate->mask_source_id == mask_source_id &&
                                candidate->mask_mode != MaskMode::None) {
                                source_was_already_matte = true;
                                break;
                            }
                        }
                    }

                    layer->mask_source_id = mask_source_id;
                    layer->mask_mode = mask_source_id.empty() ? MaskMode::None : mask_mode;

                    /* Development Version 067 promoted a clipping source to
                     * VisibleAndMatte.  That state then leaked into Alpha/Luma
                     * when the type toggle moved on.  Normalize the old source
                     * as soon as the last clipping relationship is removed. */
                    if (!previous_source_id.empty() &&
                        mask_mode_is_clipping(previous_mode) &&
                        (previous_source_id != mask_source_id ||
                         !mask_mode_is_clipping(layer->mask_mode))) {
                        bool still_used_for_clipping = false;
                        for (const auto &candidate : title_->layers) {
                            if (!candidate || candidate->id == lid)
                                continue;
                            if (candidate->mask_source_id == previous_source_id &&
                                mask_mode_is_clipping(candidate->mask_mode)) {
                                still_used_for_clipping = true;
                                break;
                            }
                        }
                        if (!still_used_for_clipping) {
                            if (auto previous_source = title_->find_layer(previous_source_id)) {
                                if (previous_source->matte_visibility_mode ==
                                    MatteVisibilityMode::VisibleAndMatte) {
                                    previous_source->matte_visibility_mode =
                                        MatteVisibilityMode::MatteOnly;
                                    previous_source->visible = true;
                                }
                            }
                        }
                    }

                    if (!mask_source_id.empty()) {
                        if (auto source = title_->find_layer(mask_source_id)) {
                            /* Clipping visibility is now resolved by the GPU
                             * compositor as a base+clipped pair.  Keep the
                             * source in its ordinary matte state instead of
                             * mutating it to VisibleAndMatte. */
                            if (!source_was_already_matte) {
                                source->matte_visibility_mode = source->visible
                                    ? MatteVisibilityMode::MatteOnly
                                    : MatteVisibilityMode::HiddenInactive;
                            }
                        }
                    }
                    layers_->refresh();
                    force_next_title_visual_update();
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_blend_mode_changed,
            this, [this](const std::string &lid, EffectBlendMode blend_mode) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    layer->blend_mode = blend_mode;
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_effects_enabled_changed,
            this, [this](const std::string &lid, bool enabled) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    const double local_time = std::clamp(playhead_ - layer->in_time,
                                                         0.0,
                                                         std::max(0.0, layer->out_time - layer->in_time));
                    for (auto &effect : layer->effects) {
                        effect.enabled = enabled;
                        set_animated_value(effect.enabled_prop, local_time,
                                           enabled ? 1.0 : 0.0);
                    }
                    if (effects_panel_ && sel_layer_id_ == lid) {
                        effects_panel_->set_title(title_);
                        effects_panel_->set_layer(layer, playhead_);
                    }
                    force_next_title_visual_update();
                    on_title_modified();
                }
            });

    connect(layers_, &LayerStack::layer_dimension_mode_changed,
            this, [this](const std::string &lid, LayerDimensionMode mode) {
                if (!title_) return;
                auto layer = title_->find_layer(lid);
                if (!layer || layer->locked || layer_type_is_audio(layer->type) ||
                    layer->type == LayerType::Adjustment || layer->dimension_mode == mode)
                    return;
                set_layer_dimension_mode_preserving_position_track(*layer, mode);
                if (props_ && sel_layer_id_ == lid)
                    props_->set_layer(layer, playhead_);
                if (timeline_) timeline_->set_title(title_);
                if (layers_) {
                    QTimer::singleShot(0, this, [this, lid]() {
                        if (!layers_) return;
                        layers_->refresh();
                        synchronize_layer_selection({lid}, false);
                    });
                }
                force_next_title_visual_update();
                on_title_modified(true);
            });

    connect(layers_, &LayerStack::layer_ui_color_changed,
            this, [this](const std::string &lid, bool enabled,
                         uint32_t argb) {
                if (!title_) return;
                auto layer = title_->find_layer(lid);
                if (!layer) return;
                const uint32_t opaque_argb = argb | 0xFF000000u;
                if (layer->custom_ui_color_enabled == enabled &&
                    (!enabled || layer->custom_ui_color == opaque_argb))
                    return;
                layer->custom_ui_color_enabled = enabled;
                if (enabled)
                    layer->custom_ui_color = opaque_argb;
                force_next_title_visual_update();
                on_title_modified(true);
                /* Let the originating QMenu finish its triggered/clicked
                 * dispatch before rebuilding and deleting the row widgets. */
                if (layers_) {
                    QTimer::singleShot(0, layers_, [this]() {
                        if (layers_)
                            layers_->refresh();
                    });
                }
                if (timeline_)
                    timeline_->set_title(title_);
                if (canvas_)
                    canvas_->update();
            });

    connect(layers_, &LayerStack::layer_name_changed,
            this, [this](const std::string &lid, const std::string &name) {
                if (!title_) return;
                if (auto layer = title_->find_layer(lid)) {
                    const std::string next_name = unique_layer_name(name.empty() ? editor_text_std("OBSTitles.Layer") : name,
                                                                    {lid});
                    if (layer->name == next_name) return;
                    layer->name = next_name;
                    timeline_->set_title(title_);
                    on_title_modified();
                    QTimer::singleShot(0, layers_, [this]() {
                        if (layers_) layers_->refresh();
                    });
                }
            });

    connect(timeline_, &TimelineWidget::playhead_changed,
            this, &TitleEditor::on_playhead_changed);
    connect(timeline_, &TimelineWidget::layer_selected,
            this, &TitleEditor::on_layer_selected);
    connect(timeline_, &TimelineWidget::layers_selected,
            this, [this](const std::vector<std::string> &ids) {
                synchronize_layer_selection(ids);
            });
    connect(timeline_, &TimelineWidget::layer_context_menu_requested,
            this, [this](const QPoint &global_pos) {
                if (canvas_)
                    canvas_->show_selected_layers_context_menu(global_pos);
            });
    connect(timeline_, &TimelineWidget::effect_preset_dropped,
            this, &TitleEditor::apply_effect_preset_to_layer);
    connect(timeline_, &TimelineWidget::audio_effect_dropped,
            this, [this](int type, const std::string &layer_id) {
                if (!title_ || !effects_panel_) return;
                auto layer = title_->find_layer(layer_id);
                if (!layer || layer->locked) return;
                effects_panel_->set_title(title_);
                effects_panel_->set_layer(layer, playhead_);
                if (effects_panel_->add_audio_effect(static_cast<AudioEffectType>(type))) {
                    synchronize_layer_selection({layer_id});
                    on_title_modified();
                }
            });
    connect(timeline_, &TimelineWidget::transition_preset_dropped,
            this, &TitleEditor::apply_transition_preset_to_layer);
    connect(timeline_, &TimelineWidget::transition_edit_requested,
            this, &TitleEditor::edit_layer_transition);
    connect(timeline_, &TimelineWidget::transition_modified,
            this, [this]() {
                if (title_ && !sel_layer_id_.empty()) {
                    if (auto layer = title_->find_layer(sel_layer_id_)) {
                        synchronize_text_transition_animators(
                            layer->transitions, layer->text_animators,
                            layer->in_time, layer->out_time, nullptr, true);
                    }
                }
                force_next_title_visual_update();
                on_title_modified();
                if (timeline_) timeline_->update();
            });
    connect(timeline_, &TimelineWidget::keyframe_easing_changed,
            this, [this]() {
                if (layers_) layers_->set_playhead(playhead_);
                if (title_props_ && title_ && title_->graphic_type == TitleGraphicType::Stinger)
                    title_props_->set_title(title_);
                on_title_modified();
            });
    connect(timeline_, &TimelineWidget::keyframe_structure_changed,
            this, [this]() {
                /* Timeline and layer-list disclosure rows are two views of the
                 * same authored tracks. Rebuild the list immediately whenever
                 * a timeline operation creates the first or removes the last
                 * keyframe of a property. */
                if (layers_) {
                    layers_->refresh();
                    layers_->set_playhead(playhead_);
                }
                if (timeline_) timeline_->update();
            });
    connect(timeline_, &TimelineWidget::layer_timing_changed,
            this, [this](bool commit_undo) {
                if (!title_) return;
                force_next_title_visual_update();
                if (commit_undo) {
                    on_title_modified(true);
                    publish_editor_audio_runtime_state();
                    sync_editor_audio_preview(false);
                } else {
                    /* Live strip trim should feel instantaneous. Avoid full cache
                     * invalidation/audio runtime rebuild on every mouse move; the
                     * commit path above performs the authoritative sync. */
                    set_dirty(true);
                    if (canvas_)
                        canvas_->refresh_preview();
                }
                if (props_) {
                    const auto selected_ids = selected_layer_ids_for_operation();
                    auto selected = selected_ids.empty() ? nullptr : title_->find_layer(selected_ids.front());
                    if (selected) {
                        if (commit_undo)
                            props_->set_layer(selected, playhead_);
                        else
                            props_->refresh_media_range_controls();
                    }
                }
                if (layers_) {
                    if (commit_undo)
                        layers_->refresh();
                    else
                        layers_->update();
                    layers_->set_playhead(playhead_);
                }
            });

    connect(timeline_, &TimelineWidget::audio_layer_property_changed,
            this, [this](bool commit_undo) {
                if (!title_) return;
                set_dirty(true);
                publish_editor_audio_runtime_state();
                sync_editor_audio_preview(false);
                if (props_) {
                    const auto selected_ids = selected_layer_ids_for_operation();
                    auto selected = selected_ids.empty() ? nullptr : title_->find_layer(selected_ids.front());
                    if (selected && selected->type == LayerType::Audio)
                        props_->set_layer(selected, playhead_);
                }
                if (commit_undo) {
                    push_undo_snapshot();
                    save_live_edit();
                }
            });

    connect(props_, &PropertiesPanel::undo_requested, this, [this]() {
        if (undo_index_ > 0)
            restore_undo_snapshot(undo_index_ - 1);
    });
    connect(props_, &PropertiesPanel::redo_requested, this, [this]() {
        if (undo_index_ + 1 < (int)undo_stack_.size())
            restore_undo_snapshot(undo_index_ + 1);
    });
    connect(props_, &PropertiesPanel::keyframe_navigation_requested,
            this, &TitleEditor::on_playhead_changed);

    connect(props_, &PropertiesPanel::property_changed,
            this, [this](bool commit_undo) {
                if (updating_layer_panels_)
                    return;
                // Every PropertiesPanel notification represents a real model edit.
                // Live drag steps invalidate rendering without adding snapshots; the
                // final committed step records the canonical post-state exactly once.
                force_next_title_visual_update();
                on_title_modified(commit_undo);
                if (commit_undo) {
                    text_property_transaction_active_ = false;
                    text_property_transaction_layer_id_.clear();
                }
                if (layers_) layers_->refresh();
            });
    connect(props_, &PropertiesPanel::audio_property_changed,
            this, [this](bool commit_undo) {
                if (updating_layer_panels_ || !title_)
                    return;
                /* Audio mix/transport edits are consumed directly from the shared
                 * title model by SourceAudioRuntime.  Do not enter the visual
                 * prerender/cache invalidation path for every spin-box step. */
                set_dirty(true);
                publish_editor_audio_runtime_state();
                if (timeline_) timeline_->update();
                if (layers_) layers_->update();
                sync_editor_audio_preview(false);
                if (commit_undo) {
                    push_undo_snapshot();
                    save_live_edit();
                }
            });

    connect(effects_panel_, &EffectsPanel::audio_property_changed,
            this, [this](bool commit_undo) {
                if (updating_layer_panels_ || !title_) return;
                set_dirty(true);
                publish_editor_audio_runtime_state();
                sync_editor_audio_preview(false);
                if (effects_panel_) effects_panel_->update();
                if (commit_undo) { push_undo_snapshot(); save_live_edit(); }
            });

    connect(props_, &PropertiesPanel::live_visual_changed,
            this, [this]() {
                if (updating_layer_panels_)
                    return;
                force_next_title_visual_update();
                if (title_)
                    set_dirty(true);
                schedule_cache_invalidation();
                if (canvas_)
                    canvas_->refresh_preview();
            });
    connect(props_, &PropertiesPanel::runtime_visual_changed,
            this, [this]() {
                if (updating_layer_panels_)
                    return;
                if (canvas_)
                    canvas_->refresh_runtime_dynamic_content();
            });
    connect(props_, &PropertiesPanel::text_property_change_started,
            this, [this](const std::string &layer_id) {
                begin_text_property_transaction(layer_id);
            });
    connect(props_, &PropertiesPanel::text_char_format_changed,
            this, [this](const std::string &layer_id, const RichTextCharFormat &format, uint32_t mask) {
                if (canvas_) canvas_->apply_active_text_char_format(layer_id, format, mask);
            });
    connect(props_, &PropertiesPanel::gradient_editor_active_changed,
            this, [this](bool active) {
                handle_gradient_editor_tool_request(active);
            });
    connect(props_, &PropertiesPanel::recent_colors_changed,
            this, [this]() {
                refresh_color_swatches_panel();
                persist_recent_colors();
            });
    connect(props_, &PropertiesPanel::color_library_add_requested,
            this, &TitleEditor::show_add_color_to_library_dialog);
    connect(props_, &PropertiesPanel::asset_overrides_requested,
            this, &TitleEditor::open_asset_overrides_dialog);
    connect(props_, &PropertiesPanel::color_picker_tool_requested,
            this, [this]() {
                reopen_color_tab_after_canvas_pick_ = true;
                current_editor_tool_ = EditorTool::ColorPicker;
                if (canvas_) canvas_->set_color_picker_tool_active();
            });
    connect(title_props_, &TitlePropertiesPanel::title_changed,
            this, [this, refresh_canvas_background_control](bool push_undo_snapshot) {
                if (!title_) return;
                playhead_ = std::clamp(playhead_, 0.0, title_->duration);
                on_title_modified(push_undo_snapshot);
                if (layers_) layers_->refresh();
                timeline_->set_title(title_);
                on_playhead_changed(playhead_);
                refresh_canvas_background_control();
            });
    connect(title_props_, &TitlePropertiesPanel::document_format_changed,
            this, [this](bool canvas_size_changed) {
                if (!title_) return;
                fxm::set_document_frame_rate(title_->frame_rate);
                reset_playback_timer_cadence();
                if (canvas_size_changed && canvas_)
                    canvas_->set_title(title_, true, false);
                if (timeline_)
                    timeline_->set_title(title_);
            });
    connect(title_props_, &TitlePropertiesPanel::keyframe_navigation_requested,
            this, &TitleEditor::on_playhead_changed);
    connect(title_props_, &TitlePropertiesPanel::stinger_structure_changed,
            this, [this, refresh_canvas_background_control]() {
                if (!title_ || !layers_ || !timeline_ || !canvas_)
                    return;
                const bool show_transition_inputs =
                    title_->graphic_type == TitleGraphicType::Stinger &&
                    title_->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation;
                std::vector<std::string> next_selection =
                    synchronized_layer_selection_;
                if (!show_transition_inputs && next_selection.size() == 1) {
                    const auto selected = title_->find_layer(next_selection.front());
                    if (selected && layer_type_is_transition_input(selected->type))
                        next_selection.clear();
                }
                layers_->refresh();
                timeline_->set_title(title_);
                synchronize_layer_selection(next_selection);
                refresh_canvas_background_control();
            });
    connect(title_props_, &TitlePropertiesPanel::stinger_editor_preview_changed,
            this, [this]() {
                force_next_title_visual_update();
                if (canvas_)
                    canvas_->update();
            });
    connect(layers_, &LayerStack::layer_order_changed,
            this, [this]() {
                layers_->refresh();
                canvas_->refresh_preview();
                timeline_->set_title(title_);
                on_title_modified();
            });

    connect(layers_, &LayerStack::property_channel_value_changed,
            this, [this](const std::string &lid,
                         const std::string &property_name,
                         int channel, double value) {
                if (!title_) return;
                if (timeline_owner_locked(*title_, lid)) return;
                auto prop = timeline_property_for_owner(*title_, lid, property_name);
                if (!prop || channel < 0 || channel >= prop.graph_channel_count())
                    return;
                const double owner_in = timeline_owner_in_time(*title_, lid);
                const double owner_out = timeline_owner_out_time(*title_, lid);
                const double lt = std::clamp(playhead_ - owner_in, 0.0,
                    std::max(0.0, owner_out - owner_in));
                if (property_name == "scale") value /= 100.0;

                if (prop.vector3) {
                    Vec3Value next = prop.vector3->evaluate(lt);
                    if (channel == 0) next.x = value;
                    else if (channel == 1) next.y = value;
                    else next.z = value;
                    prop.activate_vector3();
                    set_animated_value(*prop.vector3, lt, next);
                } else if (prop.vector) {
                    Vec2Value next = prop.vector->evaluate(lt);
                    if (channel == 0) next.x = value;
                    else if (channel == 1) next.y = value;
                    else next.z = value;
                    set_animated_value(*prop.vector, lt, next);
                } else if (prop.is_scalar_group()) {
                    if (channel >= static_cast<int>(prop.scalar_group.size())) return;
                    if (AnimatedProperty *selected = prop.scalar_group[
                            static_cast<size_t>(channel)])
                        set_animated_value(*selected, lt, value);
                } else {
                    return;
                }
                layers_->update();
                layers_->set_playhead(playhead_);
                timeline_->set_title(title_);
                on_title_modified();
            });
