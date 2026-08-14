namespace {
constexpr int kTextAnimatorTreeKindRole = Qt::UserRole;
constexpr int kTextAnimatorTreeIndexRole = Qt::UserRole + 1;
constexpr int kTextAnimatorTreeKindProperty = 1;
constexpr int kTextAnimatorTreeKindSelector = 2;

QString text_animator_unit_label(TextAnimatorUnit unit)
{
    switch (unit) {
    case TextAnimatorUnit::Grapheme: return QStringLiteral("Grapheme clusters");
    case TextAnimatorUnit::Character: return QStringLiteral("Characters");
    case TextAnimatorUnit::CharacterExcludingSpaces: return QStringLiteral("Characters excluding spaces");
    case TextAnimatorUnit::Word: return QStringLiteral("Words");
    case TextAnimatorUnit::Line: return QStringLiteral("Lines");
    case TextAnimatorUnit::Paragraph: return QStringLiteral("Paragraphs");
    case TextAnimatorUnit::RichTextRun: return QStringLiteral("Rich-text runs");
    case TextAnimatorUnit::WholeLayer: return QStringLiteral("Whole text layer");
    case TextAnimatorUnit::Sentence: return QStringLiteral("Sentences");
    }
    return QStringLiteral("Grapheme clusters");
}

QString text_animator_property_label(TextAnimatorPropertyType type)
{
    switch (type) {
    case TextAnimatorPropertyType::Position: return QStringLiteral("Position");
    case TextAnimatorPropertyType::AnchorPoint: return QStringLiteral("Anchor Point");
    case TextAnimatorPropertyType::Scale: return QStringLiteral("Scale");
    case TextAnimatorPropertyType::Rotation: return QStringLiteral("Rotation");
    case TextAnimatorPropertyType::CharacterRotation: return QStringLiteral("Character Rotation");
    case TextAnimatorPropertyType::Skew: return QStringLiteral("Skew");
    case TextAnimatorPropertyType::SkewAxis: return QStringLiteral("Skew Axis");
    case TextAnimatorPropertyType::Opacity: return QStringLiteral("Opacity");
    case TextAnimatorPropertyType::FontSize: return QStringLiteral("Font Size");
    case TextAnimatorPropertyType::HorizontalScale: return QStringLiteral("Horizontal Scale");
    case TextAnimatorPropertyType::VerticalScale: return QStringLiteral("Vertical Scale");
    case TextAnimatorPropertyType::Tracking: return QStringLiteral("Tracking");
    case TextAnimatorPropertyType::BaselineShift: return QStringLiteral("Baseline Shift");
    case TextAnimatorPropertyType::FillColor: return QStringLiteral("Fill Color");
    case TextAnimatorPropertyType::FillOpacity: return QStringLiteral("Fill Opacity");
    case TextAnimatorPropertyType::StrokeColor: return QStringLiteral("Stroke Color");
    case TextAnimatorPropertyType::StrokeWidth: return QStringLiteral("Stroke Width");
    case TextAnimatorPropertyType::StrokeOpacity: return QStringLiteral("Stroke Opacity");
    case TextAnimatorPropertyType::LineSpacing: return QStringLiteral("Line Spacing");
    case TextAnimatorPropertyType::SpaceBeforeParagraph: return QStringLiteral("Space Before Paragraph");
    case TextAnimatorPropertyType::SpaceAfterParagraph: return QStringLiteral("Space After Paragraph");
    case TextAnimatorPropertyType::WordSpacing: return QStringLiteral("Word Spacing");
    case TextAnimatorPropertyType::TextBoxPositionOffset: return QStringLiteral("Text Box Position Offset");
    case TextAnimatorPropertyType::TextBoxScale: return QStringLiteral("Text Box Scale");
    case TextAnimatorPropertyType::LineAlignmentOffset: return QStringLiteral("Per-line Alignment Offset");
    case TextAnimatorPropertyType::Blur: return QStringLiteral("Blur");
    case TextAnimatorPropertyType::GlowIntensity: return QStringLiteral("Glow Intensity");
    case TextAnimatorPropertyType::ShadowOffset: return QStringLiteral("Shadow Offset");
    case TextAnimatorPropertyType::ShadowOpacity: return QStringLiteral("Shadow Opacity");
    case TextAnimatorPropertyType::CharacterVisibility: return QStringLiteral("Character Visibility");
    case TextAnimatorPropertyType::CharacterReveal: return QStringLiteral("Character Reveal");
    case TextAnimatorPropertyType::CharacterReplacement: return QStringLiteral("Character Replacement");
    case TextAnimatorPropertyType::ScrambleAmount: return QStringLiteral("Scramble Amount");
    }
    return QStringLiteral("Property");
}

QString text_selector_type_label(TextSelectorType type)
{
    switch (type) {
    case TextSelectorType::Range: return QStringLiteral("Range Selector");
    case TextSelectorType::Procedural: return QStringLiteral("Procedural Selector");
    case TextSelectorType::TextBased: return QStringLiteral("Text-based Selector");
    case TextSelectorType::Wiggly: return QStringLiteral("Wiggly Selector");
    case TextSelectorType::Staggered: return QStringLiteral("Staggered Selector");
    }
    return QStringLiteral("Selector");
}

QString text_selector_combination_label(TextSelectorCombinationMode mode)
{
    switch (mode) {
    case TextSelectorCombinationMode::Add: return QStringLiteral("Add");
    case TextSelectorCombinationMode::Subtract: return QStringLiteral("Subtract");
    case TextSelectorCombinationMode::Intersect: return QStringLiteral("Intersect");
    case TextSelectorCombinationMode::Difference: return QStringLiteral("Difference");
    case TextSelectorCombinationMode::Minimum: return QStringLiteral("Minimum");
    case TextSelectorCombinationMode::Maximum: return QStringLiteral("Maximum");
    case TextSelectorCombinationMode::Multiply: return QStringLiteral("Multiply");
    }
    return QStringLiteral("Intersect");
}

QString text_change_behaviour_label(TextChangeBehaviour value)
{
    switch (value) {
    case TextChangeBehaviour::Restart: return QStringLiteral("Restart when text changes");
    case TextChangeBehaviour::ContinueLocalTime: return QStringLiteral("Continue current local time");
    case TextChangeBehaviour::PreserveCompletion: return QStringLiteral("Preserve completion percentage");
    case TextChangeBehaviour::AnimateNewOnly: return QStringLiteral("Animate only newly added text");
    case TextChangeBehaviour::AnimateRemovedOnly: return QStringLiteral("Animate only removed text");
    case TextChangeBehaviour::AnimateChangedOnly: return QStringLiteral("Animate only changed text");
    case TextChangeBehaviour::ReevaluateFullText: return QStringLiteral("Re-evaluate full text");
    }
    return QStringLiteral("Re-evaluate full text");
}

TextAnimatorProperty make_default_text_animator_property(TextAnimatorPropertyType type,
                                                         const std::string &animator_id,
                                                         size_t ordinal)
{
    TextAnimatorProperty property;
    property.type = type;
    property.name = text_animator_property_label(type).toStdString();
    property.id = make_text_animator_id("property", animator_id + ':' + property.name,
                                        ordinal);
    property.value.name = property.name + ".value";
    property.secondary.name = property.name + ".secondary";
    property.tertiary.name = property.name + ".tertiary";
    property.quaternary.name = property.name + ".quaternary";
    switch (type) {
    case TextAnimatorPropertyType::Scale:
    case TextAnimatorPropertyType::TextBoxScale:
        property.value.static_value = 1.0;
        property.secondary.static_value = 1.0;
        break;
    case TextAnimatorPropertyType::HorizontalScale:
    case TextAnimatorPropertyType::VerticalScale:
    case TextAnimatorPropertyType::Opacity:
    case TextAnimatorPropertyType::FillOpacity:
    case TextAnimatorPropertyType::StrokeOpacity:
    case TextAnimatorPropertyType::ShadowOpacity:
    case TextAnimatorPropertyType::CharacterVisibility:
    case TextAnimatorPropertyType::CharacterReveal:
        property.value.static_value = 1.0;
        break;
    case TextAnimatorPropertyType::FillColor:
    case TextAnimatorPropertyType::StrokeColor:
        property.value.static_value = 255.0;
        property.secondary.static_value = 255.0;
        property.tertiary.static_value = 255.0;
        property.quaternary.static_value = 255.0;
        break;
    default:
        break;
    }
    return property;
}

TextSelector make_default_text_selector(TextSelectorType type,
                                        const std::string &animator_id,
                                        size_t ordinal)
{
    TextSelector selector;
    selector.type = type;
    selector.id = make_text_animator_id("selector", animator_id, ordinal);
    selector.name = text_selector_type_label(type).toStdString() + " " +
                    std::to_string(ordinal + 1);
    selector.combination = TextSelectorCombinationMode::Intersect;
    selector.based_on = TextAnimatorUnit::Grapheme;
    selector.start.static_value = 0.0;
    selector.end.static_value = 100.0;
    selector.amount.static_value = 100.0;
    selector.smoothness.static_value = 100.0;
    return selector;
}

bool text_animator_property_is_color(TextAnimatorPropertyType type)
{
    return type == TextAnimatorPropertyType::FillColor ||
           type == TextAnimatorPropertyType::StrokeColor;
}

int text_animator_property_component_count(TextAnimatorPropertyType type)
{
    if (text_animator_property_is_color(type)) return 4;
    switch (type) {
    case TextAnimatorPropertyType::Position:
    case TextAnimatorPropertyType::AnchorPoint:
    case TextAnimatorPropertyType::Scale:
    case TextAnimatorPropertyType::TextBoxPositionOffset:
    case TextAnimatorPropertyType::TextBoxScale:
    case TextAnimatorPropertyType::LineAlignmentOffset:
    case TextAnimatorPropertyType::ShadowOffset:
        return 2;
    default:
        return 1;
    }
}

QString text_animator_component_label(TextAnimatorPropertyType type, int component)
{
    if (text_animator_property_is_color(type)) {
        static const char *labels[] = {"Alpha", "Red", "Green", "Blue"};
        return QString::fromLatin1(labels[std::clamp(component, 0, 3)]);
    }
    if (text_animator_property_component_count(type) == 2)
        return component == 0 ? QStringLiteral("X") : QStringLiteral("Y");
    return QStringLiteral("Value");
}
}

void PropertiesPanel::build_text_animator_section(QWidget *parent,
                                                  QVBoxLayout *panel_layout)
{
    if (!parent || !panel_layout)
        return;
    const QPalette palette = qApp->palette();
    const QColor section_bg = palette.color(QPalette::Window).lightness() < 128
        ? palette.color(QPalette::Window).lighter(112)
        : palette.color(QPalette::Window).darker(104);
    text_animators_box_ = new QGroupBox(QStringLiteral("Text Animators"), parent);
    text_animators_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{background:%1;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}")
        .arg(section_bg.name(QColor::HexRgb)));
    auto *layout = new QVBoxLayout(text_animators_box_);
    layout->setContentsMargins(10, 28, 10, 10);
    layout->setSpacing(6);

    auto make_button = [this](const QString &text, const QString &tip) {
        auto *button = new QPushButton(text, text_animators_box_);
        button->setFixedHeight(20);
        button->setToolTip(tip);
        return button;
    };

    auto *preset_row = new QHBoxLayout();
    btn_text_animator_apply_preset_ = make_button(QStringLiteral("Apply Preset…"),
        QStringLiteral("Apply a built-in or imported generic Text Animator preset."));
    btn_text_animator_save_preset_ = make_button(QStringLiteral("Save as Preset…"),
        QStringLiteral("Save the selected editable animator as a reusable preset."));
    preset_row->addWidget(btn_text_animator_apply_preset_);
    preset_row->addWidget(btn_text_animator_save_preset_);
    layout->addLayout(preset_row);

    lst_text_animators_ = new QListWidget(text_animators_box_);
    lst_text_animators_->setMinimumHeight(90);
    lst_text_animators_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(lst_text_animators_);

    auto *animator_actions = new QHBoxLayout();
    btn_text_animator_add_ = make_button(QStringLiteral("+ Animator"),
                                         QStringLiteral("Add a new Text Animator."));
    btn_text_animator_duplicate_ = make_button(QStringLiteral("Duplicate"),
                                               QStringLiteral("Duplicate the selected animator."));
    btn_text_animator_delete_ = make_button(QStringLiteral("Delete"),
                                            QStringLiteral("Delete the selected animator."));
    btn_text_animator_up_ = make_button(QStringLiteral("↑"), QStringLiteral("Move animator up."));
    btn_text_animator_down_ = make_button(QStringLiteral("↓"), QStringLiteral("Move animator down."));
    animator_actions->addWidget(btn_text_animator_add_);
    animator_actions->addWidget(btn_text_animator_duplicate_);
    animator_actions->addWidget(btn_text_animator_delete_);
    animator_actions->addWidget(btn_text_animator_up_);
    animator_actions->addWidget(btn_text_animator_down_);
    layout->addLayout(animator_actions);

    auto *animator_form = new QFormLayout();
    animator_form->setContentsMargins(0, 0, 0, 0);
    animator_form->setHorizontalSpacing(6);
    animator_form->setVerticalSpacing(4);
    edt_text_animator_name_ = new QLineEdit(text_animators_box_);
    chk_text_animator_enabled_ = new FxmSwitch(QStringLiteral("Enabled"), text_animators_box_);
    chk_text_animator_expanded_ = new FxmSwitch(QStringLiteral("Expanded"), text_animators_box_);
    chk_text_animator_transform_as_unit_ = new FxmSwitch(
        QStringLiteral("Transform selected unit as a group"), text_animators_box_);
    chk_text_animator_transform_as_unit_->setToolTip(QStringLiteral(
        "Use one shared transform origin for every glyph in the selected word, "
        "line, sentence, paragraph, or rich-text run."));
    auto *state_row = new QWidget(text_animators_box_);
    auto *state_layout = new QHBoxLayout(state_row);
    state_layout->setContentsMargins(0, 0, 0, 0);
    state_layout->addWidget(chk_text_animator_enabled_);
    state_layout->addWidget(chk_text_animator_expanded_);
    state_layout->addStretch(1);
    cmb_text_animator_blend_ = new QComboBox(text_animators_box_);
    cmb_text_animator_blend_->addItem(QStringLiteral("Add"), (int)TextAnimatorBlendMode::Add);
    cmb_text_animator_blend_->addItem(QStringLiteral("Replace"), (int)TextAnimatorBlendMode::Replace);
    cmb_text_animator_blend_->addItem(QStringLiteral("Multiply"), (int)TextAnimatorBlendMode::Multiply);
    cmb_text_animator_granularity_ = new QComboBox(text_animators_box_);
    for (int i = (int)TextAnimatorUnit::Grapheme; i <= (int)TextAnimatorUnit::Sentence; ++i)
        cmb_text_animator_granularity_->addItem(
            text_animator_unit_label((TextAnimatorUnit)i), i);
    cmb_text_animator_change_behaviour_ = new QComboBox(text_animators_box_);
    for (int i = (int)TextChangeBehaviour::Restart;
         i <= (int)TextChangeBehaviour::ReevaluateFullText; ++i)
        cmb_text_animator_change_behaviour_->addItem(
            text_change_behaviour_label((TextChangeBehaviour)i), i);
    animator_form->addRow(QStringLiteral("Name"), edt_text_animator_name_);
    animator_form->addRow(QStringLiteral("State"), state_row);
    animator_form->addRow(QStringLiteral("Blend"), cmb_text_animator_blend_);
    animator_form->addRow(QStringLiteral("Granularity"), cmb_text_animator_granularity_);
    animator_form->addRow(QStringLiteral("Unit transform"),
                          chk_text_animator_transform_as_unit_);
    animator_form->addRow(QStringLiteral("On text change"), cmb_text_animator_change_behaviour_);
    layout->addLayout(animator_form);

    tree_text_animator_items_ = new QTreeWidget(text_animators_box_);
    tree_text_animator_items_->setHeaderHidden(true);
    tree_text_animator_items_->setMinimumHeight(130);
    tree_text_animator_items_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(tree_text_animator_items_);

    auto *item_actions = new QHBoxLayout();
    btn_text_animator_add_property_ = make_button(QStringLiteral("+ Property"),
                                                  QStringLiteral("Add an animatable property."));
    btn_text_animator_add_selector_ = make_button(QStringLiteral("+ Selector"),
                                                  QStringLiteral("Add a reusable selector."));
    btn_text_animator_delete_item_ = make_button(QStringLiteral("Delete"),
                                                 QStringLiteral("Delete the selected property or selector."));
    btn_text_animator_item_up_ = make_button(QStringLiteral("↑"), QStringLiteral("Move selected item up."));
    btn_text_animator_item_down_ = make_button(QStringLiteral("↓"), QStringLiteral("Move selected item down."));
    item_actions->addWidget(btn_text_animator_add_property_);
    item_actions->addWidget(btn_text_animator_add_selector_);
    item_actions->addWidget(btn_text_animator_delete_item_);
    item_actions->addWidget(btn_text_animator_item_up_);
    item_actions->addWidget(btn_text_animator_item_down_);
    layout->addLayout(item_actions);

    text_animator_item_editor_host_ = new QWidget(text_animators_box_);
    auto *editor_layout = new QVBoxLayout(text_animator_item_editor_host_);
    editor_layout->setContentsMargins(0, 0, 0, 0);
    editor_layout->setSpacing(0);
    layout->addWidget(text_animator_item_editor_host_);
    fxm_add_panel_section(panel_layout, text_animators_box_);

    connect(lst_text_animators_, &QListWidget::currentRowChanged,
            this, [this](int) {
                if (loading_values_) return;
                refresh_text_animator_controls();
            });
    connect(tree_text_animator_items_, &QTreeWidget::itemSelectionChanged,
            this, [this]() {
                if (loading_values_) return;
                rebuild_text_animator_item_editor();
            });

    connect(btn_text_animator_add_, &QPushButton::clicked, this, [this]() {
        if (!layer_ || loading_values_) return;
        TextAnimator animator;
        const size_t ordinal = layer_->text_animators.animators.size();
        animator.id = make_text_animator_id("animator", layer_->id, ordinal);
        animator.name = QStringLiteral("Animator %1").arg(ordinal + 1).toStdString();
        animator.granularity = TextAnimatorUnit::Grapheme;
        animator.change_behaviour = TextChangeBehaviour::ReevaluateFullText;
        layer_->text_animators.animators.push_back(std::move(animator));
        refresh_text_animator_controls();
        lst_text_animators_->setCurrentRow((int)ordinal);
        emit property_changed(true);
    });
    connect(btn_text_animator_duplicate_, &QPushButton::clicked, this, [this]() {
        TextAnimator *source = selected_text_animator();
        if (!layer_ || !source || loading_values_) return;
        TextAnimator copy = *source;
        const size_t ordinal = layer_->text_animators.animators.size();
        copy.id = make_text_animator_id("animator", source->id + ":duplicate", ordinal);
        copy.name += " Copy";
        /* A duplicate is an independent editable animator. Keeping the same
         * transition binding would make one descriptor drive two runtime
         * animators and double every visual property after the next sync. */
        copy.transition_managed = false;
        copy.transition_id.clear();
        copy.transition_binding_signature = 0;
        for (size_t i = 0; i < copy.properties.size(); ++i)
            copy.properties[i].id = make_text_animator_id("property", copy.id, i);
        for (size_t i = 0; i < copy.selectors.size(); ++i)
            copy.selectors[i].id = make_text_animator_id("selector", copy.id, i);
        layer_->text_animators.animators.push_back(std::move(copy));
        refresh_text_animator_controls();
        lst_text_animators_->setCurrentRow((int)ordinal);
        emit property_changed(true);
    });
    connect(btn_text_animator_delete_, &QPushButton::clicked, this, [this]() {
        if (!layer_ || loading_values_) return;
        const int row = lst_text_animators_ ? lst_text_animators_->currentRow() : -1;
        if (row < 0 || row >= (int)layer_->text_animators.animators.size()) return;
        const TextAnimator removed = layer_->text_animators.animators[(size_t)row];
        if (removed.transition_managed && !removed.transition_id.empty()) {
            layer_->transitions.erase(std::remove_if(
                layer_->transitions.begin(), layer_->transitions.end(),
                [&](const LayerTransition &transition) {
                    return transition.id == removed.transition_id;
                }), layer_->transitions.end());
        }
        layer_->text_animators.animators.erase(layer_->text_animators.animators.begin() + row);
        refresh_text_animator_controls();
        if (lst_text_animators_->count() > 0)
            lst_text_animators_->setCurrentRow(std::min(row, lst_text_animators_->count() - 1));
        emit property_changed(true);
    });
    auto move_animator = [this](int direction) {
        if (!layer_ || loading_values_) return;
        const int row = lst_text_animators_ ? lst_text_animators_->currentRow() : -1;
        const int target = row + direction;
        auto &items = layer_->text_animators.animators;
        if (row < 0 || target < 0 || row >= (int)items.size() || target >= (int)items.size()) return;
        std::swap(items[(size_t)row], items[(size_t)target]);
        refresh_text_animator_controls();
        lst_text_animators_->setCurrentRow(target);
        emit property_changed(true);
    };
    connect(btn_text_animator_up_, &QPushButton::clicked, this,
            [move_animator]() { move_animator(-1); });
    connect(btn_text_animator_down_, &QPushButton::clicked, this,
            [move_animator]() { move_animator(1); });

    connect(edt_text_animator_name_, &QLineEdit::editingFinished, this, [this]() {
        TextAnimator *animator = selected_text_animator();
        if (!animator || loading_values_) return;
        const std::string next = edt_text_animator_name_->text().trimmed().left(256).toStdString();
        if (next.empty() || next == animator->name) return;
        animator->name = next;
        notify_text_animator_changed(true);
    });
    connect(chk_text_animator_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        TextAnimator *animator = selected_text_animator();
        if (!animator || loading_values_) return;
        animator->enabled = enabled;
        if (animator->transition_managed && layer_) {
            const std::string transition_id = animator->transition_id;
            auto found = std::find_if(layer_->transitions.begin(),
                                      layer_->transitions.end(),
                [&](const LayerTransition &transition) {
                    return transition.id == transition_id;
                });
            if (found != layer_->transitions.end())
                found->enabled = enabled;
        }
        notify_text_animator_changed(true);
    });
    connect(chk_text_animator_expanded_, &QCheckBox::toggled, this, [this](bool expanded) {
        TextAnimator *animator = selected_text_animator();
        if (!animator || loading_values_) return;
        animator->expanded = expanded;
        notify_text_animator_changed(true);
    });
    connect(chk_text_animator_transform_as_unit_, &QCheckBox::toggled,
            this, [this](bool enabled) {
                TextAnimator *animator = selected_text_animator();
                if (!animator || loading_values_) return;
                animator->transform_as_unit = enabled;
                notify_text_animator_changed(false);
            });
    connect(cmb_text_animator_blend_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                TextAnimator *animator = selected_text_animator();
                if (!animator || loading_values_) return;
                animator->blend_mode = (TextAnimatorBlendMode)cmb_text_animator_blend_->currentData().toInt();
                notify_text_animator_changed(false);
            });
    connect(cmb_text_animator_granularity_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                TextAnimator *animator = selected_text_animator();
                if (!animator || loading_values_) return;
                animator->granularity = (TextAnimatorUnit)cmb_text_animator_granularity_->currentData().toInt();
                notify_text_animator_changed(false);
            });
    connect(cmb_text_animator_change_behaviour_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                TextAnimator *animator = selected_text_animator();
                if (!animator || loading_values_) return;
                animator->change_behaviour = (TextChangeBehaviour)cmb_text_animator_change_behaviour_->currentData().toInt();
                notify_text_animator_changed(false);
            });

    connect(btn_text_animator_add_property_, &QPushButton::clicked, this, [this]() {
        TextAnimator *animator = selected_text_animator();
        if (!animator || loading_values_) return;
        QMenu menu(btn_text_animator_add_property_);
        for (int value = (int)TextAnimatorPropertyType::Position;
             value <= (int)TextAnimatorPropertyType::ScrambleAmount; ++value) {
            QAction *action = menu.addAction(text_animator_property_label((TextAnimatorPropertyType)value));
            action->setData(value);
        }
        QAction *selected = menu.exec(btn_text_animator_add_property_->mapToGlobal(
            QPoint(0, btn_text_animator_add_property_->height())));
        if (!selected) return;
        const auto type = (TextAnimatorPropertyType)selected->data().toInt();
        animator->properties.push_back(make_default_text_animator_property(
            type, animator->id, animator->properties.size()));
        notify_text_animator_changed(true);
    });
    connect(btn_text_animator_add_selector_, &QPushButton::clicked, this, [this]() {
        TextAnimator *animator = selected_text_animator();
        if (!animator || loading_values_) return;
        QMenu menu(btn_text_animator_add_selector_);
        for (int value = (int)TextSelectorType::Range;
             value <= (int)TextSelectorType::Staggered; ++value) {
            QAction *action = menu.addAction(text_selector_type_label((TextSelectorType)value));
            action->setData(value);
        }
        QAction *selected = menu.exec(btn_text_animator_add_selector_->mapToGlobal(
            QPoint(0, btn_text_animator_add_selector_->height())));
        if (!selected) return;
        const auto type = (TextSelectorType)selected->data().toInt();
        animator->selectors.push_back(make_default_text_selector(
            type, animator->id, animator->selectors.size()));
        notify_text_animator_changed(true);
    });
    connect(btn_text_animator_delete_item_, &QPushButton::clicked, this, [this]() {
        TextAnimator *animator = selected_text_animator();
        QTreeWidgetItem *item = tree_text_animator_items_ ? tree_text_animator_items_->currentItem() : nullptr;
        if (!animator || !item || loading_values_) return;
        const int kind = item->data(0, kTextAnimatorTreeKindRole).toInt();
        const int index = item->data(0, kTextAnimatorTreeIndexRole).toInt();
        if (kind == kTextAnimatorTreeKindProperty && index >= 0 && index < (int)animator->properties.size())
            animator->properties.erase(animator->properties.begin() + index);
        else if (kind == kTextAnimatorTreeKindSelector && index >= 0 && index < (int)animator->selectors.size())
            animator->selectors.erase(animator->selectors.begin() + index);
        else
            return;
        notify_text_animator_changed(true);
    });
    auto move_item = [this](int direction) {
        TextAnimator *animator = selected_text_animator();
        QTreeWidgetItem *item = tree_text_animator_items_ ? tree_text_animator_items_->currentItem() : nullptr;
        if (!animator || !item || loading_values_) return;
        const int kind = item->data(0, kTextAnimatorTreeKindRole).toInt();
        const int index = item->data(0, kTextAnimatorTreeIndexRole).toInt();
        const int target = index + direction;
        if (kind == kTextAnimatorTreeKindProperty) {
            if (index < 0 || target < 0 || index >= (int)animator->properties.size() || target >= (int)animator->properties.size()) return;
            std::swap(animator->properties[(size_t)index], animator->properties[(size_t)target]);
        } else if (kind == kTextAnimatorTreeKindSelector) {
            if (index < 0 || target < 0 || index >= (int)animator->selectors.size() || target >= (int)animator->selectors.size()) return;
            std::swap(animator->selectors[(size_t)index], animator->selectors[(size_t)target]);
        } else return;
        notify_text_animator_changed(true);
    };
    connect(btn_text_animator_item_up_, &QPushButton::clicked, this,
            [move_item]() { move_item(-1); });
    connect(btn_text_animator_item_down_, &QPushButton::clicked, this,
            [move_item]() { move_item(1); });

    /* Built-ins are converted to concrete editable structures immediately.
     * The existing Effects & Presets browser keeps its names/categories and
     * reaches the same factory through apply_transition_preset_to_layer(). */
    connect(btn_text_animator_apply_preset_, &QPushButton::clicked, this, [this]() {
        if (!layer_ || loading_values_) return;
        QMenu menu(btn_text_animator_apply_preset_);
        struct Builtin { const char *id; const char *name; bool exit_animation; };
        const Builtin builtins[] = {
            {"text.fade", "Fade In", false}, {"text.slide-in", "Slide In", false},
            {"text.scale", "Scale In", false}, {"text.blur", "Blur In", false},
            {"text.wipe", "Character Reveal", false},
            {"text.blur-slide-in", "Blur Slide In", false},
            {"text.fade", "Fade Out", true}, {"text.slide-in", "Slide Out", true},
            {"text.scale", "Scale Out", true}, {"text.blur", "Blur Out", true},
            {"text.wipe", "Character Disappear", true},
            {"text.blur-slide-in", "Blur Slide Out", true}
        };
        for (int i = 0; i < (int)(sizeof(builtins) / sizeof(builtins[0])); ++i) {
            QAction *action = menu.addAction(QString::fromUtf8(builtins[i].name));
            action->setData(i);
        }
        menu.addSeparator();
        QAction *import_action = menu.addAction(QStringLiteral("Import Text Animator Preset…"));
        import_action->setData(-1);
        QAction *selected = menu.exec(btn_text_animator_apply_preset_->mapToGlobal(
            QPoint(0, btn_text_animator_apply_preset_->height())));
        if (!selected) return;
        const int selected_index = selected->data().toInt();
        TextAnimator animator;
        if (selected_index < 0) {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Import Text Animator Preset"), QString(),
                QStringLiteral("FXM Text Animator Preset (*.fxmtextanim *.json)"));
            if (path.isEmpty()) return;
            TextAnimatorPresetMetadata metadata;
            std::string error;
            if (!load_text_animator_preset_file(path.toStdString(), &metadata,
                                                &animator, &error)) {
                QMessageBox::warning(this, QStringLiteral("Import Text Animator Preset"),
                                     QString::fromStdString(error));
                return;
            }
            if (!metadata.name.empty()) animator.name = metadata.name;
        } else {
            const Builtin &preset = builtins[selected_index];
            const double layer_duration = std::max(1.0 / 240.0,
                                                   layer_->out_time - layer_->in_time);
            animator = make_builtin_text_animator_preset(
                preset.id, std::min(0.75, layer_duration),
                preset.exit_animation, layer_duration);
            animator.name = preset.name;
        }
        reseed_text_animator_ids(animator, layer_->id,
                                 layer_->text_animators.animators.size());
        layer_->text_animators.animators.push_back(std::move(animator));
        notify_text_animator_changed(true);
        lst_text_animators_->setCurrentRow(lst_text_animators_->count() - 1);
    });
    connect(btn_text_animator_save_preset_, &QPushButton::clicked, this, [this]() {
        TextAnimator *animator = selected_text_animator();
        if (!animator || loading_values_) return;
        QString suggested = QString::fromStdString(animator->name).trimmed();
        if (suggested.isEmpty()) suggested = QStringLiteral("Text Animator");
        suggested.replace(QRegularExpression(QStringLiteral("[\\/:*?\"<>|]")), QStringLiteral("-"));
        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Text Animator Preset"),
            suggested + QStringLiteral(".fxmtextanim"),
            QStringLiteral("FXM Text Animator Preset (*.fxmtextanim)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".fxmtextanim"), Qt::CaseInsensitive))
            path += QStringLiteral(".fxmtextanim");
        TextAnimatorPresetMetadata metadata;
        metadata.name = animator->name;
        metadata.category = "Custom";
        metadata.description = "Editable FXM Text Animator preset";
        metadata.identifier = animator->preset_id;
        std::string error;
        if (!save_text_animator_preset_file(path.toStdString(), metadata,
                                            *animator, &error)) {
            QMessageBox::warning(this, QStringLiteral("Save Text Animator Preset"),
                                 QString::fromStdString(error));
        }
    });
}

TextAnimator *PropertiesPanel::selected_text_animator()
{
    if (!layer_ || !lst_text_animators_)
        return nullptr;
    const int row = lst_text_animators_->currentRow();
    if (row < 0 || row >= (int)layer_->text_animators.animators.size())
        return nullptr;
    return &layer_->text_animators.animators[(size_t)row];
}

TextAnimatorProperty *PropertiesPanel::selected_text_animator_property()
{
    TextAnimator *animator = selected_text_animator();
    QTreeWidgetItem *item = tree_text_animator_items_ ? tree_text_animator_items_->currentItem() : nullptr;
    if (!animator || !item ||
        item->data(0, kTextAnimatorTreeKindRole).toInt() != kTextAnimatorTreeKindProperty)
        return nullptr;
    const int index = item->data(0, kTextAnimatorTreeIndexRole).toInt();
    return index >= 0 && index < (int)animator->properties.size()
        ? &animator->properties[(size_t)index] : nullptr;
}

TextSelector *PropertiesPanel::selected_text_animator_selector()
{
    TextAnimator *animator = selected_text_animator();
    QTreeWidgetItem *item = tree_text_animator_items_ ? tree_text_animator_items_->currentItem() : nullptr;
    if (!animator || !item ||
        item->data(0, kTextAnimatorTreeKindRole).toInt() != kTextAnimatorTreeKindSelector)
        return nullptr;
    const int index = item->data(0, kTextAnimatorTreeIndexRole).toInt();
    return index >= 0 && index < (int)animator->selectors.size()
        ? &animator->selectors[(size_t)index] : nullptr;
}

void PropertiesPanel::notify_text_animator_changed(bool rebuild)
{
    if (loading_values_)
        return;
    if (rebuild)
        refresh_text_animator_controls();
    else
        rebuild_text_animator_item_editor();
    emit property_changed(true);
}

void PropertiesPanel::refresh_text_animator_controls()
{
    if (!text_animators_box_ || !lst_text_animators_ || !tree_text_animator_items_)
        return;
    const bool previous_loading = loading_values_;
    loading_values_ = true;
    const int requested_row = lst_text_animators_->currentRow();
    QString selected_item_id;
    if (QTreeWidgetItem *selected = tree_text_animator_items_->currentItem())
        selected_item_id = selected->data(0, Qt::UserRole + 2).toString();

    {
        QSignalBlocker blocker(lst_text_animators_);
        lst_text_animators_->clear();
        if (layer_) {
            for (const TextAnimator &animator : layer_->text_animators.animators) {
                auto *item = new QListWidgetItem(QString::fromStdString(animator.name),
                                                 lst_text_animators_);
                item->setIcon(obs_icon(animator.enabled ? "layer-visible.svg" : "layer-hidden.svg"));
                item->setData(Qt::UserRole, QString::fromStdString(animator.id));
                if (!animator.preset_id.empty())
                    item->setToolTip(QStringLiteral("Preset: %1").arg(
                        QString::fromStdString(animator.preset_id)));
            }
        }
        if (lst_text_animators_->count() > 0)
            lst_text_animators_->setCurrentRow(std::clamp(requested_row, 0,
                                              lst_text_animators_->count() - 1));
    }

    TextAnimator *animator = selected_text_animator();
    const bool has_animator = animator != nullptr;
    for (QWidget *widget : {static_cast<QWidget *>(edt_text_animator_name_),
                            static_cast<QWidget *>(chk_text_animator_enabled_),
                            static_cast<QWidget *>(chk_text_animator_expanded_),
                            static_cast<QWidget *>(chk_text_animator_transform_as_unit_),
                            static_cast<QWidget *>(cmb_text_animator_blend_),
                            static_cast<QWidget *>(cmb_text_animator_granularity_),
                            static_cast<QWidget *>(cmb_text_animator_change_behaviour_),
                            static_cast<QWidget *>(btn_text_animator_duplicate_),
                            static_cast<QWidget *>(btn_text_animator_delete_),
                            static_cast<QWidget *>(btn_text_animator_add_property_),
                            static_cast<QWidget *>(btn_text_animator_add_selector_)})
        if (widget) widget->setEnabled(has_animator);
    if (animator) {
        edt_text_animator_name_->setText(QString::fromStdString(animator->name));
        chk_text_animator_enabled_->setChecked(animator->enabled);
        chk_text_animator_expanded_->setChecked(animator->expanded);
        chk_text_animator_transform_as_unit_->setChecked(
            animator->transform_as_unit);
        cmb_text_animator_blend_->setCurrentIndex(std::max(0,
            cmb_text_animator_blend_->findData((int)animator->blend_mode)));
        cmb_text_animator_granularity_->setCurrentIndex(std::max(0,
            cmb_text_animator_granularity_->findData((int)animator->granularity)));
        cmb_text_animator_change_behaviour_->setCurrentIndex(std::max(0,
            cmb_text_animator_change_behaviour_->findData((int)animator->change_behaviour)));
    } else {
        edt_text_animator_name_->clear();
    }

    {
        QSignalBlocker blocker(tree_text_animator_items_);
        tree_text_animator_items_->clear();
        if (animator) {
            auto *properties_root = new QTreeWidgetItem(tree_text_animator_items_,
                                                        QStringList{QStringLiteral("Properties")});
            auto *selectors_root = new QTreeWidgetItem(tree_text_animator_items_,
                                                       QStringList{QStringLiteral("Selectors")});
            for (size_t i = 0; i < animator->properties.size(); ++i) {
                const TextAnimatorProperty &property = animator->properties[i];
                auto *item = new QTreeWidgetItem(properties_root,
                    QStringList{QString::fromStdString(property.name)});
                item->setData(0, kTextAnimatorTreeKindRole, kTextAnimatorTreeKindProperty);
                item->setData(0, kTextAnimatorTreeIndexRole, (int)i);
                item->setData(0, Qt::UserRole + 2, QString::fromStdString(property.id));
                item->setIcon(0, obs_icon(property.enabled ? "layer-visible.svg" : "layer-hidden.svg"));
                if (QString::fromStdString(property.id) == selected_item_id)
                    tree_text_animator_items_->setCurrentItem(item);
            }
            for (size_t i = 0; i < animator->selectors.size(); ++i) {
                const TextSelector &selector = animator->selectors[i];
                auto *item = new QTreeWidgetItem(selectors_root,
                    QStringList{QString::fromStdString(selector.name)});
                item->setData(0, kTextAnimatorTreeKindRole, kTextAnimatorTreeKindSelector);
                item->setData(0, kTextAnimatorTreeIndexRole, (int)i);
                item->setData(0, Qt::UserRole + 2, QString::fromStdString(selector.id));
                item->setIcon(0, obs_icon(selector.enabled ? "layer-visible.svg" : "layer-hidden.svg"));
                if (QString::fromStdString(selector.id) == selected_item_id)
                    tree_text_animator_items_->setCurrentItem(item);
            }
            properties_root->setExpanded(animator->expanded);
            selectors_root->setExpanded(animator->expanded);
            if (!tree_text_animator_items_->currentItem()) {
                if (properties_root->childCount() > 0)
                    tree_text_animator_items_->setCurrentItem(properties_root->child(0));
                else if (selectors_root->childCount() > 0)
                    tree_text_animator_items_->setCurrentItem(selectors_root->child(0));
            }
        }
    }
    loading_values_ = previous_loading;
    rebuild_text_animator_item_editor();
}

void PropertiesPanel::rebuild_text_animator_item_editor()
{
    if (!text_animator_item_editor_host_)
        return;
    auto *host_layout = qobject_cast<QVBoxLayout *>(text_animator_item_editor_host_->layout());
    if (text_animator_item_editor_) {
        if (host_layout) host_layout->removeWidget(text_animator_item_editor_);
        delete text_animator_item_editor_;
        text_animator_item_editor_ = nullptr;
    }
    if (!host_layout)
        return;
    text_animator_item_editor_ = new QWidget(text_animator_item_editor_host_);
    auto *form = new QFormLayout(text_animator_item_editor_);
    form->setContentsMargins(0, 6, 0, 0);
    form->setHorizontalSpacing(6);
    form->setVerticalSpacing(4);
    host_layout->addWidget(text_animator_item_editor_);

    TextAnimator *animator = selected_text_animator();
    TextAnimatorProperty *property = selected_text_animator_property();
    TextSelector *selector = selected_text_animator_selector();
    const bool has_item = property || selector;
    for (QWidget *widget : {static_cast<QWidget *>(btn_text_animator_delete_item_),
                            static_cast<QWidget *>(btn_text_animator_item_up_),
                            static_cast<QWidget *>(btn_text_animator_item_down_)})
        if (widget) widget->setEnabled(has_item);
    if (!animator || !has_item) {
        form->addRow(new QLabel(QStringLiteral("Select a property or selector to edit."),
                                text_animator_item_editor_));
        return;
    }

    const std::string animator_id = animator->id;
    auto find_animator = [this, animator_id]() -> TextAnimator * {
        if (!layer_) return nullptr;
        auto found = std::find_if(layer_->text_animators.animators.begin(),
                                  layer_->text_animators.animators.end(),
            [&](const TextAnimator &candidate) { return candidate.id == animator_id; });
        return found == layer_->text_animators.animators.end() ? nullptr : &*found;
    };
    auto local_time = [this]() {
        return layer_ ? std::max(0.0, playhead_ - layer_->in_time) : 0.0;
    };
    auto make_spin = [this](double value, double minimum = -100000.0,
                            double maximum = 100000.0, int decimals = 3) {
        auto *spin = new QDoubleSpinBox(text_animator_item_editor_);
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setSingleStep(decimals == 0 ? 1.0 : 0.1);
        spin->setValue(value);
        spin->setFixedHeight(20);
        return spin;
    };
    auto add_animated_row = [this, form, find_animator, local_time, make_spin](
        const QString &label, const std::string &item_id,
        bool selector_item, AnimatedProperty TextAnimatorProperty::*property_member,
        AnimatedProperty TextSelector::*selector_member,
        double minimum = -100000.0, double maximum = 100000.0,
        int decimals = 3) {
        TextAnimator *current_animator = find_animator();
        AnimatedProperty *animated = nullptr;
        if (current_animator) {
            if (selector_item) {
                auto found = std::find_if(current_animator->selectors.begin(),
                                          current_animator->selectors.end(),
                    [&](const TextSelector &candidate) { return candidate.id == item_id; });
                if (found != current_animator->selectors.end() && selector_member)
                    animated = &((*found).*selector_member);
            } else {
                auto found = std::find_if(current_animator->properties.begin(),
                                          current_animator->properties.end(),
                    [&](const TextAnimatorProperty &candidate) { return candidate.id == item_id; });
                if (found != current_animator->properties.end() && property_member)
                    animated = &((*found).*property_member);
            }
        }
        if (!animated) return;
        auto *row = new QWidget(text_animator_item_editor_);
        auto *row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);
        auto *spin = make_spin(animated->evaluate(local_time()), minimum, maximum,
                               decimals);
        auto *key_button = new QPushButton(row);
        key_button->setIcon(fxm_keyframe_diamond_icon(keyframe_at_time(*animated,
                                                                    local_time())));
        fxm_style_keyframe_button(key_button);
        auto resolve = [find_animator, item_id, selector_item,
                        property_member, selector_member]() -> AnimatedProperty * {
            TextAnimator *resolved_animator = find_animator();
            if (!resolved_animator) return nullptr;
            if (selector_item) {
                auto found = std::find_if(resolved_animator->selectors.begin(),
                                          resolved_animator->selectors.end(),
                    [&](const TextSelector &candidate) { return candidate.id == item_id; });
                return found != resolved_animator->selectors.end() && selector_member
                    ? &((*found).*selector_member) : nullptr;
            }
            auto found = std::find_if(resolved_animator->properties.begin(),
                                      resolved_animator->properties.end(),
                [&](const TextAnimatorProperty &candidate) { return candidate.id == item_id; });
            return found != resolved_animator->properties.end() && property_member
                ? &((*found).*property_member) : nullptr;
        };
        auto *navigation = fxm_make_keyframe_controls(
            key_button, row,
            [resolve]() {
                std::vector<double> times;
                if (const AnimatedProperty *target = resolve())
                    for (const Keyframe &key : target->keyframes)
                        times.push_back(key.time);
                return times;
            }, local_time,
            [this](double time) {
                if (layer_)
                    emit keyframe_navigation_requested(layer_->in_time + time);
            });
        row_layout->addWidget(spin, 1);
        row_layout->addWidget(navigation);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, spin, resolve, local_time](double value) {
                    if (loading_values_) return;
                    if (AnimatedProperty *target = resolve()) {
                        set_animated_value(*target, local_time(), value);
                        emit live_visual_changed();
                    }
                });
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this]() {
            if (!loading_values_) emit property_changed(true);
        });
        connect(key_button, &QPushButton::clicked, this,
                [this, spin, resolve, local_time]() {
                    if (loading_values_) return;
                    if (AnimatedProperty *target = resolve()) {
                        toggle_keyframe(*target, local_time(), spin->value());
                        rebuild_text_animator_item_editor();
                        emit property_changed(true);
                    }
                });
        key_button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(key_button, &QPushButton::customContextMenuRequested, this,
                [this, key_button, resolve](const QPoint &position) {
                    AnimatedProperty *target = resolve();
                    if (!target) return;
                    QMenu menu(key_button);
                    QAction *remove_all = menu.addAction(QStringLiteral("Delete All Keyframes"));
                    remove_all->setEnabled(!target->keyframes.empty());
                    if (menu.exec(key_button->mapToGlobal(position)) == remove_all &&
                        !target->keyframes.empty()) {
                        target->keyframes.clear();
                        rebuild_text_animator_item_editor();
                        emit property_changed(true);
                    }
                });
        form->addRow(label, row);
    };

    if (property) {
        const std::string property_id = property->id;
        auto *enabled = new FxmSwitch(QStringLiteral("Enabled"), text_animator_item_editor_);
        enabled->setChecked(property->enabled);
        form->addRow(QString::fromStdString(property->name), enabled);
        connect(enabled, &QCheckBox::toggled, this,
                [this, find_animator, property_id](bool checked) {
                    if (loading_values_) return;
                    if (TextAnimator *resolved = find_animator()) {
                        auto found = std::find_if(resolved->properties.begin(), resolved->properties.end(),
                            [&](const TextAnimatorProperty &candidate) { return candidate.id == property_id; });
                        if (found != resolved->properties.end()) {
                            found->enabled = checked;
                            notify_text_animator_changed(true);
                        }
                    }
                });
        AnimatedProperty TextAnimatorProperty::*members[] = {
            &TextAnimatorProperty::value, &TextAnimatorProperty::secondary,
            &TextAnimatorProperty::tertiary, &TextAnimatorProperty::quaternary};
        const int components = text_animator_property_component_count(property->type);
        for (int component = 0; component < components; ++component) {
            const bool color = text_animator_property_is_color(property->type);
            add_animated_row(text_animator_component_label(property->type, component),
                property_id, false, members[component], nullptr,
                color ? 0.0 : -100000.0, color ? 255.0 : 100000.0,
                color ? 0 : 3);
        }
        return;
    }

    const std::string selector_id = selector->id;
    auto resolve_selector = [find_animator, selector_id]() -> TextSelector * {
        TextAnimator *resolved_animator = find_animator();
        if (!resolved_animator) return nullptr;
        auto found = std::find_if(resolved_animator->selectors.begin(),
                                  resolved_animator->selectors.end(),
            [&](const TextSelector &candidate) { return candidate.id == selector_id; });
        return found == resolved_animator->selectors.end() ? nullptr : &*found;
    };
    auto *name = new QLineEdit(QString::fromStdString(selector->name),
                               text_animator_item_editor_);
    form->addRow(QStringLiteral("Name"), name);
    connect(name, &QLineEdit::editingFinished, this,
            [this, name, resolve_selector]() {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    const std::string value = name->text().trimmed().left(256).toStdString();
                    if (!value.empty() && value != resolved->name) {
                        resolved->name = value;
                        notify_text_animator_changed(true);
                    }
                }
            });
    auto *enabled = new FxmSwitch(QStringLiteral("Enabled"), text_animator_item_editor_);
    enabled->setChecked(selector->enabled);
    form->addRow(QStringLiteral("State"), enabled);
    connect(enabled, &QCheckBox::toggled, this,
            [this, resolve_selector](bool checked) {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    resolved->enabled = checked;
                    notify_text_animator_changed(true);
                }
            });
    auto *type_combo = new QComboBox(text_animator_item_editor_);
    for (int i = (int)TextSelectorType::Range; i <= (int)TextSelectorType::Staggered; ++i)
        type_combo->addItem(text_selector_type_label((TextSelectorType)i), i);
    type_combo->setCurrentIndex(type_combo->findData((int)selector->type));
    form->addRow(QStringLiteral("Type"), type_combo);
    connect(type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, type_combo, resolve_selector](int) {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    resolved->type = (TextSelectorType)type_combo->currentData().toInt();
                    notify_text_animator_changed(true);
                }
            });
    auto *based_on = new QComboBox(text_animator_item_editor_);
    for (int i = (int)TextAnimatorUnit::Grapheme; i <= (int)TextAnimatorUnit::Sentence; ++i)
        based_on->addItem(text_animator_unit_label((TextAnimatorUnit)i), i);
    based_on->setCurrentIndex(based_on->findData((int)selector->based_on));
    form->addRow(QStringLiteral("Based On"), based_on);
    connect(based_on, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, based_on, resolve_selector](int) {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    resolved->based_on = (TextAnimatorUnit)based_on->currentData().toInt();
                    emit property_changed(true);
                }
            });
    auto *combination = new QComboBox(text_animator_item_editor_);
    for (int i = (int)TextSelectorCombinationMode::Add;
         i <= (int)TextSelectorCombinationMode::Multiply; ++i)
        combination->addItem(text_selector_combination_label((TextSelectorCombinationMode)i), i);
    combination->setCurrentIndex(combination->findData((int)selector->combination));
    form->addRow(QStringLiteral("Combine"), combination);
    connect(combination, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, combination, resolve_selector](int) {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    resolved->combination = (TextSelectorCombinationMode)combination->currentData().toInt();
                    emit property_changed(true);
                }
            });
    auto *direction = new QComboBox(text_animator_item_editor_);
    direction->addItem(QStringLiteral("Forward"), (int)TextSelectorDirection::Forward);
    direction->addItem(QStringLiteral("Reverse"), (int)TextSelectorDirection::Reverse);
    direction->addItem(QStringLiteral("Bidirectional"), (int)TextSelectorDirection::Bidirectional);
    direction->setCurrentIndex(direction->findData((int)selector->direction));
    form->addRow(QStringLiteral("Direction"), direction);
    connect(direction, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, direction, resolve_selector](int) {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    resolved->direction = (TextSelectorDirection)direction->currentData().toInt();
                    emit property_changed(true);
                }
            });
    auto *invert = new FxmSwitch(QStringLiteral("Invert Selection"), text_animator_item_editor_);
    invert->setChecked(selector->invert);
    form->addRow(QString(), invert);
    connect(invert, &QCheckBox::toggled, this,
            [this, resolve_selector](bool checked) {
                if (loading_values_) return;
                if (TextSelector *resolved = resolve_selector()) {
                    resolved->invert = checked;
                    emit property_changed(true);
                }
            });

    if (selector->type == TextSelectorType::Range) {
        auto *units = new QComboBox(text_animator_item_editor_);
        units->addItem(QStringLiteral("Percentage"), (int)TextRangeUnits::Percentage);
        units->addItem(QStringLiteral("Index"), (int)TextRangeUnits::Index);
        units->setCurrentIndex(units->findData((int)selector->range_units));
        form->addRow(QStringLiteral("Units"), units);
        connect(units, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, units, resolve_selector](int) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->range_units = (TextRangeUnits)units->currentData().toInt();
                        emit property_changed(true);
                    }
                });
        auto *shape = new QComboBox(text_animator_item_editor_);
        const char *shape_names[] = {"Square", "Ramp Up", "Ramp Down", "Triangle", "Round", "Smooth"};
        for (int i = 0; i <= (int)TextRangeShape::Smooth; ++i)
            shape->addItem(QString::fromLatin1(shape_names[i]), i);
        shape->setCurrentIndex(shape->findData((int)selector->range_shape));
        form->addRow(QStringLiteral("Shape"), shape);
        connect(shape, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, shape, resolve_selector](int) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->range_shape = (TextRangeShape)shape->currentData().toInt();
                        emit property_changed(true);
                    }
                });
        add_animated_row(QStringLiteral("Start"), selector_id, true, nullptr, &TextSelector::start);
        add_animated_row(QStringLiteral("End"), selector_id, true, nullptr, &TextSelector::end);
        add_animated_row(QStringLiteral("Offset"), selector_id, true, nullptr, &TextSelector::offset);
        add_animated_row(QStringLiteral("Amount"), selector_id, true, nullptr, &TextSelector::amount);
        add_animated_row(QStringLiteral("Ease High"), selector_id, true, nullptr, &TextSelector::ease_high, -100.0, 100.0);
        add_animated_row(QStringLiteral("Ease Low"), selector_id, true, nullptr, &TextSelector::ease_low, -100.0, 100.0);
        add_animated_row(QStringLiteral("Smoothness"), selector_id, true, nullptr, &TextSelector::smoothness, 0.0, 100.0);
        auto *randomize = new FxmSwitch(QStringLiteral("Randomize Order"), text_animator_item_editor_);
        randomize->setChecked(selector->randomize_order);
        auto *seed = new QSpinBox(text_animator_item_editor_);
        seed->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        seed->setValue(selector->random_seed);
        form->addRow(QString(), randomize);
        form->addRow(QStringLiteral("Random Seed"), seed);
        connect(randomize, &QCheckBox::toggled, this,
                [this, resolve_selector](bool checked) {
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->randomize_order = checked;
                        emit property_changed(true);
                    }
                });
        connect(seed, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, resolve_selector](int value) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->random_seed = value;
                        emit property_changed(true);
                    }
                });
    } else if (selector->type == TextSelectorType::Staggered) {
        add_animated_row(QStringLiteral("Completion"), selector_id, true, nullptr,
                         &TextSelector::completion, 0.0, 100.0);
        add_animated_row(QStringLiteral("Stagger"), selector_id, true, nullptr,
                         &TextSelector::stagger_percent, 0.0, 95.0);
        auto *mode = new QComboBox(text_animator_item_editor_);
        mode->addItem(QStringLiteral("Entrance"), (int)TextStaggerMode::Entrance);
        mode->addItem(QStringLiteral("Exit"), (int)TextStaggerMode::Exit);
        mode->setCurrentIndex(mode->findData((int)selector->stagger_mode));
        form->addRow(QStringLiteral("Mode"), mode);
        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, mode, resolve_selector](int) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->stagger_mode = (TextStaggerMode)mode->currentData().toInt();
                        emit property_changed(true);
                    }
                });
        auto *easing = new QComboBox(text_animator_item_editor_);
        easing->addItem(QStringLiteral("Linear"), (int)EasingType::Linear);
        easing->addItem(QStringLiteral("Ease In"), (int)EasingType::EaseIn);
        easing->addItem(QStringLiteral("Ease Out"), (int)EasingType::EaseOut);
        easing->addItem(QStringLiteral("Ease In/Out"), (int)EasingType::EaseInOut);
        easing->addItem(QStringLiteral("Bezier"), (int)EasingType::Bezier);
        easing->addItem(QStringLiteral("Hold"), (int)EasingType::Hold);
        easing->setCurrentIndex(easing->findData((int)selector->unit_easing));
        form->addRow(QStringLiteral("Unit Easing"), easing);
        connect(easing, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, easing, resolve_selector](int) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->unit_easing = (EasingType)easing->currentData().toInt();
                        emit property_changed(true);
                    }
                });
        auto *exclude_spaces = new FxmSwitch(QStringLiteral("Exclude whitespace"),
                                             text_animator_item_editor_);
        exclude_spaces->setChecked(selector->exclude_whitespace);
        form->addRow(QString(), exclude_spaces);
        connect(exclude_spaces, &QCheckBox::toggled, this,
                [this, resolve_selector](bool checked) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->exclude_whitespace = checked;
                        emit property_changed(true);
                    }
                });
        auto *randomize = new FxmSwitch(QStringLiteral("Randomize Order"),
                                        text_animator_item_editor_);
        randomize->setChecked(selector->randomize_order);
        auto *seed = new QSpinBox(text_animator_item_editor_);
        seed->setRange(std::numeric_limits<int>::min(),
                       std::numeric_limits<int>::max());
        seed->setValue(selector->random_seed);
        form->addRow(QString(), randomize);
        form->addRow(QStringLiteral("Random Seed"), seed);
        connect(randomize, &QCheckBox::toggled, this,
                [this, resolve_selector](bool checked) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->randomize_order = checked;
                        emit property_changed(true);
                    }
                });
        connect(seed, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, resolve_selector](int value) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->random_seed = value;
                        emit property_changed(true);
                    }
                });
    } else if (selector->type == TextSelectorType::Procedural) {
        auto *mode = new QComboBox(text_animator_item_editor_);
        const char *names[] = {"Random", "Noise", "Wave", "Sine", "Sawtooth", "Pulse", "Alternating", "Distance from start", "Distance from end", "Distance from centre", "Distance from custom index"};
        for (int i = 0; i <= (int)TextProceduralMode::DistanceFromCustomIndex; ++i)
            mode->addItem(QString::fromLatin1(names[i]), i);
        mode->setCurrentIndex(mode->findData((int)selector->procedural_mode));
        form->addRow(QStringLiteral("Mode"), mode);
        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, mode, resolve_selector](int) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->procedural_mode = (TextProceduralMode)mode->currentData().toInt();
                        emit property_changed(true);
                    }
                });
        add_animated_row(QStringLiteral("Amplitude"), selector_id, true, nullptr, &TextSelector::amplitude);
        add_animated_row(QStringLiteral("Frequency"), selector_id, true, nullptr, &TextSelector::frequency);
        add_animated_row(QStringLiteral("Phase"), selector_id, true, nullptr, &TextSelector::phase);
        add_animated_row(QStringLiteral("Speed"), selector_id, true, nullptr, &TextSelector::speed);
        add_animated_row(QStringLiteral("Falloff"), selector_id, true, nullptr, &TextSelector::falloff, 0.0, 1000.0);
        add_animated_row(QStringLiteral("Minimum"), selector_id, true, nullptr, &TextSelector::minimum);
        add_animated_row(QStringLiteral("Maximum"), selector_id, true, nullptr, &TextSelector::maximum);
        add_animated_row(QStringLiteral("Custom Index"), selector_id, true, nullptr, &TextSelector::custom_index);
    } else if (selector->type == TextSelectorType::TextBased) {
        auto *mode = new QComboBox(text_animator_item_editor_);
        const char *names[] = {"Character range", "Word range", "Line range", "Paragraph range", "Exact text", "Regular expression", "Whitespace", "Numbers", "Uppercase", "Lowercase", "Punctuation", "Rich-text style run", "External-data range", "Newly added text", "Changed text"};
        for (int i = 0; i <= (int)TextMatchMode::ChangedText; ++i)
            mode->addItem(QString::fromLatin1(names[i]), i);
        mode->setCurrentIndex(mode->findData((int)selector->match_mode));
        form->addRow(QStringLiteral("Match"), mode);
        auto *match = new QLineEdit(QString::fromStdString(
            selector->match_mode == TextMatchMode::RegularExpression
                ? selector->regular_expression : selector->match_text),
            text_animator_item_editor_);
        form->addRow(QStringLiteral("Text / Regex"), match);
        auto *start = new QSpinBox(text_animator_item_editor_);
        auto *end = new QSpinBox(text_animator_item_editor_);
        start->setRange(0, 1000000); end->setRange(0, 1000000);
        start->setValue((int)std::min<size_t>(selector->range_start, 1000000));
        end->setValue((int)std::min<size_t>(selector->range_end, 1000000));
        form->addRow(QStringLiteral("Range Start"), start);
        form->addRow(QStringLiteral("Range End"), end);
        auto *case_sensitive = new FxmSwitch(QStringLiteral("Case Sensitive"), text_animator_item_editor_);
        case_sensitive->setChecked(selector->case_sensitive);
        form->addRow(QString(), case_sensitive);
        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, mode, resolve_selector](int) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->match_mode = (TextMatchMode)mode->currentData().toInt();
                        notify_text_animator_changed(true);
                    }
                });
        connect(match, &QLineEdit::editingFinished, this,
                [this, match, resolve_selector]() {
                    if (TextSelector *resolved = resolve_selector()) {
                        if (resolved->match_mode == TextMatchMode::RegularExpression)
                            resolved->regular_expression = match->text().toStdString();
                        else
                            resolved->match_text = match->text().toStdString();
                        emit property_changed(true);
                    }
                });
        connect(start, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, resolve_selector](int value) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->range_start = (size_t)value;
                        emit property_changed(true);
                    }
                });
        connect(end, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, resolve_selector](int value) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->range_end = (size_t)value;
                        emit property_changed(true);
                    }
                });
        connect(case_sensitive, &QCheckBox::toggled, this,
                [this, resolve_selector](bool checked) {
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->case_sensitive = checked;
                        emit property_changed(true);
                    }
                });
    } else if (selector->type == TextSelectorType::Wiggly) {
        add_animated_row(QStringLiteral("Amount"), selector_id, true, nullptr, &TextSelector::wiggly_amount);
        add_animated_row(QStringLiteral("Frequency"), selector_id, true, nullptr, &TextSelector::wiggly_frequency);
        add_animated_row(QStringLiteral("Correlation"), selector_id, true, nullptr, &TextSelector::correlation, 0.0, 100.0);
        add_animated_row(QStringLiteral("Temporal Phase"), selector_id, true, nullptr, &TextSelector::temporal_phase);
        add_animated_row(QStringLiteral("Spatial Phase"), selector_id, true, nullptr, &TextSelector::spatial_phase);
        add_animated_row(QStringLiteral("Minimum"), selector_id, true, nullptr, &TextSelector::minimum_influence);
        add_animated_row(QStringLiteral("Maximum"), selector_id, true, nullptr, &TextSelector::maximum_influence);
        auto *seed = new QSpinBox(text_animator_item_editor_);
        seed->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        seed->setValue(selector->wiggly_seed);
        auto *lock = new FxmSwitch(QStringLiteral("Lock Dimensions"), text_animator_item_editor_);
        lock->setChecked(selector->lock_dimensions);
        auto *per_character = new FxmSwitch(QStringLiteral("Per-character Random Values"), text_animator_item_editor_);
        per_character->setChecked(selector->per_character_random);
        form->addRow(QStringLiteral("Random Seed"), seed);
        form->addRow(QString(), lock);
        form->addRow(QString(), per_character);
        connect(seed, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, resolve_selector](int value) {
                    if (loading_values_) return;
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->wiggly_seed = value;
                        emit property_changed(true);
                    }
                });
        connect(lock, &QCheckBox::toggled, this,
                [this, resolve_selector](bool checked) {
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->lock_dimensions = checked;
                        emit property_changed(true);
                    }
                });
        connect(per_character, &QCheckBox::toggled, this,
                [this, resolve_selector](bool checked) {
                    if (TextSelector *resolved = resolve_selector()) {
                        resolved->per_character_random = checked;
                        emit property_changed(true);
                    }
                });
    }
}
