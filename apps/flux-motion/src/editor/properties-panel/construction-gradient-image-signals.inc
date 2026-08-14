    cmb_kerning_mode_ = mk_combo({fxm_tr("OBSTitles.KerningMetrics"), fxm_tr("OBSTitles.KerningOptical"), fxm_tr("OBSTitles.KerningManual")}, {0, 1, 2});
    spn_kerning_value_ = mk_dspin(-100.0, 500.0, 1.0);
    spn_kerning_value_->setSuffix(" px");
    spn_kerning_value_->setToolTip(fxm_tr("OBSTitles.ManualKerningTooltip"));
    spn_text_leading_ = mk_dspin(-200.0, 500.0, 1.0);
    spn_text_leading_->setSuffix(" px");
    spn_text_leading_->setToolTip(fxm_tr("OBSTitles.LeadingTooltip"));
    spn_char_tracking_ = mk_dspin(-100.0, 500.0, 1.0);
    spn_char_tracking_->setSuffix(" px");
    spn_char_tracking_->setToolTip(fxm_tr("OBSTitles.TrackingTooltip"));
    spn_char_scale_x_ = mk_dspin(10.0, 500.0, 1.0);
    spn_char_scale_x_->setSuffix("%");
    spn_char_scale_y_ = mk_dspin(10.0, 500.0, 1.0);
    spn_char_scale_y_->setSuffix("%");
    spn_baseline_shift_ = mk_dspin(-500.0, 500.0, 1.0);
    spn_baseline_shift_->setSuffix(" px");
    btn_text_color_ = new QPushButton(inner);
    btn_text_color_->setFixedHeight(20);
    btn_kf_font_size_ = mk_kf_button("Toggle Size keyframe");
    btn_kf_char_scale_x_ = mk_kf_button("Toggle H Scale keyframe");
    btn_kf_char_scale_y_ = mk_kf_button("Toggle V Scale keyframe");
    btn_kf_char_tracking_ = mk_kf_button("Toggle Tracking keyframe");
    btn_kf_baseline_shift_ = mk_kf_button("Toggle Baseline keyframe");
    btn_kf_text_color_ = mk_kf_button(fxm_tr("OBSTitles.ToggleTextColorKeyframe"));

    char_grid->addWidget(grid_label(fxm_tr("OBSTitles.TextLabel"), text_box_), 0, 0);
    char_grid->addWidget(txt_content_, 0, 1, 1, 3);
    char_grid->addWidget(btn_text_external_binding_, 0, 4, Qt::AlignTop);
    // Font family and style are the primary Character controls. Keep them
    // label-free and span the complete inspector width so long family/style
    // names do not become truncated by a redundant label column.
    cmb_font_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cmb_font_style_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    char_grid->addWidget(cmb_font_, 1, 0, 1, 5);
    char_grid->addWidget(cmb_font_style_, 2, 0, 1, 5);
    add_grid_field(char_grid, 3, 0, "Size", with_kf(spn_size_, btn_kf_font_size_));
    add_grid_field(char_grid, 3, 1, "Leading", spn_text_leading_);
    add_grid_field(char_grid, 4, 0, "Kerning", cmb_kerning_mode_);
    add_grid_field(char_grid, 4, 1, "Value", spn_kerning_value_);
    add_grid_field(char_grid, 5, 0, "H Scale", with_kf(spn_char_scale_x_, btn_kf_char_scale_x_));
    add_grid_field(char_grid, 5, 1, "V Scale", with_kf(spn_char_scale_y_, btn_kf_char_scale_y_));
    add_grid_field(char_grid, 6, 0, "Tracking", with_kf(spn_char_tracking_, btn_kf_char_tracking_));
    add_grid_field(char_grid, 6, 1, "Baseline", with_kf(spn_baseline_shift_, btn_kf_baseline_shift_));
    row_text_color_ = with_kf(btn_text_color_, btn_kf_text_color_);
    add_full_width_field(char_grid, 7, "Fill Color", row_text_color_);
    add_full_width_field(char_grid, 8, "Preset", cmb_clock_preset_);
    fxm_add_panel_section(vl, text_box_);

    /* The animator editor is a normal properties-panel section. It writes the
     * same AnimatedProperty instances exposed by the layer timeline. */
    build_text_animator_section(inner, vl);

    /* ── Type Options ── */
    type_options_box_ = new QGroupBox("Type", inner);
    type_options_box_->setObjectName(QStringLiteral("FluxMotionTextTypePanel"));
    type_options_box_->setStyleSheet(QStringLiteral(
        "QGroupBox#FluxMotionTextTypePanel{color:%1;background:%2;border:1px solid %3;"
        "border-radius:2px;margin-top:16px;padding:0;font-size:11px;}"
        "QGroupBox#FluxMotionTextTypePanel::title{subcontrol-origin:margin;left:14px;top:2px;padding:0 4px;background:%2;}")
        .arg(panel_text_name, section_bg_name, border_name));
    auto *type_grid = new QGridLayout(type_options_box_);
    type_grid->setContentsMargins(5, 4, 5, 5);
    type_grid->setHorizontalSpacing(4);
    type_grid->setVerticalSpacing(4);
    chk_bold_ = mk_type_icon_button("text-style-bold.svg", fxm_tr("OBSTitles.Bold"));
    chk_italic_ = mk_type_icon_button("text-style-italic.svg", fxm_tr("OBSTitles.Italic"));
    chk_font_kerning_ = mk_type_button("K", fxm_tr("OBSTitles.Kerning"));
    chk_font_kerning_->setToolTip(fxm_tr("OBSTitles.KerningTooltip"));
    btn_all_caps_ = mk_type_icon_button("text-style-allcaps.svg", "All Caps");
    btn_small_caps_ = mk_type_icon_button("text-style-smallcaps.svg", "Small Caps");
    btn_superscript_ = mk_type_button("x²", "Superscript");
    btn_subscript_ = mk_type_button("x₂", "Subscript");
    btn_underline_ = mk_type_button("U", "Underline");
    btn_strikethrough_ = mk_type_button("S", "Strikethrough");
    btn_ligatures_ = mk_type_button("fi", "Ligatures");
    btn_stylistic_alternates_ = mk_type_button("Sw", "Stylistic Alternates");
    btn_fractions_ = mk_type_button("½", "Fractions");
    btn_opentype_features_ = mk_type_button("OT", "OpenType Features");
    QList<QToolButton *> type_buttons{chk_bold_, chk_italic_, btn_all_caps_, btn_small_caps_, btn_superscript_,
                                      btn_subscript_, btn_underline_, btn_strikethrough_, btn_ligatures_, btn_stylistic_alternates_,
                                      btn_fractions_, btn_opentype_features_, chk_font_kerning_};
    for (int i = 0; i < type_buttons.size(); ++i) type_grid->addWidget(type_buttons[i], i / 5, i % 5);
    type_grid->setColumnStretch(5, 1);
    fxm_add_panel_section(vl, type_options_box_);

    /* ── Chart ──
     * Chart authoring uses the same reorderable/collapsible inspector sections
     * as every other native layer. The grouping follows user intent while all
     * controls continue to edit the existing Layer properties directly. */
    auto prepare_chart_section = [&](QGroupBox *section, const QString &key,
                                     bool default_expanded) {
        section->setObjectName(QStringLiteral("FluxMotionChart%1Panel").arg(key));
        section->setStyleSheet(section_style);
        section->setProperty("fxmPanelPersistenceKey", QStringLiteral("chart_%1").arg(key));
        section->setProperty("fxmPanelDefaultExpanded", default_expanded);
    };
    auto make_section_layout = [](QWidget *section) {
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(5, 4, 5, 5);
        layout->setSpacing(5);
        return layout;
    };

    chart_box_ = new QGroupBox(fxm_tr("OBSTitles.Chart"), inner);
    prepare_chart_section(chart_box_, QStringLiteral("chart"), true);
    chart_box_->setProperty("fxmPanelDefaultsDisabled", true);
    auto *chart_layout = make_section_layout(chart_box_);
    auto *chart_form = new QFormLayout();
    style_form(chart_form);
    chart_layout->addLayout(chart_form);
    cmb_chart_type_ = mk_combo(
        {fxm_tr("OBSTitles.ChartBar"), fxm_tr("OBSTitles.ChartLine"),
         fxm_tr("OBSTitles.ChartArea"), fxm_tr("OBSTitles.ChartPie"),
         fxm_tr("OBSTitles.ChartDonut"), fxm_tr("OBSTitles.ChartScatter")},
        {(int)ChartType::Bar, (int)ChartType::Line, (int)ChartType::Area,
         (int)ChartType::Pie, (int)ChartType::Donut, (int)ChartType::Scatter});
    cmb_chart_orientation_ = mk_combo(
        {fxm_tr("OBSTitles.ChartVertical"), fxm_tr("OBSTitles.ChartHorizontal")},
        {0, 1});
    add_form_row(chart_form, fxm_tr("OBSTitles.ChartType"), cmb_chart_type_);
    add_form_row(chart_form, fxm_tr("OBSTitles.ChartOrientation"), cmb_chart_orientation_);

    tbl_chart_series_ = new QTableWidget(0, 7, chart_box_);
    tbl_chart_series_->setHorizontalHeaderLabels({
        fxm_tr("OBSTitles.ChartVisible"), fxm_tr("OBSTitles.ChartSeriesName"),
        fxm_tr("OBSTitles.ChartValueField"), fxm_tr("OBSTitles.ChartColor"),
        fxm_tr("OBSTitles.ChartFill"), fxm_tr("OBSTitles.ChartOutline"),
        fxm_tr("OBSTitles.ChartLineWidth")});
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    tbl_chart_series_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    tbl_chart_series_->verticalHeader()->setVisible(false);
    tbl_chart_series_->setMinimumHeight(86);
    tbl_chart_series_->setMaximumHeight(150);
    row_chart_series_ = new QWidget(chart_box_);
    auto *series_layout = new QVBoxLayout(row_chart_series_);
    series_layout->setContentsMargins(0, 0, 0, 0);
    series_layout->setSpacing(3);
    series_layout->addWidget(tbl_chart_series_);
    auto make_chart_tool = [&](const char *icon, const QString &tip) {
        auto *button = new QToolButton(chart_box_);
        button->setIcon(obs_icon(icon));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFixedSize(22, 22);
        return button;
    };
    auto *series_buttons = new QHBoxLayout();
    btn_chart_add_series_ = make_chart_tool("add.svg", fxm_tr("OBSTitles.ChartAddSeries"));
    btn_chart_remove_series_ = make_chart_tool("delete.svg", fxm_tr("OBSTitles.ChartRemoveSeries"));
    series_buttons->addWidget(btn_chart_add_series_);
    series_buttons->addWidget(btn_chart_remove_series_);
    series_buttons->addStretch(1);
    series_layout->addLayout(series_buttons);
    chart_layout->addWidget(row_chart_series_);
    fxm_add_panel_section(vl, chart_box_, fxm_tr("OBSTitles.Chart"));

    chart_data_box_ = new QGroupBox(fxm_tr("OBSTitles.ChartData"), inner);
    prepare_chart_section(chart_data_box_, QStringLiteral("data"), true);
    chart_data_box_->setProperty("fxmPanelDefaultsDisabled", true);
    auto *chart_data_layout = make_section_layout(chart_data_box_);
    auto *chart_data_form = new QFormLayout();
    style_form(chart_data_form);
    chart_data_layout->addLayout(chart_data_form);
    cmb_chart_data_mode_ = mk_combo(
        {fxm_tr("OBSTitles.ChartStaticData"),
         fxm_tr("OBSTitles.ChartExternalData")},
        {(int)ChartDataMode::StaticData, (int)ChartDataMode::ExternalData});
    cmb_chart_source_ = new QComboBox(chart_data_box_);
    cmb_chart_table_ = new QComboBox(chart_data_box_);
    cmb_chart_category_ = new QComboBox(chart_data_box_);
    cmb_chart_series_field_ = new QComboBox(chart_data_box_);
    cmb_chart_secondary_field_ = new QComboBox(chart_data_box_);
    cmb_chart_metadata_field_ = new QComboBox(chart_data_box_);
    cmb_chart_color_field_ = new QComboBox(chart_data_box_);
    for (QComboBox *combo : {cmb_chart_source_, cmb_chart_table_,
                             cmb_chart_category_, cmb_chart_series_field_,
                             cmb_chart_secondary_field_, cmb_chart_metadata_field_,
                             cmb_chart_color_field_}) {
        combo->setEditable(true);
        combo->setFixedHeight(20);
        combo->setStyleSheet(control_style);
    }
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartDataMode"), cmb_chart_data_mode_);
    auto *chart_source_row = new QWidget(chart_data_box_);
    row_chart_source_ = chart_source_row;
    auto *chart_source_layout = new QHBoxLayout(chart_source_row);
    chart_source_layout->setContentsMargins(0, 0, 0, 0);
    chart_source_layout->setSpacing(3);
    chart_source_layout->addWidget(cmb_chart_source_, 1);
    btn_chart_data_settings_ = new QToolButton(chart_source_row);
    btn_chart_data_settings_->setIcon(obs_icon("settings.svg"));
    btn_chart_data_settings_->setToolTip(fxm_tr("OBSTitles.ExternalDataSourceSettings"));
    btn_chart_data_settings_->setAutoRaise(true);
    btn_chart_data_settings_->setFixedSize(22, 20);
    chart_source_layout->addWidget(btn_chart_data_settings_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartDataSource"), chart_source_row);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartTable"), cmb_chart_table_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartCategoryField"), cmb_chart_category_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartSeriesField"), cmb_chart_series_field_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartSecondaryField"), cmb_chart_secondary_field_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartMetadataField"), cmb_chart_metadata_field_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartColorField"), cmb_chart_color_field_);
    spn_chart_start_row_ = new QSpinBox(chart_data_box_);
    spn_chart_maximum_rows_ = new QSpinBox(chart_data_box_);
    for (QSpinBox *spin : {spn_chart_start_row_, spn_chart_maximum_rows_}) {
        spin->setRange(0, 1000000);
        spin->setFixedHeight(20);
        spin->setStyleSheet(control_style);
    }
    spn_chart_maximum_rows_->setSpecialValueText(fxm_tr("OBSTitles.ChartAllRows"));
    chk_chart_ignore_empty_ = new FxmSwitch(fxm_tr("OBSTitles.ChartIgnoreEmptyRows"), chart_data_box_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartFirstRow"), spn_chart_start_row_);
    add_form_row(chart_data_form, fxm_tr("OBSTitles.ChartMaximumRows"), spn_chart_maximum_rows_);
    add_form_row(chart_data_form, QString(), chk_chart_ignore_empty_);

    tbl_chart_values_ = new QTableWidget(0, 7, chart_data_box_);
    tbl_chart_values_->setHorizontalHeaderLabels({
        fxm_tr("OBSTitles.ChartExpose"), fxm_tr("OBSTitles.ChartCategory"),
        fxm_tr("OBSTitles.ChartSeries"), fxm_tr("OBSTitles.ChartValue"),
        fxm_tr("OBSTitles.ChartSecondary"), fxm_tr("OBSTitles.ChartColor"),
        fxm_tr("OBSTitles.ChartMetadata")});
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    tbl_chart_values_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    tbl_chart_values_->verticalHeader()->setVisible(false);
    tbl_chart_values_->setMinimumHeight(100);
    tbl_chart_values_->setMaximumHeight(190);
    row_chart_values_ = new QWidget(chart_data_box_);
    auto *values_layout = new QVBoxLayout(row_chart_values_);
    values_layout->setContentsMargins(0, 0, 0, 0);
    values_layout->setSpacing(3);
    values_layout->addWidget(tbl_chart_values_);
    auto *value_buttons = new QHBoxLayout();
    btn_chart_add_value_ = make_chart_tool("add.svg", fxm_tr("OBSTitles.ChartAddValue"));
    btn_chart_remove_value_ = make_chart_tool("delete.svg", fxm_tr("OBSTitles.ChartRemoveValue"));
    value_buttons->addWidget(btn_chart_add_value_);
    value_buttons->addWidget(btn_chart_remove_value_);
    value_buttons->addStretch(1);
    values_layout->addLayout(value_buttons);
    chart_data_layout->addWidget(row_chart_values_);
    fxm_add_panel_section(vl, chart_data_box_, fxm_tr("OBSTitles.ChartData"));

    auto make_chart_color = [&](QWidget *parent) {
        auto *button = new QPushButton(parent);
        button->setFixedHeight(20);
        style_push_button(button);
        return button;
    };

    chart_axes_box_ = new QGroupBox(fxm_tr("OBSTitles.ChartAxesGrid"), inner);
    prepare_chart_section(chart_axes_box_, QStringLiteral("axes_grid"), false);
    auto *chart_axes_layout = make_section_layout(chart_axes_box_);
    chk_chart_axes_ = new FxmSwitch(fxm_tr("OBSTitles.ChartShowAxes"), chart_axes_box_);
    chk_chart_axis_labels_ = new FxmSwitch(fxm_tr("OBSTitles.ChartShowAxisLabels"), chart_axes_box_);
    chk_chart_grid_ = new FxmSwitch(fxm_tr("OBSTitles.ChartShowGrid"), chart_axes_box_);
    chart_axes_layout->addWidget(chk_chart_axes_);
    chart_axes_layout->addWidget(chk_chart_axis_labels_);
    chart_axes_layout->addWidget(chk_chart_grid_);
    auto *chart_axes_form = new QFormLayout();
    style_form(chart_axes_form);
    chart_axes_layout->addLayout(chart_axes_form);
    btn_chart_axis_color_ = make_chart_color(chart_axes_box_);
    btn_chart_grid_color_ = make_chart_color(chart_axes_box_);
    add_form_row(chart_axes_form, fxm_tr("OBSTitles.ChartAxisColor"), btn_chart_axis_color_);
    add_form_row(chart_axes_form, fxm_tr("OBSTitles.ChartGridColor"), btn_chart_grid_color_);
    fxm_add_panel_section(vl, chart_axes_box_, fxm_tr("OBSTitles.ChartAxesGrid"));

    chart_labels_box_ = new QGroupBox(fxm_tr("OBSTitles.ChartLabelsLegend"), inner);
    prepare_chart_section(chart_labels_box_, QStringLiteral("labels_legend"), false);
    auto *chart_labels_layout = make_section_layout(chart_labels_box_);
    chk_chart_data_labels_ = new FxmSwitch(fxm_tr("OBSTitles.ChartShowDataLabels"), chart_labels_box_);
    chk_chart_legend_ = new FxmSwitch(fxm_tr("OBSTitles.ChartShowLegend"), chart_labels_box_);
    chk_chart_thousands_ = new FxmSwitch(fxm_tr("OBSTitles.ChartThousandsSeparator"), chart_labels_box_);
    chart_labels_layout->addWidget(chk_chart_data_labels_);
    chart_labels_layout->addWidget(chk_chart_legend_);
    chart_labels_layout->addWidget(chk_chart_thousands_);
    auto *chart_labels_form = new QFormLayout();
    style_form(chart_labels_form);
    chart_labels_layout->addLayout(chart_labels_form);
    spn_chart_precision_ = new QSpinBox(chart_labels_box_);
    spn_chart_precision_->setRange(0, 12);
    spn_chart_precision_->setFixedHeight(20);
    spn_chart_precision_->setStyleSheet(control_style);
    edt_chart_font_family_ = new QLineEdit(chart_labels_box_);
    edt_chart_number_prefix_ = new QLineEdit(chart_labels_box_);
    edt_chart_number_suffix_ = new QLineEdit(chart_labels_box_);
    for (QLineEdit *edit : {edt_chart_font_family_, edt_chart_number_prefix_,
                            edt_chart_number_suffix_}) {
        edit->setFixedHeight(20);
        edit->setStyleSheet(control_style);
    }
    spn_chart_font_size_ = mk_dspin(6.0, 512.0, 1.0);
    btn_chart_label_color_ = make_chart_color(chart_labels_box_);
    add_form_row(chart_labels_form, fxm_tr("OBSTitles.ChartNumberPrefix"), edt_chart_number_prefix_);
    add_form_row(chart_labels_form, fxm_tr("OBSTitles.ChartNumberSuffix"), edt_chart_number_suffix_);
    add_form_row(chart_labels_form, fxm_tr("OBSTitles.ChartDecimalPlaces"), spn_chart_precision_);
    add_form_row(chart_labels_form, fxm_tr("OBSTitles.ChartFont"), edt_chart_font_family_);
    add_form_row(chart_labels_form, fxm_tr("OBSTitles.ChartFontSize"), spn_chart_font_size_);
    add_form_row(chart_labels_form, fxm_tr("OBSTitles.ChartTextColor"), btn_chart_label_color_);
    fxm_add_panel_section(vl, chart_labels_box_, fxm_tr("OBSTitles.ChartLabelsLegend"));

    chart_appearance_box_ = new QGroupBox(fxm_tr("OBSTitles.ChartAppearance"), inner);
    prepare_chart_section(chart_appearance_box_, QStringLiteral("appearance"), false);
    auto *chart_appearance_layout = make_section_layout(chart_appearance_box_);
    chk_chart_background_ = new FxmSwitch(fxm_tr("OBSTitles.ChartBackground"), chart_appearance_box_);
    chk_chart_fill_ = new FxmSwitch(fxm_tr("OBSTitles.ChartFill"), chart_appearance_box_);
    chk_chart_outline_ = new FxmSwitch(fxm_tr("OBSTitles.ChartOutline"), chart_appearance_box_);
    chart_appearance_layout->addWidget(chk_chart_background_);
    chart_appearance_layout->addWidget(chk_chart_fill_);
    chart_appearance_layout->addWidget(chk_chart_outline_);
    auto *chart_appearance_form = new QFormLayout();
    style_form(chart_appearance_form);
    chart_appearance_layout->addLayout(chart_appearance_form);
    spn_chart_line_thickness_ = mk_dspin(0.0, 256.0, 0.5);
    spn_chart_bar_width_ = mk_dspin(5.0, 100.0, 1.0);
    spn_chart_bar_width_->setSuffix("%");
    spn_chart_bar_spacing_ = mk_dspin(0.0, 90.0, 1.0);
    spn_chart_bar_spacing_->setSuffix("%");
    spn_chart_corner_radius_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_chart_padding_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_chart_margin_left_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_chart_margin_top_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_chart_margin_right_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_chart_margin_bottom_ = mk_dspin(0.0, 1000.0, 1.0);
    btn_chart_background_color_ = make_chart_color(chart_appearance_box_);
    cmb_chart_fill_style_ = mk_combo(
        {fxm_tr("OBSTitles.ChartFieldColors"), fxm_tr("OBSTitles.Gradient")},
        {0, 1});
    btn_chart_gradient_ = make_chart_color(chart_appearance_box_);
    btn_chart_gradient_->setToolTip(fxm_tr("OBSTitles.ChartEditGradientTooltip"));
    row_chart_line_thickness_ = spn_chart_line_thickness_;
    row_chart_bar_width_ = spn_chart_bar_width_;
    row_chart_bar_spacing_ = spn_chart_bar_spacing_;
    row_chart_corner_radius_ = spn_chart_corner_radius_;
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartLineWidth"), row_chart_line_thickness_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartBarWidth"), row_chart_bar_width_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartBarSpacing"), row_chart_bar_spacing_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartCornerRadius"), row_chart_corner_radius_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartBackgroundColor"), btn_chart_background_color_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartFillStyle"), cmb_chart_fill_style_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.Gradient"), btn_chart_gradient_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartPadding"), spn_chart_padding_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartLeftMargin"), spn_chart_margin_left_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartTopMargin"), spn_chart_margin_top_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartRightMargin"), spn_chart_margin_right_);
    add_form_row(chart_appearance_form, fxm_tr("OBSTitles.ChartBottomMargin"), spn_chart_margin_bottom_);
    fxm_add_panel_section(vl, chart_appearance_box_, fxm_tr("OBSTitles.ChartAppearance"));

    chart_animation_box_ = new QGroupBox(fxm_tr("OBSTitles.ChartAnimation"), inner);
    prepare_chart_section(chart_animation_box_, QStringLiteral("animation"), false);
    auto *chart_animation_layout = make_section_layout(chart_animation_box_);
    auto *chart_animation_form = new QFormLayout();
    style_form(chart_animation_form);
    chart_animation_layout->addLayout(chart_animation_form);
    spn_chart_transition_ = mk_dspin(0.0, 60.0, 0.05);
    spn_chart_transition_->setSuffix(" s");
    spn_chart_animation_ = mk_dspin(0.0, 100.0, 1.0);
    spn_chart_reveal_ = mk_dspin(0.0, 100.0, 1.0);
    spn_chart_value_transition_ = mk_dspin(0.0, 100.0, 1.0);
    spn_chart_label_opacity_ = mk_dspin(0.0, 100.0, 1.0);
    spn_chart_element_opacity_ = mk_dspin(0.0, 100.0, 1.0);
    for (QDoubleSpinBox *spin : {spn_chart_animation_, spn_chart_reveal_,
                                 spn_chart_value_transition_, spn_chart_label_opacity_,
                                 spn_chart_element_opacity_})
        spin->setSuffix("%");
    btn_kf_chart_animation_ = mk_kf_button(fxm_tr("OBSTitles.ChartAnimationKeyframe"));
    btn_kf_chart_reveal_ = mk_kf_button(fxm_tr("OBSTitles.ChartRevealKeyframe"));
    btn_kf_chart_value_transition_ = mk_kf_button(fxm_tr("OBSTitles.ChartValueTransitionKeyframe"));
    btn_kf_chart_label_opacity_ = mk_kf_button(fxm_tr("OBSTitles.ChartLabelOpacityKeyframe"));
    btn_kf_chart_element_opacity_ = mk_kf_button(fxm_tr("OBSTitles.ChartElementOpacityKeyframe"));
    add_form_row(chart_animation_form, fxm_tr("OBSTitles.ChartAnimationProgress"), with_kf(spn_chart_animation_, btn_kf_chart_animation_));
    add_form_row(chart_animation_form, fxm_tr("OBSTitles.ChartRevealProgress"), with_kf(spn_chart_reveal_, btn_kf_chart_reveal_));
    add_form_row(chart_animation_form, fxm_tr("OBSTitles.ChartValueTransition"), with_kf(spn_chart_value_transition_, btn_kf_chart_value_transition_));
    add_form_row(chart_animation_form, fxm_tr("OBSTitles.ChartElementOpacity"), with_kf(spn_chart_element_opacity_, btn_kf_chart_element_opacity_));
    add_form_row(chart_animation_form, fxm_tr("OBSTitles.ChartLabelOpacity"), with_kf(spn_chart_label_opacity_, btn_kf_chart_label_opacity_));
    add_form_row(chart_animation_form, fxm_tr("OBSTitles.ChartUpdateTransition"), spn_chart_transition_);
    fxm_add_panel_section(vl, chart_animation_box_, fxm_tr("OBSTitles.ChartAnimation"));

    /* ── Paragraph ── */
    paragraph_box_ = new QGroupBox("Paragraph", inner);
    paragraph_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *paragraph_layout = new QVBoxLayout(paragraph_box_);
    paragraph_layout->setContentsMargins(5, 4, 5, 5);
    paragraph_layout->setSpacing(7);

    auto add_paragraph_button = [&](QHBoxLayout *layout, QButtonGroup *group,
                                    const char *icon_name, const QString &tip, int id) {
        auto *button = mk_paragraph_alignment_button(icon_name, tip);
        group->addButton(button, id);
        layout->addWidget(button);
        return button;
    };
    auto add_paragraph_gap = [](QHBoxLayout *layout) {
        auto *gap = new QWidget();
        gap->setFixedWidth(12);
        layout->addWidget(gap);
    };
    auto mk_paragraph_spin = [&]() {
        auto *spin = mk_dspin(-10000.0, 10000.0, 1.0);
        spin->setSuffix(QStringLiteral(" pt"));
        spin->setDecimals(0);
        spin->setMinimumWidth(0);
        spin->setMaximumWidth(QWIDGETSIZE_MAX);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        return spin;
    };
    auto add_metric_control = [&](QGridLayout *grid, int row, int column,
                                  const char *icon_name, const QString &tip, QDoubleSpinBox *spin, QWidget *field) {
        auto *icon = new NumericDragLabel(QString(), field, paragraph_box_,
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = true;
                                              emit property_changed(true);
                                          },
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = false;
                                              emit property_changed(true);
                                          });
        icon->setPixmap(obs_icon(icon_name).pixmap(16, 16));
        icon->setToolTip(QStringLiteral("%1\n%2").arg(tip, fxm_tr("OBSTitles.DragNumericLabelTooltip")));
        icon->setAccessibleName(tip);
        icon->setFixedWidth(20);
        icon->setAlignment(Qt::AlignCenter);
        spin->setToolTip(tip);
        grid->addWidget(icon, row, column * 2, Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(field, row, column * 2 + 1);
    };

    auto *horizontal_buttons = new QWidget(paragraph_box_);
    auto *horizontal_button_layout = new QHBoxLayout(horizontal_buttons);
    horizontal_button_layout->setContentsMargins(0, 0, 0, 0);
    horizontal_button_layout->setSpacing(4);
    grp_text_align_ = new QButtonGroup(horizontal_buttons);
    grp_text_align_->setExclusive(true);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-align-left.svg", fxm_tr("OBSTitles.AlignLeft"), 0);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-align-center.svg", fxm_tr("OBSTitles.AlignCenter"), 1);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-align-right.svg", fxm_tr("OBSTitles.AlignRight"), 2);
    add_paragraph_gap(horizontal_button_layout);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-justify-left.svg", fxm_tr("OBSTitles.JustifyLastLeft"), 3);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-justify-center.svg", fxm_tr("OBSTitles.JustifyLastCenter"), 4);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-justify-right.svg", fxm_tr("OBSTitles.JustifyLastRight"), 5);
    add_paragraph_gap(horizontal_button_layout);
    add_paragraph_button(horizontal_button_layout, grp_text_align_, "text-justify-full.svg", fxm_tr("OBSTitles.JustifyAll"), 6);
    horizontal_button_layout->addStretch(1);
    paragraph_layout->addWidget(horizontal_buttons);

    auto *vertical_buttons = new QWidget(paragraph_box_);
    auto *vertical_button_layout = new QHBoxLayout(vertical_buttons);
    vertical_button_layout->setContentsMargins(0, 0, 0, 0);
    vertical_button_layout->setSpacing(4);
    grp_text_valign_ = new QButtonGroup(vertical_buttons);
    grp_text_valign_->setExclusive(true);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "text-verticalalign-top.svg", fxm_tr("OBSTitles.AlignTop"), 0);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "text-verticalalign-center.svg", fxm_tr("OBSTitles.AlignMiddle"), 1);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "text-verticalalign-bottom.svg", fxm_tr("OBSTitles.AlignBottom"), 2);
    add_paragraph_button(vertical_button_layout, grp_text_valign_, "text-verticalalign-justify.svg", fxm_tr("OBSTitles.DistributeVertically"), 3);
    vertical_button_layout->addStretch(1);
    paragraph_layout->addWidget(vertical_buttons);

    spn_paragraph_indent_left_ = mk_paragraph_spin();
    spn_paragraph_indent_right_ = mk_paragraph_spin();
    spn_paragraph_indent_first_line_ = mk_paragraph_spin();
    btn_kf_paragraph_indent_left_ = mk_kf_button(fxm_tr("OBSTitles.ToggleParagraphIndentLeftKeyframe"));
    btn_kf_paragraph_indent_right_ = mk_kf_button(fxm_tr("OBSTitles.ToggleParagraphIndentRightKeyframe"));
    btn_kf_paragraph_indent_first_line_ = mk_kf_button(fxm_tr("OBSTitles.ToggleParagraphIndentFirstLineKeyframe"));
    auto *paragraph_indent_left_field = with_kf(spn_paragraph_indent_left_, btn_kf_paragraph_indent_left_);
    auto *paragraph_indent_right_field = with_kf(spn_paragraph_indent_right_, btn_kf_paragraph_indent_right_);
    auto *paragraph_indent_first_line_field = with_kf(spn_paragraph_indent_first_line_, btn_kf_paragraph_indent_first_line_);
    spn_paragraph_space_before_ = mk_paragraph_spin();
    spn_paragraph_space_after_ = mk_paragraph_spin();
    btn_kf_paragraph_space_before_ = mk_kf_button("Toggle Space Before Paragraph keyframe");
    btn_kf_paragraph_space_after_ = mk_kf_button("Toggle Space After Paragraph keyframe");
    auto *paragraph_space_before_field = with_kf(spn_paragraph_space_before_, btn_kf_paragraph_space_before_);
    auto *paragraph_space_after_field = with_kf(spn_paragraph_space_after_, btn_kf_paragraph_space_after_);
    auto *metric_grid = new QGridLayout();
    metric_grid->setContentsMargins(0, 0, 0, 0);
    metric_grid->setHorizontalSpacing(5);
    metric_grid->setVerticalSpacing(4);
    metric_grid->setColumnStretch(1, 1);
    metric_grid->setColumnStretch(3, 1);
    add_metric_control(metric_grid, 0, 0, "paragraph-indent-left.svg", fxm_tr("OBSTitles.ParagraphIndentLeft"), spn_paragraph_indent_left_, paragraph_indent_left_field);
    add_metric_control(metric_grid, 0, 1, "paragraph-indent-right.svg", fxm_tr("OBSTitles.ParagraphIndentRight"), spn_paragraph_indent_right_, paragraph_indent_right_field);
    add_metric_control(metric_grid, 1, 0, "paragraph-indent-first-line.svg", fxm_tr("OBSTitles.ParagraphIndentFirstLine"), spn_paragraph_indent_first_line_, paragraph_indent_first_line_field);
    add_metric_control(metric_grid, 2, 0, "paragraph-space-before.svg", fxm_tr("OBSTitles.ParagraphSpaceBefore"), spn_paragraph_space_before_, paragraph_space_before_field);
    add_metric_control(metric_grid, 2, 1, "paragraph-space-after.svg", fxm_tr("OBSTitles.ParagraphSpaceAfter"), spn_paragraph_space_after_, paragraph_space_after_field);
    paragraph_layout->addLayout(metric_grid);

    chk_paragraph_hyphenate_ = new FxmSwitch(fxm_tr("OBSTitles.Hyphenate"), paragraph_box_);
    style_checkbox(chk_paragraph_hyphenate_);
    paragraph_layout->addWidget(chk_paragraph_hyphenate_);

    fxm_add_panel_section(vl, paragraph_box_);

    /* ── Dynamic Text ── */
    dynamic_text_box_ = new QGroupBox("Text Flow", inner);
    dynamic_text_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *dynamic_form = new QFormLayout(dynamic_text_box_);
    style_form(dynamic_form);
    dynamic_form->setContentsMargins(5, 4, 5, 5);
    dynamic_form->setHorizontalSpacing(5);
    dynamic_form->setVerticalSpacing(6);
    cmb_text_style_ = new QComboBox(inner);
    cmb_text_style_->addItem(fxm_tr("OBSTitles.Normal"), 0);
    cmb_text_style_->addItem(fxm_tr("OBSTitles.AllCaps"), 1);
    cmb_text_style_->addItem(fxm_tr("OBSTitles.SmallCaps"), 2);
    cmb_text_style_->addItem(fxm_tr("OBSTitles.Superscript"), 3);
    cmb_text_style_->addItem(fxm_tr("OBSTitles.Subscript"), 4);
    cmb_text_style_->setToolTip(fxm_tr("OBSTitles.TextStyleTooltip"));
    cmb_text_style_->setFixedHeight(20);
    cmb_text_style_->setStyleSheet(control_style);
    cmb_text_overflow_ = new QComboBox(inner);
    cmb_text_overflow_->addItem(fxm_tr("OBSTitles.Wrap"), 0);
    cmb_text_overflow_->addItem(fxm_tr("OBSTitles.Clip"), 1);
    cmb_text_overflow_->addItem(fxm_tr("OBSTitles.HorizontalFit"), 2);
    cmb_text_overflow_->setToolTip(fxm_tr("OBSTitles.TextOverflowTooltip"));
    cmb_text_overflow_->setFixedHeight(20);
    cmb_text_overflow_->setStyleSheet(control_style);
    spn_text_fit_min_scale_ = mk_dspin(0.05, 1.0, 0.05);
    spn_text_fit_min_scale_->setDecimals(2);
    spn_text_fit_min_scale_->setToolTip(fxm_tr("OBSTitles.MinFitScaleTooltip"));
    lbl_text_fit_scale_ = new QLabel(fxm_tr("OBSTitles.Scale100"), inner);
    lbl_text_fit_scale_->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
    chk_expose_text_ = new FxmSwitch(fxm_tr("OBSTitles.ExposeInDock"), inner);
    chk_expose_text_->setToolTip(fxm_tr("OBSTitles.ExposeInDockTooltip"));
    style_checkbox(chk_expose_text_);
    chk_exposed_hide_if_empty_ = new FxmSwitch(fxm_tr("OBSTitles.ExposedHideIfEmpty"), inner);
    chk_exposed_hide_if_empty_->setToolTip(fxm_tr("OBSTitles.ExposedHideIfEmptyTooltip"));
    style_checkbox(chk_exposed_hide_if_empty_);
    chk_exposed_single_value_ = new FxmSwitch(fxm_tr("OBSTitles.ExposedSingleValue"), inner);
    chk_exposed_single_value_->setToolTip(fxm_tr("OBSTitles.ExposedSingleValueTooltip"));
    style_checkbox(chk_exposed_single_value_);
    chk_expose_fill_ = new FxmSwitch(QString(), inner);
    chk_expose_fill_->setToolTip(QStringLiteral("Expose this layer's Fill color as a live cue dock control."));
    style_checkbox(chk_expose_fill_);
    chk_exposed_fill_single_value_ = new FxmSwitch(QString(), inner);
    chk_exposed_fill_single_value_->setToolTip(QStringLiteral("Use one Fill value for all live cue rows."));
    style_checkbox(chk_exposed_fill_single_value_);
    chk_expose_stroke_ = new FxmSwitch(QString(), inner);
    chk_expose_stroke_->setToolTip(QStringLiteral("Expose this layer's Stroke color as a live cue dock control."));
    style_checkbox(chk_expose_stroke_);
    chk_exposed_stroke_single_value_ = new FxmSwitch(QString(), inner);
    chk_exposed_stroke_single_value_->setToolTip(QStringLiteral("Use one Stroke value for all live cue rows."));
    style_checkbox(chk_exposed_stroke_single_value_);
    chk_ignore_persistence_ = new FxmSwitch(fxm_tr("OBSTitles.IgnorePersistence"), inner);
    chk_ignore_persistence_->setToolTip(fxm_tr("OBSTitles.IgnorePersistenceTooltip"));
    style_checkbox(chk_ignore_persistence_);
    row_ticker_playback_ = new QWidget(inner);
    auto *ticker_playback_layout = new QHBoxLayout(row_ticker_playback_);
    ticker_playback_layout->setContentsMargins(0, 0, 0, 0);
    ticker_playback_layout->setSpacing(6);
    btn_ticker_pause_ = new QToolButton(row_ticker_playback_);
    btn_ticker_pause_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn_ticker_pause_->setFixedHeight(20);
    btn_ticker_pause_->setAutoRaise(false);
    btn_ticker_pause_->setStyleSheet(control_style);
    cmb_ticker_playback_mode_ = new QComboBox(row_ticker_playback_);
    cmb_ticker_playback_mode_->addItem(fxm_tr("OBSTitles.TickerAlwaysPlay"),
                                       static_cast<int>(TickerPlaybackMode::AlwaysPlay));
    cmb_ticker_playback_mode_->addItem(fxm_tr("OBSTitles.TickerPausedUntilCued"),
                                       static_cast<int>(TickerPlaybackMode::PausedUntilCued));
    cmb_ticker_playback_mode_->addItem(fxm_tr("OBSTitles.TickerPausedUntilHotkey"),
                                       static_cast<int>(TickerPlaybackMode::PausedUntilHotkey));
    cmb_ticker_playback_mode_->addItem(fxm_tr("OBSTitles.TickerCustomPlayback"),
                                       static_cast<int>(TickerPlaybackMode::CustomPlayback));
    cmb_ticker_playback_mode_->setFixedHeight(20);
    cmb_ticker_playback_mode_->setStyleSheet(control_style);
    ticker_playback_layout->addWidget(btn_ticker_pause_);
    ticker_playback_layout->addWidget(cmb_ticker_playback_mode_, 1);

    row_ticker_completion_ = new QWidget(inner);
    auto *ticker_completion_layout = new QHBoxLayout(row_ticker_completion_);
    ticker_completion_layout->setContentsMargins(0, 0, 0, 0);
    ticker_completion_layout->setSpacing(4);
    spn_ticker_completion_ = mk_dspin(0.0, 100.0, 1.0);
    spn_ticker_completion_->setSuffix(" %");
    spn_ticker_completion_->setDecimals(1);
    btn_kf_ticker_completion_ = mk_kf_button(fxm_tr("OBSTitles.ToggleTickerCompletionKeyframe"));
    ticker_completion_layout->addWidget(spn_ticker_completion_, 1);
    ticker_completion_layout->addWidget(
        make_keyframe_controls(btn_kf_ticker_completion_, row_ticker_completion_));

    cmb_ticker_style_ = new QComboBox(inner);
    cmb_ticker_style_->addItem(fxm_tr("OBSTitles.TickerHorizontal"), 0);
    cmb_ticker_style_->addItem(fxm_tr("OBSTitles.TickerVerticalLine"), 1);
    cmb_ticker_style_->addItem(fxm_tr("OBSTitles.TickerVerticalSmooth"), 2);
    cmb_ticker_style_->setFixedHeight(20);
    cmb_ticker_style_->setStyleSheet(control_style);
    spn_ticker_speed_ = mk_dspin(1.0, 5000.0, 1.0);
    spn_ticker_speed_->setSuffix(" px/s");
    spn_ticker_line_hold_ = new TimecodeSpinBox(inner);
    spn_ticker_line_hold_->setRange(0.1, 60.0);
    spn_ticker_line_hold_->setFixedHeight(20);
    spn_ticker_line_hold_->setStyleSheet(control_style);
    cmb_ticker_direction_ = new QComboBox(inner);
    cmb_ticker_direction_->setFixedHeight(20);
    cmb_ticker_direction_->setStyleSheet(control_style);
    add_form_row(dynamic_form, "Text Style", cmb_text_style_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.OverflowLabel"), cmb_text_overflow_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.MinFitScaleLabel"), spn_text_fit_min_scale_);
    add_form_row(dynamic_form, "", lbl_text_fit_scale_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.TickerPlaybackLabel"), row_ticker_playback_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.TickerCompletionLabel"), row_ticker_completion_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.TickerStyleLabel"), cmb_ticker_style_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.TickerSpeedLabel"), spn_ticker_speed_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.TickerLineHoldLabel"), spn_ticker_line_hold_);
    add_form_row(dynamic_form, fxm_tr("OBSTitles.DirectionLabel"), cmb_ticker_direction_);
    fxm_add_panel_section(vl, dynamic_text_box_);

    /* ── Auto Styling ── */
    auto_style_box_ = new QGroupBox("Auto Styling Rules", inner);
    auto_style_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *auto_layout = new QVBoxLayout(auto_style_box_);
    auto_layout->setContentsMargins(5, 4, 5, 5);
    auto_layout->setSpacing(6);
    chk_auto_style_enabled_ = new FxmSwitch("Enable automatic text styling", auto_style_box_);
    chk_auto_style_enabled_->setToolTip("When enabled, the selected text layer is styled by the rule engine before rendering. The original text stays editable; rules only produce final style runs.");
    style_checkbox(chk_auto_style_enabled_);
    auto_layout->addWidget(chk_auto_style_enabled_);

    auto *auto_help = new QLabel("Automatically format names, labels, dates and other text patterns. Start by formatting an example, then let GSP learn it.", auto_style_box_);
    auto_help->setWordWrap(true);
    auto_help->setToolTip("Auto Styling also applies to Live Text Cue values. Manual inline formatting remains editable and is applied after automatic rules.");
    auto_layout->addWidget(auto_help);

    auto *quick_setup_box = new QGroupBox("Quick Setup", auto_style_box_);
    auto *quick_setup_layout = new QVBoxLayout(quick_setup_box);
    quick_setup_layout->setContentsMargins(8, 8, 8, 8);
    quick_setup_layout->setSpacing(5);
    btn_auto_learn_formatting_ = new QPushButton("Learn from formatted example…", quick_setup_box);
    btn_auto_learn_formatting_->setToolTip("Format part of the current text, then click here. GSP will create reusable rules from the visible structure, including spaces and line or paragraph breaks.");
    style_push_button(btn_auto_learn_formatting_);
    quick_setup_layout->addWidget(btn_auto_learn_formatting_);
    auto *learn_hint = new QLabel("1. Format an example  2. Learn  3. Test with different text", quick_setup_box);
    learn_hint->setWordWrap(true);
    quick_setup_layout->addWidget(learn_hint);
    auto_layout->addWidget(quick_setup_box);

    auto *file_row = new QHBoxLayout();
    btn_auto_rules_load_ = new QPushButton("Load Set…", auto_style_box_);
    btn_auto_rules_save_ = new QPushButton("Save Set", auto_style_box_);
    btn_auto_rules_save_as_ = new QPushButton("Save Set As…", auto_style_box_);
    for (auto *button : {btn_auto_rules_load_, btn_auto_rules_save_, btn_auto_rules_save_as_}) {
        style_push_button(button);
        file_row->addWidget(button);
    }
    btn_auto_rules_load_->setToolTip("Load or import an Auto Styling rule set file. You can replace the current rules or append the loaded rules.");
    btn_auto_rules_save_->setToolTip("Save the current complete rule set to its last file.");
    btn_auto_rules_save_as_->setToolTip("Save the current base style and all rules as a reusable .gsp-auto-style.json file.");
    auto_layout->addLayout(file_row);
    lbl_auto_rules_status_ = new QLabel("Rule set: Not saved", auto_style_box_);
    lbl_auto_rules_status_->setWordWrap(true);
    auto_layout->addWidget(lbl_auto_rules_status_);

    auto *rules_heading = new QLabel("Rules — double-click to edit", auto_style_box_);
    auto_layout->addWidget(rules_heading);
    lst_auto_style_rules_ = new QListWidget(auto_style_box_);
    lst_auto_style_rules_->setMinimumHeight(150);
    lst_auto_style_rules_->setStyleSheet(control_style);
    lst_auto_style_rules_->setToolTip("Double-click a rule to open the full Rule Editor. Rules are evaluated from top to bottom.");
    auto_layout->addWidget(lst_auto_style_rules_);
    auto *main_rule_actions = new QHBoxLayout();
    btn_auto_rules_edit_ = new QPushButton("Open Rule Editor…", auto_style_box_);
    btn_auto_rules_clear_ = new QPushButton("Clear All Rules…", auto_style_box_);
    btn_auto_rules_edit_->setToolTip("Open the Auto Styling Rule Editor with demo text, the complete rule list and all settings.");
    btn_auto_rules_clear_->setToolTip("Remove every Auto Styling rule after confirmation.");
    for (auto *button : {btn_auto_rules_edit_, btn_auto_rules_clear_}) {
        style_push_button(button);
        main_rule_actions->addWidget(button);
    }
    auto_layout->addLayout(main_rule_actions);

    auto_rules_dialog_ = new QDialog(this);
    auto_rules_dialog_->setWindowTitle("Auto Styling Rule Editor");
    auto_rules_dialog_->setModal(true);
    auto_rules_dialog_->resize(980, 720);
    auto *dialog_layout = new QVBoxLayout(auto_rules_dialog_);
    dialog_layout->setContentsMargins(12, 12, 12, 12);
    dialog_layout->setSpacing(8);

    auto *demo_box = new QGroupBox("Demo / Test", auto_rules_dialog_);
    auto *demo_layout = new QVBoxLayout(demo_box);
    demo_layout->setContentsMargins(8, 8, 8, 8);
    auto *demo_hint = new QLabel("Type or paste sample text here to test the current rule set. Invisible characters and line/paragraph breaks are preserved.", demo_box);
    demo_hint->setWordWrap(true);
    txt_auto_rules_demo_ = new QTextEdit(demo_box);
    txt_auto_rules_demo_->setPlaceholderText("NAME: Example text\nSECOND NAME: Another example");
    txt_auto_rules_demo_->setMinimumHeight(115);
    txt_auto_rules_demo_->setStyleSheet(control_style);
    lbl_auto_rules_demo_status_ = new QLabel("No demo text", demo_box);
    lbl_auto_rules_demo_status_->setWordWrap(true);
    demo_layout->addWidget(demo_hint);
    demo_layout->addWidget(txt_auto_rules_demo_);
    demo_layout->addWidget(lbl_auto_rules_demo_status_);
    dialog_layout->addWidget(demo_box);

    auto *dialog_splitter = new QSplitter(Qt::Horizontal, auto_rules_dialog_);
    auto *dialog_left = new QWidget(dialog_splitter);
    auto *dialog_left_layout = new QVBoxLayout(dialog_left);
    dialog_left_layout->setContentsMargins(0, 0, 4, 0);
    dialog_left_layout->addWidget(new QLabel("Rules", dialog_left));
    lst_auto_rules_dialog_ = new QListWidget(dialog_left);
    lst_auto_rules_dialog_->setMinimumWidth(300);
    lst_auto_rules_dialog_->setStyleSheet(control_style);
    dialog_left_layout->addWidget(lst_auto_rules_dialog_);

    auto *dialog_right = new QWidget(dialog_splitter);
    auto *dialog_right_layout = new QVBoxLayout(dialog_right);
    dialog_right_layout->setContentsMargins(4, 0, 0, 0);
    auto *default_style_form = new QFormLayout();
    style_form(default_style_form);
    cmb_auto_default_style_ = new QComboBox(auto_rules_dialog_);
    cmb_auto_default_style_->setFixedHeight(20);
    cmb_auto_default_style_->setStyleSheet(control_style);
    cmb_auto_default_style_->setToolTip("Base preset for the entire text box. Rules are applied on top of this default style.");
    default_style_form->addRow("Base style", cmb_auto_default_style_);
    dialog_right_layout->addLayout(default_style_form);
    auto_rule_editor_box_ = new QGroupBox("Selected Rule", auto_rules_dialog_);
    auto_rule_editor_box_->setEnabled(false);
    auto *rule_editor_layout = new QVBoxLayout(auto_rule_editor_box_);
    rule_editor_layout->setContentsMargins(8, 8, 8, 8);
    rule_editor_layout->setSpacing(5);
    auto *rule_form = new QFormLayout();
    style_form(rule_form);
    cmb_auto_rule_style_ = new QComboBox(auto_rules_dialog_);
    cmb_auto_rule_style_->setFixedHeight(20);
    cmb_auto_rule_style_->setStyleSheet(control_style);
    cmb_auto_rule_style_->setToolTip("Text style preset to apply when this rule matches.");
    edt_auto_rule_name_ = new QLineEdit(auto_rules_dialog_);
    edt_auto_rule_name_->setPlaceholderText("Optional rule name");
    edt_auto_rule_name_->setFixedHeight(20);
    edt_auto_rule_name_->setStyleSheet(control_style);
    edt_auto_rule_name_->setToolTip("Optional friendly name shown in the rule list. Leave empty to use the preset name.");
    chk_auto_rule_enabled_ = new FxmSwitch("This rule is active", auto_rules_dialog_);
    chk_auto_rule_enabled_->setToolTip("Disable this rule without deleting it.");
    chk_auto_rule_enabled_->setChecked(true);
    style_checkbox(chk_auto_rule_enabled_);
    auto fill_auto_marker_combo = [](QComboBox *combo) {
        combo->addItem("Start of text", "text_start");
        combo->addItem("End of text", "text_end");
        combo->addItem("Character index", "character_index");
        combo->addItem("Number of characters", "character_count");
        combo->addItem("Number of words", "word_count");
        combo->addItem("Space / tab", "space");
        combo->addItem("Line break", "line_break");
        combo->addItem("New line", "newline");
        combo->addItem("Start of paragraph", "paragraph_start");
        combo->addItem("End of paragraph", "paragraph_end");
        combo->addItem("Custom character", "custom_char");
    };
    cmb_auto_rule_start_condition_ = new QComboBox(auto_rules_dialog_);
    cmb_auto_rule_end_condition_ = new QComboBox(auto_rules_dialog_);
    fill_auto_marker_combo(cmb_auto_rule_start_condition_);
    fill_auto_marker_combo(cmb_auto_rule_end_condition_);
    cmb_auto_rule_start_condition_->setFixedHeight(20);
    cmb_auto_rule_end_condition_->setFixedHeight(20);
    cmb_auto_rule_start_condition_->setStyleSheet(control_style);
    cmb_auto_rule_end_condition_->setStyleSheet(control_style);
    cmb_auto_rule_start_condition_->setToolTip("Where the styled range begins. Marker rules scan the entire text box, not only the beginning.");
    cmb_auto_rule_end_condition_->setToolTip("Where the styled range ends. For marker-based rules, this is searched after each matching From marker.");
    spn_auto_rule_start_offset_ = new QSpinBox(auto_rules_dialog_);
    spn_auto_rule_end_offset_ = new QSpinBox(auto_rules_dialog_);
    for (auto *spin : {spn_auto_rule_start_offset_, spn_auto_rule_end_offset_}) {
        spin->setRange(0, 100000);
        spin->setFixedHeight(20);
        spin->setStyleSheet(control_style);
        spin->setToolTip("Offset / occurrence number. For Character index, this is the exact character position. For marker rules, 0 means the first matching marker.");
    }
    edt_auto_rule_start_chars_ = new QLineEdit(auto_rules_dialog_);
    edt_auto_rule_end_chars_ = new QLineEdit(auto_rules_dialog_);
    edt_auto_rule_start_chars_->setMaxLength(8);
    edt_auto_rule_end_chars_->setMaxLength(8);
    edt_auto_rule_start_chars_->setPlaceholderText("custom");
    edt_auto_rule_end_chars_->setPlaceholderText("custom");
    edt_auto_rule_start_chars_->setFixedHeight(20);
    edt_auto_rule_end_chars_->setFixedHeight(20);
    edt_auto_rule_start_chars_->setStyleSheet(control_style);
    edt_auto_rule_end_chars_->setStyleSheet(control_style);
    edt_auto_rule_start_chars_->setToolTip("Characters to search for when From is set to Custom character. Example: : or #.");
    edt_auto_rule_end_chars_->setToolTip("Characters to search for when To is set to Custom character. Example: , or ).");
    auto *start_row = new QWidget(auto_rules_dialog_);
    auto *start_lay = new QHBoxLayout(start_row);
    start_lay->setContentsMargins(0, 0, 0, 0);
    start_lay->setSpacing(4);
    start_lay->addWidget(cmb_auto_rule_start_condition_, 2);
    start_lay->addWidget(spn_auto_rule_start_offset_, 1);
    start_lay->addWidget(edt_auto_rule_start_chars_, 1);
    auto *end_row = new QWidget(auto_rules_dialog_);
    auto *end_lay = new QHBoxLayout(end_row);
    end_lay->setContentsMargins(0, 0, 0, 0);
    end_lay->setSpacing(4);
    end_lay->addWidget(cmb_auto_rule_end_condition_, 2);
    end_lay->addWidget(spn_auto_rule_end_offset_, 1);
    end_lay->addWidget(edt_auto_rule_end_chars_, 1);

    chk_auto_rule_require_stop_match_ = new FxmSwitch("Require stop match", auto_rules_dialog_);
    chk_auto_rule_require_stop_match_->setChecked(true);
    chk_auto_rule_require_stop_match_->setToolTip("When enabled, the rule is skipped if the Stop matching at condition is not found after the Start condition. This prevents accidental styling to the line/text end when a delimiter is missing in live text cues.");
    style_checkbox(chk_auto_rule_require_stop_match_);
    chk_auto_rule_include_start_marker_ = new FxmSwitch("Include From custom character", auto_rules_dialog_);
    chk_auto_rule_include_start_marker_->setChecked(true);
    chk_auto_rule_include_start_marker_->setToolTip("When From is Custom character, include that character in the styled range. Disable it to start styling after the marker.");
    style_checkbox(chk_auto_rule_include_start_marker_);
    chk_auto_rule_include_end_marker_ = new FxmSwitch("Include To custom character", auto_rules_dialog_);
    chk_auto_rule_include_end_marker_->setChecked(false);
    chk_auto_rule_include_end_marker_->setToolTip("When Stop matching at is Custom character, include that stop character in the styled range. Leave disabled to stop before the marker.");
    style_checkbox(chk_auto_rule_include_end_marker_);
    auto *marker_options_row = new QWidget(auto_rules_dialog_);
    auto *marker_options_lay = new QVBoxLayout(marker_options_row);
    marker_options_lay->setContentsMargins(0, 0, 0, 0);
    marker_options_lay->setSpacing(2);
    marker_options_lay->addWidget(chk_auto_rule_require_stop_match_);
    marker_options_lay->addWidget(chk_auto_rule_include_start_marker_);
    marker_options_lay->addWidget(chk_auto_rule_include_end_marker_);

    cmb_auto_rule_conflict_mode_ = new QComboBox(auto_rules_dialog_);
    cmb_auto_rule_conflict_mode_->addItem("Override previous rules", "override_previous");
    cmb_auto_rule_conflict_mode_->addItem("Respect previous rules", "respect_previous");
    cmb_auto_rule_conflict_mode_->addItem("Apply only if empty", "apply_if_empty");
    cmb_auto_rule_conflict_mode_->addItem("Merge", "merge");
    cmb_auto_rule_conflict_mode_->addItem("Exclude selected rules", "exclude_other_rules");
    cmb_auto_rule_conflict_mode_->setFixedHeight(20);
    cmb_auto_rule_conflict_mode_->setStyleSheet(control_style);
    cmb_auto_rule_conflict_mode_->setToolTip("How this rule behaves when another rule already styled the same text. Override wins, Respect keeps previous fields, Apply only if empty affects unstyled text, Exclude blocks selected rules.");
    cmb_auto_rule_match_mode_ = new QComboBox(auto_rules_dialog_);
    cmb_auto_rule_match_mode_->addItem("All matches", "all_matches");
    cmb_auto_rule_match_mode_->addItem("First match only", "first_match");
    cmb_auto_rule_match_mode_->setFixedHeight(20);
    cmb_auto_rule_match_mode_->setStyleSheet(control_style);
    cmb_auto_rule_match_mode_->setToolTip("Apply to every matching range in the text box or only to the first one.");
    cmb_auto_rule_generalization_ = new QComboBox(auto_rules_dialog_);
    cmb_auto_rule_generalization_->addItem("Smart merge equivalent examples", "auto_merge");
    cmb_auto_rule_generalization_->addItem("Exact structure only", "exact_only");
    cmb_auto_rule_generalization_->addItem("Always keep separate", "keep_separate");
    cmb_auto_rule_generalization_->setFixedHeight(20);
    cmb_auto_rule_generalization_->setStyleSheet(control_style);
    cmb_auto_rule_generalization_->setToolTip("Controls whether learned examples that describe the same reusable structure are folded into one rule.");
    chk_auto_rule_prevent_duplicates_ = new FxmSwitch("Skip exact duplicate rules", auto_rules_dialog_);
    chk_auto_rule_prevent_duplicates_->setChecked(true);
    chk_auto_rule_allow_multiple_cases_ = new FxmSwitch("Apply one rule to every matching case", auto_rules_dialog_);
    chk_auto_rule_allow_multiple_cases_->setChecked(true);
    edt_auto_rule_excludes_ = new QLineEdit(auto_rules_dialog_);
    edt_auto_rule_excludes_->setPlaceholderText("Rule IDs to exclude, comma-separated");
    edt_auto_rule_excludes_->setFixedHeight(20);
    edt_auto_rule_excludes_->setStyleSheet(control_style);
    edt_auto_rule_excludes_->setToolTip("Comma-separated rule IDs to block where this rule matches. Example: rule_1, rule_3. Use with Conflict: Exclude selected rules.");
    chk_auto_rule_stop_processing_ = new FxmSwitch("Stop later rules on this matched text", auto_rules_dialog_);
    chk_auto_rule_stop_processing_->setToolTip("After this rule matches, later rules in the list cannot change the same characters. Useful for speaker names, labels, or protected prefixes.");
    style_checkbox(chk_auto_rule_stop_processing_);
    spn_auto_rule_chars_ = new QSpinBox(auto_rules_dialog_);
    spn_auto_rule_chars_->hide();
    rule_form->addRow("Rule name", edt_auto_rule_name_);
    rule_form->addRow("Apply preset", cmb_auto_rule_style_);
    rule_form->addRow("Active", chk_auto_rule_enabled_);
    rule_form->addRow("Start matching at", start_row);
    rule_form->addRow("Stop matching at", end_row);
    rule_form->addRow("Stop safety", marker_options_row);
    rule_editor_layout->addLayout(rule_form);
    btn_auto_rule_advanced_ = new QToolButton(auto_rule_editor_box_);
    btn_auto_rule_advanced_->setText("Advanced options");
    btn_auto_rule_advanced_->setCheckable(true);
    btn_auto_rule_advanced_->setChecked(false);
    btn_auto_rule_advanced_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn_auto_rule_advanced_->setArrowType(Qt::RightArrow);
    rule_editor_layout->addWidget(btn_auto_rule_advanced_);
    auto_rule_advanced_widget_ = new QWidget(auto_rule_editor_box_);
    auto *advanced_form = new QFormLayout(auto_rule_advanced_widget_);
    style_form(advanced_form);
    advanced_form->setContentsMargins(0, 0, 0, 0);
    advanced_form->addRow("Apply to", cmb_auto_rule_match_mode_);
    advanced_form->addRow("Learned examples", cmb_auto_rule_generalization_);
    advanced_form->addRow("Duplicates", chk_auto_rule_prevent_duplicates_);
    advanced_form->addRow("Reusable cases", chk_auto_rule_allow_multiple_cases_);
    advanced_form->addRow("If rules overlap", cmb_auto_rule_conflict_mode_);
    advanced_form->addRow("Exclude rules", edt_auto_rule_excludes_);
    advanced_form->addRow("Protect match", chk_auto_rule_stop_processing_);
    auto_rule_advanced_widget_->setVisible(false);
    rule_editor_layout->addWidget(auto_rule_advanced_widget_);
    QObject::connect(btn_auto_rule_advanced_, &QToolButton::toggled, auto_rule_advanced_widget_, &QWidget::setVisible);
    QObject::connect(btn_auto_rule_advanced_, &QToolButton::toggled, btn_auto_rule_advanced_, [this](bool open) {
        btn_auto_rule_advanced_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    dialog_right_layout->addWidget(auto_rule_editor_box_);
    auto *rule_buttons = new QHBoxLayout();
    btn_auto_rule_add_ = new QPushButton("Add rule", auto_rules_dialog_);
    btn_auto_rule_update_ = new QPushButton("Save rule", auto_rules_dialog_);
    btn_auto_rule_delete_ = new QPushButton("Delete", auto_rules_dialog_);
    btn_auto_rules_clear_dialog_ = new QPushButton("Clear All…", auto_rules_dialog_);
    btn_auto_rule_up_ = new QPushButton("↑", auto_rules_dialog_);
    btn_auto_rule_down_ = new QPushButton("↓", auto_rules_dialog_);
    btn_auto_rule_add_->setToolTip("Create a new rule from the editor fields below.");
    btn_auto_rule_update_->setToolTip("Save the current editor fields into the selected rule.");
    btn_auto_rule_delete_->setToolTip("Delete the selected rule.");
    btn_auto_rules_clear_dialog_->setToolTip("Remove every Auto Styling rule after confirmation.");
    btn_auto_rule_up_->setToolTip("Move selected rule earlier. Earlier rules are evaluated first.");
    btn_auto_rule_down_->setToolTip("Move selected rule later. Later rules can override earlier ones depending on conflict mode.");
    for (auto *b : {btn_auto_rule_add_, btn_auto_rule_update_, btn_auto_rule_delete_, btn_auto_rules_clear_dialog_, btn_auto_rule_up_, btn_auto_rule_down_}) {
        style_push_button(b);
        rule_buttons->addWidget(b);
    }
    rule_editor_layout->addLayout(rule_buttons);

    dialog_splitter->addWidget(dialog_left);
    dialog_splitter->addWidget(dialog_right);
    dialog_splitter->setStretchFactor(0, 1);
    dialog_splitter->setStretchFactor(1, 2);
    dialog_layout->addWidget(dialog_splitter, 1);
    auto *dialog_buttons = new QDialogButtonBox(QDialogButtonBox::Close, auto_rules_dialog_);
    QObject::connect(dialog_buttons, &QDialogButtonBox::rejected, auto_rules_dialog_, &QDialog::reject);
    dialog_layout->addWidget(dialog_buttons);
    fxm_add_panel_section(vl, auto_style_box_);

    /* ── Live Properties ── */
    live_edit_box_ = new QGroupBox(QStringLiteral("Live Properties"), inner);
    live_edit_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *live_edit_form = new QFormLayout(live_edit_box_);
    style_form(live_edit_form);
    live_edit_form->setContentsMargins(5, 4, 5, 5);
    live_edit_form->setHorizontalSpacing(12);
    live_edit_form->setVerticalSpacing(6);
    chk_scene_mask_->setText(QString());
    chk_scene_mask_->setToolTip(fxm_tr("OBSTitles.LiveSceneMaskTooltip"));
    chk_scene_mask_->setFixedHeight(20);
    chk_scene_mask_->setStyleSheet(checkbox_style);
    chk_expose_text_->setText(QString());
    chk_expose_text_->setToolTip(fxm_tr("OBSTitles.ExposeInDockTooltip"));
    if (chk_exposed_hide_if_empty_) chk_exposed_hide_if_empty_->setText(QString());
    if (chk_exposed_single_value_) chk_exposed_single_value_->setText(QString());
    if (chk_ignore_persistence_) chk_ignore_persistence_->setText(QString());
    if (chk_expose_fill_) chk_expose_fill_->setText(QString());
    if (chk_exposed_fill_single_value_) chk_exposed_fill_single_value_->setText(QString());
    if (chk_expose_stroke_) chk_expose_stroke_->setText(QString());
    if (chk_exposed_stroke_single_value_) chk_exposed_stroke_single_value_->setText(QString());
    auto compact_live_field = [this](QWidget *field) {
        auto *row = new QWidget(live_edit_box_);
        row->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(field, 0, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addStretch(1);
        return row;
    };
    add_form_row(live_edit_form, fxm_tr("OBSTitles.SetAsSceneMask"), compact_live_field(chk_scene_mask_));
    add_form_row(live_edit_form, fxm_tr("OBSTitles.ExposeToDock"), compact_live_field(chk_expose_text_));
    add_form_row(live_edit_form, fxm_tr("OBSTitles.ExposedHideIfEmpty"), compact_live_field(chk_exposed_hide_if_empty_));
    add_form_row(live_edit_form, fxm_tr("OBSTitles.ExposedSingleValue"), compact_live_field(chk_exposed_single_value_));
    add_form_row(live_edit_form, QStringLiteral("Expose Fill"), compact_live_field(chk_expose_fill_));
    add_form_row(live_edit_form, QStringLiteral("Fill single value"), compact_live_field(chk_exposed_fill_single_value_));
    add_form_row(live_edit_form, QStringLiteral("Expose Stroke"), compact_live_field(chk_expose_stroke_));
    add_form_row(live_edit_form, QStringLiteral("Stroke single value"), compact_live_field(chk_exposed_stroke_single_value_));
    if (cmb_video_playback_) {
        cmb_video_playback_->setParent(live_edit_box_);
        add_form_row(live_edit_form, fxm_tr("OBSTitles.AssetPlayback"), cmb_video_playback_);
    }
    add_form_row(live_edit_form, fxm_tr("OBSTitles.Persistence"), compact_live_field(chk_ignore_persistence_));
    fxm_add_panel_section(vl, live_edit_box_);

    /* ── Bullets and Numbering ── */
    bullets_box_ = new QGroupBox("Bullets and Numbering", inner);
    bullets_box_->setStyleSheet(section_style);
    auto *bullets_layout = new QVBoxLayout(bullets_box_);
    bullets_layout->setContentsMargins(5, 4, 5, 5);
    auto *bullets_hint = new QLabel("Broadcast lower thirds typically use manual bullet glyphs; this group is ready for list presets.", inner);
    bullets_hint->setWordWrap(true);
    bullets_hint->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
    bullets_layout->addWidget(bullets_hint);
    fxm_add_panel_section(vl, bullets_box_);
    bullets_box_->hide();

    /* ── Shape ── */
    rect_box_ = new QGroupBox(inner);
    rect_box_->setTitle(QString());
    rect_box_->setStyleSheet(QStringLiteral("QGroupBox{background:%1;border:none;margin:0;padding:0;}").arg(section_bg_name));
    auto *shape_layout = new QVBoxLayout(rect_box_);
    shape_layout->setContentsMargins(5, 4, 5, 5);
    shape_layout->setSpacing(2);

    btn_shape_defaults_ = new QToolButton(rect_box_);
    btn_shape_defaults_->setObjectName(QStringLiteral("FxmPanelDefaultsButton"));
    btn_shape_defaults_->setAutoRaise(true);
    btn_shape_defaults_->setFixedSize(22, 20);
    btn_shape_defaults_->setIcon(fxm_panel_defaults_icon(palette()));
    btn_shape_defaults_->setIconSize(QSize(14, 14));
    btn_shape_defaults_->setToolTip(fxm_tr("OBSTitles.RestoreShapeDefaults"));
    btn_shape_defaults_->setStyleSheet(fxm_transform_panel_button_style(palette()));

    auto *shape_types_row = new QWidget(rect_box_);
    shape_types_row->setObjectName(QStringLiteral("OBSTitlesShapeTypeButtonsRow"));
    shape_types_row->setStyleSheet("background:transparent;");
    auto *shape_types_layout = new QHBoxLayout(shape_types_row);
    shape_types_layout->setContentsMargins(0, 0, 0, 0);
    shape_types_layout->setSpacing(4);
    grp_shape_type_ = new QButtonGroup(shape_types_row);
    grp_shape_type_->setExclusive(true);
    auto add_shape_button = [&](ShapeType shape_type) {
        auto *button = new QToolButton(shape_types_row);
        button->setCheckable(true);
        button->setIcon(shape_tool_icon(shape_type));
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(30, 28);
        button->setToolTip(shape_display_name(shape_type));
        button->setAccessibleName(shape_display_name(shape_type));
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:0;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name, highlight_name, highlighted_text_name));
        grp_shape_type_->addButton(button, (int)shape_type);
        shape_types_layout->addWidget(button);
    };
    for (ShapeType shape_type : {ShapeType::Rectangle, ShapeType::RoundedRectangle, ShapeType::Ellipse,
                                 ShapeType::Triangle, ShapeType::Star, ShapeType::Polygon,
                                 ShapeType::Diamond}) {
        add_shape_button(shape_type);
    }
    shape_types_layout->addStretch(1);
    shape_layout->addWidget(shape_types_row);

    auto *shape_grid = new QGridLayout();
    shape_grid->setContentsMargins(0, 0, 0, 0);
    shape_grid->setHorizontalSpacing(4);
    shape_grid->setVerticalSpacing(2);
    shape_grid->setColumnMinimumWidth(0, 72);
    shape_grid->setColumnMinimumWidth(1, 0);
    shape_grid->setColumnMinimumWidth(2, 0);
    shape_grid->setColumnMinimumWidth(3, 30);
    shape_grid->setColumnMinimumWidth(4, 0);
    shape_grid->setColumnStretch(1, 1);
    shape_grid->setColumnStretch(2, 1);

    auto make_shape_field = [&](const QString &label, QDoubleSpinBox *spin, const QIcon &icon = QIcon()) {
        auto *field = new QWidget(rect_box_);
        field->setObjectName(QStringLiteral("OBSTitlesShapeNumericField"));
        field->setStyleSheet(QStringLiteral(
            "QWidget#OBSTitlesShapeNumericField{background:transparent;border:none;}"));
        auto *field_layout = new QHBoxLayout(field);
        field_layout->setContentsMargins(0, 0, 0, 0);
        field_layout->setSpacing(4);
        auto *field_label = new NumericDragLabel(label, spin, field,
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = true;
                                                     emit property_changed(true);
                                                 },
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = false;
                                                     emit property_changed(true);
                                                 });
        field_label->setFixedWidth(icon.isNull() ? 16 : 18);
        field_label->setAlignment(icon.isNull() ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignCenter);
        if (!icon.isNull()) {
            field_label->setText(QString());
            field_label->setPixmap(icon.pixmap(QSize(14, 14)));
            field_label->setAccessibleName(label);
        }
        field_label->setStyleSheet(QStringLiteral("color:%1;background:transparent;font-size:10px;").arg(subtle_text_name));
        field_layout->addWidget(field_label);
        field_layout->addWidget(spin, 1);
        field->setMinimumWidth(0);
        field->setMaximumWidth(QWIDGETSIZE_MAX);
        field->setFixedHeight(20);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        return field;
    };

    spn_layer_w_ = mk_dspin(0.0, 9999.0, 10.0);
    spn_layer_h_ = mk_dspin(0.0, 9999.0, 10.0);
    spn_image_box_w_ = mk_dspin(0.0, 9999.0, 10.0);
    spn_image_box_h_ = mk_dspin(0.0, 9999.0, 10.0);
    chk_text_box_width_to_text_ = new FxmSwitch(fxm_tr("OBSTitles.TextBoxWidthToText"), inner);
    chk_text_box_height_to_text_ = new FxmSwitch(fxm_tr("OBSTitles.TextBoxHeightToText"), inner);
    style_checkbox(chk_text_box_width_to_text_);
    style_checkbox(chk_text_box_height_to_text_);
    spn_max_text_box_width_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_max_text_box_height_ = mk_dspin(1.0, 9999.0, 10.0);
    spn_rect_corner_tl_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_rect_corner_tr_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_rect_corner_br_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_rect_corner_bl_ = mk_dspin(0.0, 1000.0, 1.0);
    spn_corner_bevel_roundness_ = mk_dspin(-100.0, 100.0, 1.0);
    spn_corner_bevel_roundness_->setDecimals(0);
    spn_corner_bevel_roundness_->setSuffix(QStringLiteral("%"));
    spn_corner_bevel_roundness_->setToolTip(QStringLiteral("100 = round, 0 = flat bevel, -100 = inverted round"));
    cmb_shape_type_ = new QComboBox(inner);
    cmb_shape_type_->addItem("Rectangle", (int)ShapeType::Rectangle);
    cmb_shape_type_->addItem("Rounded Rectangle", (int)ShapeType::RoundedRectangle);
    cmb_shape_type_->addItem("Ellipse", (int)ShapeType::Ellipse);
    cmb_shape_type_->addItem("Triangle", (int)ShapeType::Triangle);
    cmb_shape_type_->addItem("Star", (int)ShapeType::Star);
    cmb_shape_type_->addItem("Polygon", (int)ShapeType::Polygon);
    cmb_shape_type_->addItem("Diamond", (int)ShapeType::Diamond);
    cmb_shape_type_->addItem(fxm_tr("OBSTitles.Path"), (int)ShapeType::Path);
    cmb_shape_type_->setFixedHeight(20);
    cmb_shape_type_->setStyleSheet(control_style);
    cmb_shape_type_->hide();
    spn_shape_points_ = new QSpinBox(inner); spn_shape_points_->setRange(3, 64); spn_shape_points_->setFixedHeight(20); spn_shape_points_->setStyleSheet(control_style);
    spn_shape_sides_ = new QSpinBox(inner); spn_shape_sides_->setRange(3, 64); spn_shape_sides_->setFixedHeight(20); spn_shape_sides_->setStyleSheet(control_style);
    spn_shape_inner_radius_ = mk_dspin(0.0, 1.0, 0.05); spn_shape_inner_radius_->setDecimals(2);
    spn_shape_outer_radius_ = mk_dspin(0.0, 1.0, 0.05); spn_shape_outer_radius_->setDecimals(2);
    spn_shape_roundness_ = mk_dspin(0.0, 1000.0, 1.0);
    btn_kf_width_ = mk_kf_button(fxm_tr("OBSTitles.ToggleWidthKeyframe"));
    btn_kf_width_->setToolTip(fxm_tr("OBSTitles.ToggleSizeKeyframe"));
    btn_kf_image_box_size_ = mk_kf_button(fxm_tr("OBSTitles.ToggleSizeKeyframe"));

    style_transform_spin(spn_layer_w_);
    style_transform_spin(spn_layer_h_);
    style_transform_spin(spn_image_box_w_);
    style_transform_spin(spn_image_box_h_);
    style_transform_spin(spn_rect_corner_tl_);
    style_transform_spin(spn_rect_corner_tr_);
    style_transform_spin(spn_rect_corner_br_);
    style_transform_spin(spn_rect_corner_bl_);
    style_transform_spin(spn_corner_bevel_roundness_);
    auto *field_width = make_shape_field(fxm_tr("OBSTitles.W"), spn_layer_w_);
    auto *field_height = make_shape_field(fxm_tr("OBSTitles.H"), spn_layer_h_);
    chk_size_lock_ = new FxmSwitch(rect_box_);
    chk_size_lock_->setText(QString());
    chk_size_lock_->setToolTip(fxm_tr("OBSTitles.LockAspectRatio"));
    chk_size_lock_->setFixedSize(30, 20);
    chk_size_lock_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *size_label = new QLabel(fxm_tr("OBSTitles.Size"), rect_box_);
    size_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    size_label->setStyleSheet(QStringLiteral("color:%1;background:transparent;font-size:10px;").arg(panel_text_name));
    shape_size_label_ = size_label;
    shape_size_field_w_ = field_width;
    shape_size_field_h_ = field_height;
    shape_grid->addWidget(size_label, 0, 0);
    shape_grid->addWidget(field_width, 0, 1);
    shape_grid->addWidget(field_height, 0, 2);
    shape_grid->addWidget(chk_size_lock_, 0, 3, Qt::AlignCenter);
    shape_grid->addWidget(make_keyframe_controls(btn_kf_width_, rect_box_),
                          0, 4, Qt::AlignRight | Qt::AlignVCenter);
    shape_layout->addLayout(shape_grid);

    row_rect_corners_ = new QWidget(rect_box_);
    row_rect_corners_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *corners_layout = new QGridLayout(row_rect_corners_);
    corners_layout->setContentsMargins(0, 0, 0, 0);
    corners_layout->setHorizontalSpacing(4);
    corners_layout->setVerticalSpacing(2);
    corners_layout->setColumnStretch(0, 1);
    corners_layout->setColumnStretch(1, 1);
    corners_layout->addWidget(make_shape_field(fxm_tr("OBSTitles.TL"), spn_rect_corner_tl_, obs_icon("corner-radius-tl.svg")), 0, 0);
    corners_layout->addWidget(make_shape_field(fxm_tr("OBSTitles.TR"), spn_rect_corner_tr_, obs_icon("corner-radius-tr.svg")), 0, 1);
    chk_corner_lock_ = new FxmSwitch(row_rect_corners_);
    chk_corner_lock_->setText(QString());
    chk_corner_lock_->setToolTip(fxm_tr("OBSTitles.SyncCornerRadii"));
    chk_corner_lock_->setFixedSize(30, 20);
    chk_corner_lock_->setStyleSheet(QStringLiteral("background:transparent;"));
    corners_layout->addWidget(chk_corner_lock_, 0, 2, 2, 1, Qt::AlignCenter);
    corners_layout->addWidget(make_shape_field(fxm_tr("OBSTitles.BL"), spn_rect_corner_bl_, obs_icon("corner-radius-bl.svg")), 1, 0);
    corners_layout->addWidget(make_shape_field(fxm_tr("OBSTitles.BR"), spn_rect_corner_br_, obs_icon("corner-radius-br.svg")), 1, 1);

    auto *shape_form_widget = new QWidget(rect_box_);
    shape_form_widget->setStyleSheet("background:transparent;");
    auto *rfl = new QFormLayout(shape_form_widget);
    style_form(rfl);
    add_form_row(rfl, "", chk_text_box_width_to_text_);
    add_form_row(rfl, fxm_tr("OBSTitles.MaxTextBoxWidthLabel"), spn_max_text_box_width_);
    add_form_row(rfl, "", chk_text_box_height_to_text_);
    add_form_row(rfl, fxm_tr("OBSTitles.MaxTextBoxHeightLabel"), spn_max_text_box_height_);
    add_form_row(rfl, fxm_tr("OBSTitles.CornerLabel"), row_rect_corners_);
    add_form_row(rfl, QStringLiteral("Corner Roundness"), spn_corner_bevel_roundness_);
    add_form_row(rfl, "Points", spn_shape_points_);
    add_form_row(rfl, "Sides", spn_shape_sides_);
    add_form_row(rfl, "Inner Radius", spn_shape_inner_radius_);
    add_form_row(rfl, "Outer Radius", spn_shape_outer_radius_);
    add_form_row(rfl, "Roundness", spn_shape_roundness_);
    cmb_fill_type_ = new QComboBox(inner);
    cmb_fill_type_->addItem(fxm_tr("OBSTitles.Solid"), 0);
    cmb_fill_type_->addItem(fxm_tr("OBSTitles.Gradient"), 1);
    cmb_fill_type_->setFixedHeight(20);
    cmb_fill_type_->setStyleSheet(control_style);
    row_fill_type_ = cmb_fill_type_;
    add_form_row(rfl, fxm_tr("OBSTitles.FillTypeLabel"), row_fill_type_);
    btn_fill_color_ = new QPushButton(inner);
    btn_kf_fill_color_ = mk_kf_button(fxm_tr("OBSTitles.ToggleFillColorKeyframe"));
    row_fill_color_ = with_kf(btn_fill_color_, btn_kf_fill_color_);
    add_form_row(rfl, fxm_tr("OBSTitles.ColorLabel"), row_fill_color_);
    shape_layout->addWidget(shape_form_widget);
    fxm_add_panel_section(vl, rect_box_, fxm_tr("OBSTitles.Shape"), btn_shape_defaults_);

    /* ── Gradient Properties ── */
    gradient_box_ = new QGroupBox(fxm_tr("OBSTitles.GradientProperties"), inner);
    gradient_box_->setStyleSheet(section_style);
    auto *gfl = new QFormLayout(gradient_box_);
    style_form(gfl);
    cmb_gradient_type_ = new QComboBox(inner);
    cmb_gradient_type_->addItem(fxm_tr("OBSTitles.LinearGradient"), 0);
    cmb_gradient_type_->addItem(fxm_tr("OBSTitles.RadialGradient"), 1);
    cmb_gradient_type_->addItem(fxm_tr("OBSTitles.ConicalGradient"), 2);
    cmb_gradient_type_->setFixedHeight(20);
    cmb_gradient_type_->setStyleSheet(control_style);
    cmb_gradient_spread_ = new QComboBox(inner);
    cmb_gradient_spread_->addItem(fxm_tr("OBSTitles.No"), 0);
    cmb_gradient_spread_->addItem(fxm_tr("OBSTitles.Repeat"), 2);
    cmb_gradient_spread_->addItem(fxm_tr("OBSTitles.Reflect"), 1);
    cmb_gradient_spread_->setFixedHeight(20);
    cmb_gradient_spread_->setStyleSheet(control_style);
    btn_gradient_start_color_ = new QPushButton(inner);
    btn_gradient_end_color_ = new QPushButton(inner);
    spn_gradient_start_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_end_pos_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_start_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_end_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_opacity_ = mk_dspin(0.0, 1.0, 0.01);
    spn_gradient_angle_ = mk_dspin(-360.0, 360.0, 1.0);
    spn_gradient_center_x_ = mk_dspin(-100.0, 100.0, 0.01);
    spn_gradient_center_y_ = mk_dspin(-100.0, 100.0, 0.01);
    spn_gradient_scale_ = mk_dspin(0.01, 100.0, 0.05);
    spn_gradient_focal_x_ = mk_dspin(-100.0, 100.0, 0.01);
    spn_gradient_focal_y_ = mk_dspin(-100.0, 100.0, 0.01);
    for (auto *spin : std::initializer_list<QDoubleSpinBox *>{spn_gradient_start_pos_, spn_gradient_end_pos_,
                                                               spn_gradient_start_opacity_, spn_gradient_end_opacity_,
                                                               spn_gradient_opacity_, spn_gradient_center_x_,
                                                               spn_gradient_center_y_, spn_gradient_scale_,
                                                               spn_gradient_focal_x_, spn_gradient_focal_y_})
        spin->setDecimals(2);
    spn_gradient_angle_->setSuffix("°");
    add_form_row(gfl, fxm_tr("OBSTitles.GradientTypeLabel"), cmb_gradient_type_);
    add_form_row(gfl, fxm_tr("OBSTitles.SpreadLabel"), cmb_gradient_spread_);
    add_form_row(gfl, fxm_tr("OBSTitles.StartColorLabel"), btn_gradient_start_color_);
    add_form_row(gfl, fxm_tr("OBSTitles.StartStopLabel"), spn_gradient_start_pos_);
    add_form_row(gfl, fxm_tr("OBSTitles.StartOpacityLabel"), spn_gradient_start_opacity_);
    add_form_row(gfl, fxm_tr("OBSTitles.EndColorLabel"), btn_gradient_end_color_);
    add_form_row(gfl, fxm_tr("OBSTitles.EndStopLabel"), spn_gradient_end_pos_);
    add_form_row(gfl, fxm_tr("OBSTitles.EndOpacityLabel"), spn_gradient_end_opacity_);
    add_form_row(gfl, fxm_tr("OBSTitles.OpacityLabel"), spn_gradient_opacity_);
    add_form_row(gfl, fxm_tr("OBSTitles.AngleLabel"), spn_gradient_angle_);
    add_form_row(gfl, fxm_tr("OBSTitles.CenterXLabel"), spn_gradient_center_x_);
    add_form_row(gfl, fxm_tr("OBSTitles.CenterYLabel"), spn_gradient_center_y_);
    add_form_row(gfl, fxm_tr("OBSTitles.ScaleLabel"), spn_gradient_scale_);
    add_form_row(gfl, fxm_tr("OBSTitles.FocalXLabel"), spn_gradient_focal_x_);
    add_form_row(gfl, fxm_tr("OBSTitles.FocalYLabel"), spn_gradient_focal_y_);
    fxm_add_panel_section(vl, gradient_box_);

    /* ── Image ── */
    image_box_ = new QWidget(inner);
    image_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *image_layout = new QVBoxLayout(image_box_);
    image_layout->setContentsMargins(5, 4, 5, 5);
    image_layout->setSpacing(2);

    auto *image_content = new QWidget(image_box_);
    image_content->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *image_content_layout = new QHBoxLayout(image_content);
    image_content_layout->setContentsMargins(0, 0, 0, 0);
    image_content_layout->setSpacing(10);

    lbl_image_preview_ = new QLabel(image_content);
    lbl_image_preview_->setFixedSize(104, 104);
    lbl_image_preview_->setAlignment(Qt::AlignCenter);
    lbl_image_preview_->setStyleSheet(QStringLiteral("background:transparent;"));
    set_image_preview_label(lbl_image_preview_, QString());
    image_content_layout->addWidget(lbl_image_preview_, 0, Qt::AlignTop);
