static void set_animated_y(AnimatedVec2Property &prop, double time, double value)
{
    Vec2Value next = prop.evaluate(time);
    next.y = value;
    set_animated_value(prop, time, next);
}
static void set_color_channels_at(Layer &layer, bool text, double time, uint32_t argb)
{
    auto &a = text ? layer.text_color_a : layer.fill_color_a;
    auto &r = text ? layer.text_color_r : layer.fill_color_r;
    auto &g = text ? layer.text_color_g : layer.fill_color_g;
    auto &b = text ? layer.text_color_b : layer.fill_color_b;
    set_animated_value(a, time, (argb >> 24) & 0xFF);
    set_animated_value(r, time, (argb >> 16) & 0xFF);
    set_animated_value(g, time, (argb >> 8) & 0xFF);
    set_animated_value(b, time, argb & 0xFF);
}
static void set_stroke_color_channels_at(Layer &layer, double time, uint32_t argb)
{
    set_animated_value(layer.stroke_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(layer.stroke_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(layer.stroke_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(layer.stroke_color_b, time, argb & 0xFF);
}
static void set_shadow_color_channels_at(Layer &layer, double time, uint32_t argb)
{
    set_animated_value(layer.shadow_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(layer.shadow_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(layer.shadow_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(layer.shadow_color_b, time, argb & 0xFF);
}
static void set_background_color_channels_at(Layer &layer, double time, uint32_t argb)
{
    set_animated_value(layer.background_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(layer.background_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(layer.background_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(layer.background_color_b, time, argb & 0xFF);
}
static void set_background_stroke_color_channels_at(Layer &layer, double time, uint32_t argb)
{
    set_animated_value(layer.background_stroke_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(layer.background_stroke_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(layer.background_stroke_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(layer.background_stroke_color_b, time, argb & 0xFF);
}
static bool keyframe_at_time(const AnimatedProperty &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    for (const auto &kf : prop.keyframes)
        if (std::abs(kf.time - time) <= kEpsilon) return true;
    return false;
}
static bool keyframe_at_time(const AnimatedVec2Property &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    for (const auto &kf : prop.keyframes)
        if (std::abs(kf.time - time) <= kEpsilon) return true;
    return false;
}
static bool keyframe_at_time(const AnimatedVec3Property &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    for (const auto &kf : prop.keyframes)
        if (std::abs(kf.time - time) <= kEpsilon) return true;
    return false;
}
static void remove_keyframe_at(AnimatedProperty &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.keyframes.erase(
        std::remove_if(prop.keyframes.begin(), prop.keyframes.end(),
                       [&](const Keyframe &kf) { return std::abs(kf.time - time) <= kEpsilon; }),
        prop.keyframes.end());
}
static void remove_keyframe_at(AnimatedVec2Property &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.keyframes.erase(
        std::remove_if(prop.keyframes.begin(), prop.keyframes.end(),
                       [&](const VectorKeyframe &kf) { return std::abs(kf.time - time) <= kEpsilon; }),
        prop.keyframes.end());
}
static void remove_keyframe_at(AnimatedVec3Property &prop, double time)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.keyframes.erase(
        std::remove_if(prop.keyframes.begin(), prop.keyframes.end(),
                       [&](const Vector3Keyframe &kf) { return std::abs(kf.time - time) <= kEpsilon; }),
        prop.keyframes.end());
    prop.recalculate_rove_times();
}
static void toggle_keyframe(AnimatedProperty &prop, double time, double value)
{
    if (keyframe_at_time(prop, time))
        remove_keyframe_at(prop, time);
    else
        add_or_replace_keyframe(prop, time, value);
}
static void toggle_keyframe(AnimatedVec2Property &prop, double time, Vec2Value value)
{
    if (keyframe_at_time(prop, time))
        remove_keyframe_at(prop, time);
    else
        add_or_replace_keyframe(prop, time, value);
}
static void toggle_keyframe(AnimatedVec3Property &prop, double time, Vec3Value value)
{
    if (keyframe_at_time(prop, time))
        remove_keyframe_at(prop, time);
    else
        add_or_replace_keyframe(prop, time, value);
}
static bool any_keyframe_at_time(std::initializer_list<const AnimatedProperty *> props, double time)
{
    for (const auto *prop : props)
        if (prop && keyframe_at_time(*prop, time)) return true;
    return false;
}
static bool any_keyframes(std::initializer_list<const AnimatedProperty *> props)
{
    for (const auto *prop : props)
        if (prop && prop->is_animated()) return true;
    return false;
}

static QStringList font_styles_for_family(const QString &family)
{
    QFontDatabase fdb;
    QStringList styles = fdb.styles(family);
    if (styles.isEmpty()) {
        styles << fxm_tr("OBSTitles.Regular");
    } else {
        styles.removeDuplicates();
        styles.sort(Qt::CaseInsensitive);
        int regular = styles.indexOf(fxm_tr("OBSTitles.Regular"));
        if (regular > 0) {
            QString item = styles.takeAt(regular);
            styles.prepend(item);
        }
    }
    return styles;
}

static void populate_font_style_combo(QComboBox *combo, const QString &family, const QString &preferred)
{
    if (!combo) return;
    QSignalBlocker blocker(combo);
    combo->clear();
    QStringList styles = font_styles_for_family(family);
    for (const QString &style : styles)
        combo->addItem(style, style);
    int idx = preferred.isEmpty() ? -1 : combo->findText(preferred);
    if (idx < 0) idx = combo->findText(fxm_tr("OBSTitles.Regular"));
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

static void style_color_button(QPushButton *button, uint32_t argb)
{
    if (!button) return;
    QColor c = color_from_argb(argb);
    button->setText(c.name(QColor::HexArgb));
    button->setToolTip(QString());
    button->setStyleSheet(QString(
        "QPushButton{color:%1;background:%2;border:1px solid #555;"
        "border-radius:3px;padding:3px 8px;}")
        .arg(c.lightness() < 128 ? "#fff" : "#000")
        .arg(c.name(QColor::HexArgb)));
}


static void style_scene_mask_placeholder_button(QPushButton *button)
{
    if (!button) return;
    button->setText(QString());
    button->setToolTip(fxm_tr("OBSTitles.UseAsSceneMask"));
    button->setStyleSheet(QStringLiteral(
        "QPushButton{"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #ff00c8,stop:0.24 #ff00c8,"
        "stop:0.25 #242424,stop:0.49 #242424,"
        "stop:0.50 #ff00c8,stop:0.74 #ff00c8,"
        "stop:0.75 #242424,stop:1 #242424);"
        "border:1px dashed #ff66d9;border-radius:3px;padding:3px 8px;}"));
    button->setProperty("buttonFillKind", QStringLiteral("scene-mask-placeholder"));
}

static void style_gradient_button(QPushButton *button, uint32_t start_argb, uint32_t end_argb, int gradient_type)
{
    if (!button) return;
    QColor start = color_from_argb(start_argb);
    QColor end = color_from_argb(end_argb);
    button->setText(QString());
    const int type = normalized_gradient_type(gradient_type);
    const bool radial = type == 1;
    QString type_name = fxm_tr("OBSTitles.LinearGradient");
    if (type == 1) type_name = fxm_tr("OBSTitles.RadialGradient");
    else if (type == 2) type_name = fxm_tr("OBSTitles.ConicalGradient");
    button->setToolTip(type_name);
    const QString fill = radial
        ? QStringLiteral("qradialgradient(cx:0.5,cy:0.5,radius:0.65,fx:0.5,fy:0.5,stop:0 %1,stop:1 %2)")
              .arg(start.name(QColor::HexArgb), end.name(QColor::HexArgb))
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1,stop:1 %2)")
              .arg(start.name(QColor::HexArgb), end.name(QColor::HexArgb));
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;border:1px solid #555;border-radius:3px;padding:3px 8px;}").arg(fill));
}

static void style_color_button_mixed(QPushButton *button)
{
    if (!button) return;
    button->setText(fxm_tr("OBSTitles.M"));
    button->setToolTip(fxm_tr("OBSTitles.MixedValues"));
    button->setStyleSheet(
        "QPushButton{color:#f2c94c;background:#252525;border:1px dashed #8a7635;"
        "border-radius:3px;padding:3px 8px;font-weight:700;}");
}

static void remember_mixed_tooltip(QWidget *widget)
{
    if (!widget || widget->property("_fxm_mixed_original_tooltip").isValid())
        return;
    widget->setProperty("_fxm_mixed_original_tooltip", widget->toolTip());
}

static void set_mixed_tooltip(QWidget *widget, bool mixed)
{
    if (!widget) return;
    remember_mixed_tooltip(widget);
    widget->setToolTip(mixed
        ? fxm_tr("OBSTitles.MixedValues")
        : widget->property("_fxm_mixed_original_tooltip").toString());
}

static void set_mixed_style_suffix(QWidget *widget, bool mixed,
                                   const QString &suffix)
{
    if (!widget) return;
    if (!widget->property("_fxm_mixed_base_stylesheet").isValid())
        widget->setProperty("_fxm_mixed_base_stylesheet", widget->styleSheet());
    const QString base = widget->property("_fxm_mixed_base_stylesheet").toString();
    widget->setStyleSheet(mixed ? base + suffix : base);
}

static QIcon mixed_tinted_icon(const QIcon &source)
{
    if (source.isNull()) return source;
    QIcon result;
    const QColor mixed_color(QStringLiteral("#f2c94c"));
    const QList<QSize> sizes{QSize(12, 12), QSize(14, 14), QSize(16, 16),
                             QSize(20, 20), QSize(24, 24), QSize(32, 32)};
    for (const QSize &size : sizes) {
        QPixmap pixmap = source.pixmap(size);
        if (pixmap.isNull()) continue;
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), mixed_color);
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

static void set_spin_mixed(QAbstractSpinBox *spin, bool mixed)
{
    if (!spin) return;
    if (auto *edit = spin->findChild<QLineEdit *>()) {
        if (!edit->property("_fxm_mixed_original_alignment").isValid())
            edit->setProperty("_fxm_mixed_original_alignment", (int)edit->alignment());
        if (mixed) {
            edit->setText(fxm_tr("OBSTitles.M"));
            edit->setAlignment(Qt::AlignCenter);
        } else {
            edit->setAlignment((Qt::Alignment)edit->property(
                "_fxm_mixed_original_alignment").toInt());
        }
        set_mixed_style_suffix(edit, mixed,
            QStringLiteral("QLineEdit{color:#f2c94c;font-weight:700;}"));
    }
    set_mixed_tooltip(spin, mixed);
}

static void set_combo_mixed(QComboBox *combo, bool mixed)
{
    if (!combo) return;
    if (!combo->property("_fxm_mixed_original_placeholder").isValid())
        combo->setProperty("_fxm_mixed_original_placeholder", combo->placeholderText());
    if (combo->isEditable() && combo->lineEdit() &&
        !combo->lineEdit()->property("_fxm_mixed_original_alignment").isValid()) {
        combo->lineEdit()->setProperty("_fxm_mixed_original_alignment",
                                       (int)combo->lineEdit()->alignment());
    }
    if (mixed) {
        combo->setCurrentIndex(-1);
        combo->setPlaceholderText(fxm_tr("OBSTitles.M"));
        if (combo->isEditable() && combo->lineEdit()) {
            combo->lineEdit()->setText(fxm_tr("OBSTitles.M"));
            combo->lineEdit()->setAlignment(Qt::AlignCenter);
        }
    } else {
        combo->setPlaceholderText(combo->property(
            "_fxm_mixed_original_placeholder").toString());
        if (combo->isEditable() && combo->lineEdit())
            combo->lineEdit()->setAlignment((Qt::Alignment)combo->lineEdit()->property(
                "_fxm_mixed_original_alignment").toInt());
    }
    set_mixed_style_suffix(combo, mixed,
        QStringLiteral("QComboBox{color:#f2c94c;font-weight:700;}"
                       "QComboBox QLineEdit{color:#f2c94c;font-weight:700;}"));
    set_mixed_tooltip(combo, mixed);
}

static void set_button_mixed(QAbstractButton *button, bool mixed)
{
    if (!button) return;
    if (!button->property("_fxm_mixed_original_icon").isValid())
        button->setProperty("_fxm_mixed_original_icon", button->icon());
    if (mixed) {
        button->setChecked(false);
        const QIcon original = button->property("_fxm_mixed_original_icon").value<QIcon>();
        if (!original.isNull())
            button->setIcon(mixed_tinted_icon(original));
    } else {
        button->setIcon(button->property("_fxm_mixed_original_icon").value<QIcon>());
    }
    set_mixed_style_suffix(button, mixed,
        QStringLiteral("QAbstractButton{color:#f2c94c;font-weight:700;}"));
    set_mixed_tooltip(button, mixed);
}

static void set_button_group_mixed(QButtonGroup *group, bool mixed)
{
    if (!group) return;
    const bool was_exclusive = group->exclusive();
    if (mixed && was_exclusive)
        group->setExclusive(false);
    for (QAbstractButton *button : group->buttons())
        set_button_mixed(button, mixed);
    if (mixed && was_exclusive)
        group->setExclusive(true);
}

static QColor keyframe_color(EasingType)
{
    return fxm_keyframe_color();
}

static QPainterPath keyframe_shape_path(EasingType easing, const QPointF &center, qreal radius)
{
    const qreal x = center.x();
    const qreal y = center.y();
    QPainterPath path;

    switch (easing) {
    case EasingType::Linear:
        path.moveTo(x, y - radius);
        path.lineTo(x + radius, y);
        path.lineTo(x, y + radius);
        path.lineTo(x - radius, y);
        path.closeSubpath();
        break;
    case EasingType::EaseIn:
        path.moveTo(x - radius, y);
        path.lineTo(x, y - radius);
        path.lineTo(x + radius, y - radius);
        path.lineTo(x + radius, y + radius);
        path.lineTo(x, y + radius);
        path.closeSubpath();
        break;
    case EasingType::EaseOut:
        path.moveTo(x + radius, y);
        path.lineTo(x, y - radius);
        path.lineTo(x - radius, y - radius);
        path.lineTo(x - radius, y + radius);
        path.lineTo(x, y + radius);
        path.closeSubpath();
        break;
    case EasingType::EaseInOut:
        path.moveTo(x - radius, y - radius);
        path.lineTo(x + radius, y - radius);
        path.lineTo(x - radius, y + radius);
        path.lineTo(x + radius, y + radius);
        path.closeSubpath();
        break;
    case EasingType::Bezier:
        path.addEllipse(center, radius, radius);
        break;
    case EasingType::Hold:
        path.addRect(QRectF(x - radius, y - radius, radius * 2.0, radius * 2.0));
        break;
    default:
        path.moveTo(x, y - radius);
        path.lineTo(x + radius, y);
        path.lineTo(x, y + radius);
        path.lineTo(x - radius, y);
        path.closeSubpath();
        break;
    }

    return path;
}

static void draw_keyframe_marker(QPainter &painter, const QPointF &center, EasingType easing,
                                 qreal radius, const QColor &fill,
                                 const QColor &stroke, qreal stroke_width)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(fill);
    painter.setPen(QPen(stroke, stroke_width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.drawPath(keyframe_shape_path(easing, center, radius));

    if (easing == EasingType::Bezier) {
        painter.setPen(QPen(stroke, std::max<qreal>(1.0, stroke_width - 0.5)));
        painter.drawLine(QPointF(center.x() - radius - 3.0, center.y()),
                         QPointF(center.x() - radius, center.y()));
        painter.drawLine(QPointF(center.x() + radius, center.y()),
                         QPointF(center.x() + radius + 3.0, center.y()));
    }

    painter.restore();
}


struct TimelinePropertyRef {
    AnimatedProperty *scalar = nullptr;
    AnimatedVec2Property *vector = nullptr;
    LayerEffect *extension_effect = nullptr;
    QString extension_path;
    std::string extension_name;
    QJsonValue extension_fallback;
    EasingType extension_default_easing = EasingType::Linear;
    std::vector<AnimatedProperty *> scalar_group;
    AnimatedDiscreteProperty *discrete = nullptr;
    /* Development Version 208: a single authored XYZ spatial track can be
     * exposed as three independent Graph Editor channels without duplicating
     * keyframes or losing the shared 3D Bezier metadata. Appended fields keep
     * all pre-208 aggregate initializers source-compatible. */
    AnimatedVec3Property *vector3 = nullptr;
    int vector3_component = -1;
    bool *vector3_enabled = nullptr;
    /* Development Version 212.1: the legacy-named AnimatedVec2Property now
     * stores XYZ as well.  Keep its graph-channel selection separate from the
     * dedicated spatial Vector3 track so old aggregate initializers remain
     * source-compatible and 2D rendering continues to ignore Z. */
    int vector_component = -1;

    void activate_vector3()
    {
        if (vector3_enabled) *vector3_enabled = true;
    }

    AnimatedProperty *group_representative() const
    {
        for (AnimatedProperty *property : scalar_group)
            if (property && property->is_animated()) return property;
        return scalar_group.empty() ? nullptr : scalar_group.front();
    }
    std::vector<double> scalar_group_keyframe_times() const
    {
        std::vector<double> times;
        for (const AnimatedProperty *property : scalar_group) {
            if (!property) continue;
            for (const Keyframe &keyframe : property->keyframes)
                times.push_back(keyframe.time);
        }
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
            return std::abs(a - b) < 0.0001;
        }), times.end());
        return times;
    }
    std::pair<AnimatedProperty *, size_t> scalar_group_keyframe_location(
        size_t aggregate_index) const
    {
        const std::vector<double> times = scalar_group_keyframe_times();
        if (aggregate_index >= times.size()) return {nullptr, 0};
        const double time = times[aggregate_index];
        for (AnimatedProperty *property : scalar_group) {
            if (!property) continue;
            for (size_t index = 0; index < property->keyframes.size(); ++index)
                if (std::abs(property->keyframes[index].time - time) < 0.0001)
                    return {property, index};
        }
        return {nullptr, 0};
    }
    explicit operator bool() const {
        return scalar || vector || vector3 || extension_effect || !scalar_group.empty() || discrete;
    }
    bool is_extension() const { return extension_effect != nullptr; }
    std::string name() const {
        if (!extension_name.empty()) return extension_name;
        return discrete ? discrete->name : vector3 ? vector3->name : vector ? vector->name : scalar ? scalar->name : std::string();
    }
    bool is_animated() const {
        if (discrete) return discrete->is_animated();
        if (is_extension())
            return !fxm::effects::animation::track_keys(*extension_effect, extension_path).isEmpty();
        if (!scalar_group.empty())
            return std::any_of(scalar_group.begin(), scalar_group.end(),
                [](const AnimatedProperty *property) {
                    return property && property->is_animated();
                });
        return vector3 ? vector3->is_animated() : vector ? vector->is_animated() : scalar && scalar->is_animated();
    }
    size_t keyframe_count() const {
        if (discrete) return discrete->keyframes.size();
        if (is_extension())
            return static_cast<size_t>(fxm::effects::animation::track_keys(*extension_effect, extension_path).size());
        if (!scalar_group.empty())
            return scalar_group_keyframe_times().size();
        return vector3 ? vector3->keyframes.size() : vector ? vector->keyframes.size() : scalar ? scalar->keyframes.size() : 0;
    }
    double keyframe_time(size_t index) const {
        if (discrete) return discrete->keyframes[index].time;
        if (is_extension())
            return fxm::effects::animation::track_keys(*extension_effect, extension_path)
                .at(static_cast<qsizetype>(index)).toObject()
                .value(QStringLiteral("time")).toDouble();
        if (!scalar_group.empty()) {
            const std::vector<double> times = scalar_group_keyframe_times();
            return index < times.size() ? times[index] : 0.0;
        }
        return vector3 ? vector3->keyframes[index].time : vector ? vector->keyframes[index].time : scalar->keyframes[index].time;
    }
    EasingType keyframe_easing(size_t index) const {
        if (discrete) return EasingType::Hold;
        if (is_extension())
            return fxm::effects::animation::easing_from_key(
                fxm::effects::animation::track_keys(*extension_effect, extension_path)
                    .at(static_cast<qsizetype>(index)).toObject());
        if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            return location.first
                ? location.first->keyframes[location.second].easing
                : EasingType::Linear;
        }
        return vector3 ? vector3->keyframes[index].easing : vector ? vector->keyframes[index].easing : scalar->keyframes[index].easing;
    }
    TemporalInterpolationMode keyframe_temporal_mode(size_t index) const {
        if (is_hold_only()) return TemporalInterpolationMode::Hold;
        if (is_extension()) {
            const QJsonObject key = fxm::effects::animation::track_keys(
                *extension_effect, extension_path).at((qsizetype)index).toObject();
            if (fxm::effects::animation::temporal_velocity_explicit(key))
                return fxm::effects::animation::temporal_mode_from_key(key);
            const EasingType easing = fxm::effects::animation::easing_from_key(key);
            return easing == EasingType::Hold ? TemporalInterpolationMode::Hold
                 : easing == EasingType::Linear ? TemporalInterpolationMode::Linear
                 : TemporalInterpolationMode::ManualBezier;
        }
        if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            if (!location.first) return TemporalInterpolationMode::Linear;
            const Keyframe &key = location.first->keyframes[location.second];
            if (key.temporal_velocity_explicit) return key.temporal_mode;
            return key.easing == EasingType::Hold ? TemporalInterpolationMode::Hold
                 : key.easing == EasingType::Linear ? TemporalInterpolationMode::Linear
                 : TemporalInterpolationMode::ManualBezier;
        }
        if (vector3) {
            const Vector3Keyframe &key = vector3->keyframes[index];
            if (key.temporal_velocity_explicit) return key.temporal_mode;
            return key.easing == EasingType::Hold ? TemporalInterpolationMode::Hold
                 : key.easing == EasingType::Linear ? TemporalInterpolationMode::Linear
                 : TemporalInterpolationMode::ManualBezier;
        }
        if (vector) {
            const VectorKeyframe &key = vector->keyframes[index];
            if (key.temporal_velocity_explicit) return key.temporal_mode;
            return key.easing == EasingType::Hold ? TemporalInterpolationMode::Hold
                 : key.easing == EasingType::Linear ? TemporalInterpolationMode::Linear
                 : TemporalInterpolationMode::ManualBezier;
        }
        if (scalar) {
            const Keyframe &key = scalar->keyframes[index];
            if (key.temporal_velocity_explicit) return key.temporal_mode;
            return key.easing == EasingType::Hold ? TemporalInterpolationMode::Hold
                 : key.easing == EasingType::Linear ? TemporalInterpolationMode::Linear
                 : TemporalInterpolationMode::ManualBezier;
        }
        return TemporalInterpolationMode::Linear;
    }
    bool keyframe_temporal_tangents_linked(size_t index) const {
        if (discrete) return true;
        if (is_extension())
            return fxm::effects::animation::track_keys(*extension_effect, extension_path)
                .at((qsizetype)index).toObject()
                .value(QStringLiteral("temporal_tangents_linked")).toBool(true);
        if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            return location.first
                ? location.first->keyframes[location.second].temporal_tangents_linked
                : true;
        }
        if (vector3) return vector3->keyframes[index].temporal_tangents_linked;
        if (vector) return vector->keyframes[index].temporal_tangents_linked;
        return scalar ? scalar->keyframes[index].temporal_tangents_linked : true;
    }
    double graph_value(double time) const {
        if (discrete) return 0.0;
        if (is_extension()) {
            const QJsonValue value = fxm::effects::animation::evaluate_track(
                *extension_effect, extension_path, time, extension_fallback);
            return value.isDouble() ? value.toDouble() : 0.0;
        }
        if (AnimatedProperty *property = group_representative())
            return property->evaluate(time);
        if (vector3) return vector3_component >= 0
            ? vector3->component_value(time, vector3_component)
            : vector3->path_value(time);
        if (vector) {
            if (vector_component < 0) return vector->path_value(time);
            const Vec2Value value = vector->evaluate(time);
            return vector_component == 0 ? value.x
                 : vector_component == 1 ? value.y : value.z;
        }
        return scalar ? scalar->evaluate(time) : 0.0;
    }
    double graph_speed(double time) const {
        if (discrete) return 0.0;
        if (is_extension()) {
            const double h = 1e-5;
            return (graph_value(time + h) - graph_value(time - h)) / (2.0 * h);
        }
        if (AnimatedProperty *property = group_representative())
            return property->velocity(time);
        if (vector3) return vector3_component >= 0
            ? vector3->component_velocity(time, vector3_component)
            : vector3->speed(time);
        if (vector) {
            if (vector_component < 0) return vector->speed(time);
            const Vec2Value value = vector->velocity(time);
            return vector_component == 0 ? value.x
                 : vector_component == 1 ? value.y : value.z;
        }
        return scalar ? scalar->velocity(time) : 0.0;
    }
    double keyframe_temporal_influence(size_t index, bool incoming) const {
        if (discrete) return 0.0;
        if (is_extension()) {
            const QJsonObject key = fxm::effects::animation::track_keys(
                *extension_effect, extension_path).at((qsizetype)index).toObject();
            return key.value(incoming ? QStringLiteral("temporal_in_influence")
                                      : QStringLiteral("temporal_out_influence"))
                .toDouble(33.3333333333);
        }
        if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            if (!location.first) return 33.3333333333;
            AnimatedProperty *property = location.first;
            const size_t property_index = location.second;
            const Keyframe &key = property->keyframes[property_index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_influence : key.outgoing_influence;
            if (incoming && property_index > 0)
                return property->temporal_segment(property_index - 1).incoming_influence * 100.0;
            if (!incoming && property_index + 1 < property->keyframes.size())
                return property->temporal_segment(property_index).outgoing_influence * 100.0;
        } else if (vector3) {
            const Vector3Keyframe &key = vector3->keyframes[index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_influence : key.outgoing_influence;
            if (incoming && index > 0)
                return vector3->temporal_segment(index - 1).incoming_influence * 100.0;
            if (!incoming && index + 1 < vector3->keyframes.size())
                return vector3->temporal_segment(index).outgoing_influence * 100.0;
        } else if (vector) {
            const VectorKeyframe &key = vector->keyframes[index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_influence : key.outgoing_influence;
            if (incoming && index > 0)
                return vector->temporal_segment(index - 1).incoming_influence * 100.0;
            if (!incoming && index + 1 < vector->keyframes.size())
                return vector->temporal_segment(index).outgoing_influence * 100.0;
        } else if (scalar) {
            const Keyframe &key = scalar->keyframes[index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_influence : key.outgoing_influence;
        }
        return 33.3333333333;
    }
    double keyframe_temporal_speed(size_t index, bool incoming) const {
        if (discrete) return 0.0;
        if (is_extension()) {
            const QJsonObject key = fxm::effects::animation::track_keys(
                *extension_effect, extension_path).at((qsizetype)index).toObject();
            if (fxm::effects::animation::temporal_velocity_explicit(key))
                return key.value(incoming ? QStringLiteral("temporal_in_speed")
                                          : QStringLiteral("temporal_out_speed")).toDouble();
        } else if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            if (location.first) {
                const Keyframe &key = location.first->keyframes[location.second];
                if (key.temporal_velocity_explicit)
                    return incoming ? key.incoming_speed : key.outgoing_speed;
            }
        } else if (vector3) {
            const Vector3Keyframe &key = vector3->keyframes[index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_speed : key.outgoing_speed;
        } else if (vector) {
            const VectorKeyframe &key = vector->keyframes[index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_speed : key.outgoing_speed;
        } else if (scalar) {
            const Keyframe &key = scalar->keyframes[index];
            if (key.temporal_velocity_explicit)
                return incoming ? key.incoming_speed : key.outgoing_speed;
        }
        const double time = keyframe_time(index);
        return graph_speed(time + (incoming ? -1e-6 : 1e-6));
    }
    bool graph_value_editable() const {
        if (discrete) return false;
        if (is_extension()) {
            const QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            return !keys.isEmpty() && keys.first().toObject().value(QStringLiteral("value")).isDouble();
        }
        return vector3 ? vector3_component >= 0
             : vector ? vector_component >= 0
             : (scalar != nullptr || !scalar_group.empty());
    }
    void set_keyframe_graph_value(size_t index, double value)
    {
        if (is_extension()) {
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            if (index >= (size_t)keys.size()) return;
            QJsonObject key = keys.at((qsizetype)index).toObject();
            key.insert(QStringLiteral("value"), value);
            keys.replace((qsizetype)index, key);
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            if (!location.first) return;
            const double time = location.first->keyframes[location.second].time;
            const double delta = value - location.first->keyframes[location.second].value;
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                for (Keyframe &key : property->keyframes)
                    if (std::abs(key.time - time) < 0.0001) {
                        key.value += delta;
                        break;
                    }
            }
        } else if (vector3 && vector3_component >= 0 && index < vector3->keyframes.size()) {
            activate_vector3();
            Vec3Value &v = vector3->keyframes[index].value;
            if (vector3_component == 0) v.x = value;
            else if (vector3_component == 1) v.y = value;
            else v.z = value;
        } else if (vector && vector_component >= 0 && index < vector->keyframes.size()) {
            Vec2Value &v = vector->keyframes[index].value;
            if (vector_component == 0) v.x = value;
            else if (vector_component == 1) v.y = value;
            else v.z = value;
        } else if (scalar && index < scalar->keyframes.size()) {
            scalar->keyframes[index].value = scalar->name == "camera_projection"
                ? (value >= 0.5 ? 1.0 : 0.0) : value;
            if (scalar->name == "camera_projection") {
                scalar->keyframes[index].easing = EasingType::Hold;
                scalar->keyframes[index].temporal_mode = TemporalInterpolationMode::Hold;
                scalar->keyframes[index].temporal_velocity_explicit = true;
            }
        }
    }
    bool supports_spatial_interpolation() const {
        if (discrete) return false;
        return (vector && vector->name == "position") ||
               (vector3 && (vector3->name == "position_3d" ||
                            vector3->name == "camera_position_3d" ||
                            vector3->name == "camera_target_3d"));
    }
    SpatialInterpolationMode keyframe_spatial_mode(size_t index) const {
        if (!supports_spatial_interpolation()) return SpatialInterpolationMode::Linear;
        if (vector3 && index < vector3->keyframes.size()) return vector3->keyframes[index].spatial_mode;
        return vector && index < vector->keyframes.size()
            ? vector->keyframes[index].spatial_mode : SpatialInterpolationMode::Linear;
    }
    bool keyframe_spatial_tangents_linked(size_t index) const {
        if (!supports_spatial_interpolation()) return true;
        if (vector3 && index < vector3->keyframes.size()) return vector3->keyframes[index].spatial_tangents_linked;
        return vector && index < vector->keyframes.size()
            ? vector->keyframes[index].spatial_tangents_linked : true;
    }
    bool keyframe_roves_across_time(size_t index) const {
        if (!supports_spatial_interpolation()) return false;
        if (vector3 && index < vector3->keyframes.size()) return vector3->keyframes[index].rove_across_time;
        return vector && index < vector->keyframes.size()
            ? vector->keyframes[index].rove_across_time : false;
    }
    Keyframe scalar_keyframe(size_t index) const {
        if (!scalar_group.empty()) {
            const auto location = scalar_group_keyframe_location(index);
            return location.first
                ? location.first->keyframes[location.second] : Keyframe{};
        }
        return scalar ? scalar->keyframes[index] : Keyframe{};
    }
    std::vector<Keyframe> scalar_group_keyframes(size_t index) const
    {
        std::vector<Keyframe> result;
        if (scalar_group.empty()) return result;
        const double time = keyframe_time(index);
        result.reserve(scalar_group.size());
        for (AnimatedProperty *property : scalar_group) {
            Keyframe keyframe;
            bool found = false;
            if (property) {
                for (const Keyframe &candidate : property->keyframes) {
                    if (std::abs(candidate.time - time) < 0.0001) {
                        keyframe = candidate;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    keyframe.time = time;
                    keyframe.value = property->evaluate(time);
                    keyframe.easing = keyframe_easing(index);
                }
            }
            result.push_back(keyframe);
        }
        return result;
    }
    VectorKeyframe vector_keyframe(size_t index) const { return vector ? vector->keyframes[index] : VectorKeyframe{}; }
    Vector3Keyframe vector3_keyframe(size_t index) const { return vector3 ? vector3->keyframes[index] : Vector3Keyframe{}; }
    QJsonObject extension_keyframe(size_t index) const {
        return is_extension()
            ? fxm::effects::animation::track_keys(*extension_effect, extension_path)
                  .at(static_cast<qsizetype>(index)).toObject()
            : QJsonObject{};
    }
    bool is_vector() const { return vector != nullptr; }
    bool is_vector3() const { return vector3 != nullptr; }
    bool is_discrete() const { return discrete != nullptr; }
    bool is_hold_only() const {
        return discrete || (scalar && scalar->name == "camera_projection");
    }
    bool graph_eligible() const { return !discrete; }
    DiscreteKeyframe discrete_keyframe(size_t index) const { return discrete ? discrete->keyframes[index] : DiscreteKeyframe{}; }
    bool is_scalar_group() const { return !scalar_group.empty(); }
    int graph_channel_count() const
    {
        if (vector3 || vector) return 3;
        if (scalar_group.size() > 1)
            return static_cast<int>(std::min<size_t>(4, scalar_group.size()));
        return 1;
    }
    TimelinePropertyRef graph_channel(int component) const
    {
        TimelinePropertyRef result = *this;
        const int count = graph_channel_count();
        if (count <= 1) return result;
        component = std::clamp(component, 0, count - 1);
        if (result.vector3) {
            result.vector3_component = component;
        } else if (result.vector) {
            result.vector_component = component;
        } else if (!result.scalar_group.empty()) {
            AnimatedProperty *selected = result.scalar_group[static_cast<size_t>(component)];
            result.scalar_group.clear();
            if (selected) result.scalar_group.push_back(selected);
        }
        return result;
    }
    TimelinePropertyRef graph_all_channels() const
    {
        TimelinePropertyRef result = *this;
        if (result.vector3) result.vector3_component = -1;
        if (result.vector) result.vector_component = -1;
        return result;
    }
    double keyframe_graph_channel_value(size_t index, int component) const
    {
        if (index >= keyframe_count()) return 0.0;
        const double time = keyframe_time(index);
        return graph_channel(component).graph_value(time);
    }
    void set_keyframe_graph_channel_value(size_t index, int component, double value)
    {
        if (index >= keyframe_count()) return;
        if (vector3) {
            activate_vector3();
            if (index >= vector3->keyframes.size()) return;
            Vec3Value &next = vector3->keyframes[index].value;
            if (component == 0) next.x = value;
            else if (component == 1) next.y = value;
            else next.z = value;
            return;
        }
        if (vector) {
            if (index >= vector->keyframes.size()) return;
            Vec2Value &next = vector->keyframes[index].value;
            if (component == 0) next.x = value;
            else if (component == 1) next.y = value;
            else next.z = value;
            return;
        }
        if (!scalar_group.empty()) {
            component = std::clamp(component, 0,
                static_cast<int>(scalar_group.size()) - 1);
            AnimatedProperty *property = scalar_group[static_cast<size_t>(component)];
            if (!property) return;
            set_animated_value(*property, keyframe_time(index), value);
            return;
        }
        set_keyframe_graph_value(index, value);
    }
    void offset_keyframe_graph_value(size_t index, double delta)
    {
        if (std::abs(delta) < 1e-15) return;
        const int channels = graph_channel_count();
        if (channels > 1) {
            for (int component = 0; component < channels; ++component)
                set_keyframe_graph_channel_value(
                    index, component,
                    keyframe_graph_channel_value(index, component) + delta);
            return;
        }
        set_keyframe_graph_value(index, graph_value(keyframe_time(index)) + delta);
    }
    void push_keyframe(const DiscreteKeyframe &keyframe)
    {
        if (discrete) discrete->keyframes.push_back(keyframe);
    }
    void push_keyframe(const Keyframe &keyframe)
    {
        if (scalar) scalar->keyframes.push_back(keyframe);
    }
    void push_keyframe(const VectorKeyframe &keyframe)
    {
        if (vector) vector->keyframes.push_back(keyframe);
    }
    void push_keyframe(const Vector3Keyframe &keyframe)
    {
        if (vector3) {
            activate_vector3();
            vector3->keyframes.push_back(keyframe);
        }
    }
    void push_keyframes(const std::vector<Keyframe> &keyframes)
    {
        if (scalar_group.empty()) return;
        const size_t count = std::min(scalar_group.size(), keyframes.size());
        for (size_t i = 0; i < count; ++i)
            if (scalar_group[i]) scalar_group[i]->keyframes.push_back(keyframes[i]);
    }
    void push_keyframe(const QJsonObject &keyframe)
    {
        if (!is_extension()) return;
        QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
        keys.append(keyframe);
        fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
    }
    void set_keyframe_time(size_t index, double time)
    {
        if (discrete) {
            discrete->keyframes[index].time = time;
        } else if (is_extension()) {
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            QJsonObject key = keys.at(static_cast<qsizetype>(index)).toObject();
            key.insert(QStringLiteral("time"), time);
            keys.replace(static_cast<qsizetype>(index), key);
            // Preserve index identity during an interactive multi-keyframe drag.
            // The timeline/graph editor performs one explicit stable sort on release.
            fxm::effects::animation::write_track_keys_preserve_order(
                *extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const double old_time = keyframe_time(index);
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                for (Keyframe &keyframe : property->keyframes) {
                    if (std::abs(keyframe.time - old_time) < 0.0001) {
                        keyframe.time = time;
                        break;
                    }
                }
            }
        } else if (vector3) {
            activate_vector3();
            vector3->keyframes[index].rove_across_time = false;
            vector3->keyframes[index].time = time;
            vector3->recalculate_rove_times();
        } else if (vector) {
            vector->keyframes[index].rove_across_time = false;
            vector->keyframes[index].time = time;
            vector->recalculate_rove_times();
        } else if (scalar) {
            scalar->keyframes[index].time = time;
        }
    }
    void erase_keyframe(size_t index)
    {
        if (discrete) {
            discrete->keyframes.erase(discrete->keyframes.begin() + (ptrdiff_t)index);
        } else if (is_extension()) {
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            keys.removeAt(static_cast<qsizetype>(index));
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const double time = keyframe_time(index);
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                property->keyframes.erase(
                    std::remove_if(property->keyframes.begin(), property->keyframes.end(),
                        [time](const Keyframe &keyframe) {
                            return std::abs(keyframe.time - time) < 0.0001;
                        }),
                    property->keyframes.end());
            }
        } else if (vector3) {
            activate_vector3();
            vector3->keyframes.erase(vector3->keyframes.begin() + (ptrdiff_t)index);
            vector3->recalculate_rove_times();
        } else if (vector) {
            vector->keyframes.erase(vector->keyframes.begin() + (ptrdiff_t)index);
            vector->recalculate_rove_times();
        } else if (scalar) {
            scalar->keyframes.erase(scalar->keyframes.begin() + (ptrdiff_t)index);
        }
    }
    void clear_keyframes()
    {
        if (discrete) discrete->keyframes.clear();
        else if (is_extension())
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, QJsonArray{});
        else if (!scalar_group.empty()) {
            for (AnimatedProperty *property : scalar_group)
                if (property) property->keyframes.clear();
        } else if (vector3) {
            activate_vector3();
            vector3->keyframes.clear();
        }
        else if (vector) vector->keyframes.clear();
        else if (scalar) scalar->keyframes.clear();
    }
    void sort_keyframes()
    {
        if (discrete) discrete->sort_keyframes();
        else if (is_extension()) {
            fxm::effects::animation::write_track_keys(
                *extension_effect, extension_path,
                fxm::effects::animation::track_keys(*extension_effect, extension_path));
        } else if (!scalar_group.empty()) {
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                std::sort(property->keyframes.begin(), property->keyframes.end(),
                          [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
            }
        } else if (vector3) {
            std::sort(vector3->keyframes.begin(), vector3->keyframes.end(),
                      [](const Vector3Keyframe &a, const Vector3Keyframe &b) { return a.time < b.time; });
            vector3->recalculate_rove_times();
        } else if (vector) {
            std::sort(vector->keyframes.begin(), vector->keyframes.end(),
                      [](const VectorKeyframe &a, const VectorKeyframe &b) { return a.time < b.time; });
            vector->recalculate_rove_times();
        } else if (scalar) {
            std::sort(scalar->keyframes.begin(), scalar->keyframes.end(),
                      [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
        }
    }
    void apply_spatial_mode(size_t index, SpatialInterpolationMode mode)
    {
        if (!supports_spatial_interpolation())
            return;
        if (vector3) { activate_vector3(); vector3->set_spatial_mode(index, mode); }
        else vector->set_spatial_mode(index, mode);
    }
    void set_spatial_tangents_linked(size_t index, bool linked)
    {
        if (!supports_spatial_interpolation())
            return;
        if (vector3) { activate_vector3(); vector3->set_spatial_tangents_linked(index, linked); }
        else vector->set_spatial_tangents_linked(index, linked);
    }
    void set_keyframe_rove_across_time(size_t index, bool enabled)
    {
        if (!supports_spatial_interpolation())
            return;
        if (vector3) { activate_vector3(); vector3->set_rove_across_time(index, enabled); }
        else vector->set_rove_across_time(index, enabled);
    }

    void apply_temporal_mode(size_t index, TemporalInterpolationMode mode)
    {
        if (is_hold_only()) return;
        if (is_extension()) {
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            if (index >= (size_t)keys.size()) return;
            QJsonObject key = keys.at((qsizetype)index).toObject();
            const double time = key.value(QStringLiteral("time")).toDouble();
            auto slope = [&](int a, int b) {
                if (a < 0 || b < 0 || a >= keys.size() || b >= keys.size()) return 0.0;
                const QJsonObject ka = keys.at(a).toObject(), kb = keys.at(b).toObject();
                const double dt = kb.value(QStringLiteral("time")).toDouble() -
                                  ka.value(QStringLiteral("time")).toDouble();
                return dt > 1e-10 ? (kb.value(QStringLiteral("value")).toDouble() -
                                     ka.value(QStringLiteral("value")).toDouble()) / dt : 0.0;
            };
            const double in_speed = index > 0 ? slope((int)index - 1, (int)index) :
                                    (index + 1 < (size_t)keys.size() ? slope((int)index, (int)index + 1) : 0.0);
            const double out_speed = index + 1 < (size_t)keys.size() ? slope((int)index, (int)index + 1) : in_speed;
            if (!fxm::effects::animation::temporal_velocity_explicit(key)) {
                key.insert(QStringLiteral("temporal_in_speed"), in_speed);
                key.insert(QStringLiteral("temporal_out_speed"), out_speed);
                key.insert(QStringLiteral("temporal_in_influence"), 33.3333333333);
                key.insert(QStringLiteral("temporal_out_influence"), 33.3333333333);
            }
            fxm::effects::animation::set_key_temporal_mode(key, mode);
            if (mode == TemporalInterpolationMode::AutoBezier) {
                double auto_speed = in_speed;
                if (index > 0 && index + 1 < (size_t)keys.size())
                    auto_speed = slope((int)index - 1, (int)index + 1);
                key.insert(QStringLiteral("temporal_in_speed"), auto_speed);
                key.insert(QStringLiteral("temporal_out_speed"), auto_speed);
            } else if (mode == TemporalInterpolationMode::ContinuousBezier) {
                const double common = 0.5 * (key.value(QStringLiteral("temporal_in_speed")).toDouble(in_speed) +
                                             key.value(QStringLiteral("temporal_out_speed")).toDouble(out_speed));
                key.insert(QStringLiteral("temporal_in_speed"), common);
                key.insert(QStringLiteral("temporal_out_speed"), common);
            }
            key.insert(QStringLiteral("time"), time);
            keys.replace((qsizetype)index, key);
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const double time = keyframe_time(index);
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                for (size_t i = 0; i < property->keyframes.size(); ++i)
                    if (std::abs(property->keyframes[i].time - time) < 0.0001) {
                        property->set_temporal_mode(i, mode); break;
                    }
            }
        } else if (vector3) { activate_vector3(); vector3->set_temporal_mode(index, mode); }
        else if (vector) vector->set_temporal_mode(index, mode);
        else if (scalar) scalar->set_temporal_mode(index, mode);
    }
    void set_temporal_handle(size_t index, bool incoming, double influence,
                             double speed, bool break_pair)
    {
        if (is_hold_only()) return;
        if (is_extension()) {
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            if (index >= (size_t)keys.size()) return;
            QJsonObject key = keys.at((qsizetype)index).toObject();
            const bool was_linked = key.value(QStringLiteral("temporal_tangents_linked")).toBool(true);
            fxm::effects::animation::set_key_temporal_mode(
                key, TemporalInterpolationMode::ManualBezier);
            key.insert(incoming ? QStringLiteral("temporal_in_influence")
                                : QStringLiteral("temporal_out_influence"),
                       std::clamp(influence, 0.0, 100.0));
            key.insert(incoming ? QStringLiteral("temporal_in_speed")
                                : QStringLiteral("temporal_out_speed"), speed);
            if (break_pair)
                key.insert(QStringLiteral("temporal_tangents_linked"), false);
            else if (was_linked) {
                key.insert(incoming ? QStringLiteral("temporal_out_influence")
                                    : QStringLiteral("temporal_in_influence"),
                           std::clamp(influence, 0.0, 100.0));
                key.insert(incoming ? QStringLiteral("temporal_out_speed")
                                    : QStringLiteral("temporal_in_speed"), speed);
            }
            keys.replace((qsizetype)index, key);
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const double time = keyframe_time(index);
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                for (size_t i = 0; i < property->keyframes.size(); ++i)
                    if (std::abs(property->keyframes[i].time - time) < 0.0001) {
                        property->set_temporal_handle(i, incoming, influence, speed, break_pair); break;
                    }
            }
        } else if (vector3) { activate_vector3(); vector3->set_temporal_handle(index, incoming, influence, speed, break_pair); }
        else if (vector) vector->set_temporal_handle(index, incoming, influence, speed, break_pair);
        else if (scalar) scalar->set_temporal_handle(index, incoming, influence, speed, break_pair);
    }
    void apply_easy_ease(size_t index, bool ease_in, bool ease_out)
    {
        if (is_hold_only()) return;
        if (is_extension()) {
            apply_temporal_mode(index, TemporalInterpolationMode::ManualBezier);
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            QJsonObject key = keys.at((qsizetype)index).toObject();
            if (ease_in) {
                key.insert(QStringLiteral("temporal_in_influence"), 33.3333333333);
                key.insert(QStringLiteral("temporal_in_speed"), 0.0);
            }
            if (ease_out) {
                key.insert(QStringLiteral("temporal_out_influence"), 33.3333333333);
                key.insert(QStringLiteral("temporal_out_speed"), 0.0);
            }
            key.insert(QStringLiteral("temporal_tangents_linked"), ease_in && ease_out);
            keys.replace((qsizetype)index, key);
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const double time = keyframe_time(index);
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                for (size_t i = 0; i < property->keyframes.size(); ++i)
                    if (std::abs(property->keyframes[i].time - time) < 0.0001) {
                        property->apply_easy_ease(i, ease_in, ease_out); break;
                    }
            }
        } else if (vector3) { activate_vector3(); vector3->apply_easy_ease(index, ease_in, ease_out); }
        else if (vector) vector->apply_easy_ease(index, ease_in, ease_out);
        else if (scalar) scalar->apply_easy_ease(index, ease_in, ease_out);
    }

    void apply_easing(size_t index, EasingType easing)
    {
        if (is_hold_only()) return;
        if (is_extension()) {
            QJsonArray keys = fxm::effects::animation::track_keys(*extension_effect, extension_path);
            QJsonObject key = keys.at(static_cast<qsizetype>(index)).toObject();
            fxm::effects::animation::set_key_easing(key, easing);
            // Legacy presets intentionally leave explicit velocity mode.
            key.insert(QStringLiteral("temporal_velocity_explicit"), false);
            key.insert(QStringLiteral("temporal_tangents_linked"), true);
            keys.replace(static_cast<qsizetype>(index), key);
            fxm::effects::animation::write_track_keys(*extension_effect, extension_path, keys);
        } else if (!scalar_group.empty()) {
            const double time = keyframe_time(index);
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                for (Keyframe &keyframe : property->keyframes) {
                    if (std::abs(keyframe.time - time) < 0.0001) {
                        apply_easing_preset(keyframe, easing);
                        break;
                    }
                }
            }
        } else if (vector3) { activate_vector3(); apply_easing_preset(vector3->keyframes[index], easing); }
        else if (vector) apply_easing_preset(vector->keyframes[index], easing);
        else if (scalar) apply_easing_preset(scalar->keyframes[index], easing);
    }
    void toggle_at(double time)
    {
        if (discrete) {
            const std::string value = discrete->evaluate(time);
            bool found = false;
            for (const auto &key : discrete->keyframes)
                if (std::abs(key.time - time) <= 1.0 / 240.0) { found = true; break; }
            if (found) discrete->remove(time); else discrete->set(time, value);
        } else if (is_extension()) {
            const QJsonValue fallback = extension_fallback.isUndefined()
                ? fxm::effects::animation::state_path_value(*extension_effect, extension_path)
                : extension_fallback;
            const QJsonValue value = fxm::effects::animation::evaluate_track(
                *extension_effect, extension_path, time, fallback);
            fxm::effects::animation::toggle_keyframe(
                *extension_effect, extension_path, time, value,
                extension_default_easing);
        } else if (!scalar_group.empty()) {
            const bool has_key = std::any_of(
                scalar_group.begin(), scalar_group.end(), [time](AnimatedProperty *property) {
                    return property && std::any_of(property->keyframes.begin(), property->keyframes.end(),
                        [time](const Keyframe &keyframe) {
                            return std::abs(keyframe.time - time) < 0.0001;
                        });
                });
            for (AnimatedProperty *property : scalar_group) {
                if (!property) continue;
                if (has_key) {
                    property->keyframes.erase(
                        std::remove_if(property->keyframes.begin(), property->keyframes.end(),
                            [time](const Keyframe &keyframe) {
                                return std::abs(keyframe.time - time) < 0.0001;
                            }),
                        property->keyframes.end());
                } else {
                    toggle_keyframe(*property, time, property->evaluate(time));
                }
            }
        } else if (vector3) {
            activate_vector3();
            toggle_keyframe(*vector3, time, vector3->evaluate(time));
        } else if (vector) {
            toggle_keyframe(*vector, time, vector->evaluate(time));
        } else if (scalar) {
            if (scalar->name == "camera_projection") {
                if (keyframe_at_time(*scalar, time)) {
                    remove_keyframe_at(*scalar, time);
                } else {
                    Keyframe key;
                    key.time = time;
                    key.value = scalar->evaluate(time) >= 0.5 ? 1.0 : 0.0;
                    key.easing = EasingType::Hold;
                    key.temporal_mode = TemporalInterpolationMode::Hold;
                    key.temporal_velocity_explicit = true;
                    scalar->keyframes.push_back(key);
                    std::sort(scalar->keyframes.begin(), scalar->keyframes.end(),
                              [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
                }
            } else {
                toggle_keyframe(*scalar, time, scalar->evaluate(time));
            }
        }
    }
};

static TimelinePropertyRef vector3_timeline_channel(AnimatedVec3Property &property,
                                                    int component,
                                                    const char *channel_name,
                                                    bool *enabled = nullptr)
{
    TimelinePropertyRef ref;
    ref.extension_name = channel_name;
    ref.vector3 = &property;
    ref.vector3_component = component;
    ref.vector3_enabled = enabled;
    return ref;
}

static TimelinePropertyRef scalar_group_timeline_property(
    const char *group_name, std::initializer_list<AnimatedProperty *> properties)
{
    TimelinePropertyRef ref;
    ref.extension_name = group_name;
    ref.scalar_group.assign(properties.begin(), properties.end());
    return ref;
}

static std::vector<TimelinePropertyRef> timeline_properties(Layer &layer)
{
    if (layer.type == LayerType::Audio) {
        return {
            {&layer.audio_volume_prop, nullptr},
            {&layer.audio_pan_prop, nullptr}
        };
    }

    std::vector<TimelinePropertyRef> props;
    if (layer.dimension_mode == LayerDimensionMode::ThreeD) {
        promote_layer_position_to_3d_path(layer, false);
        /* One authoritative timeline row per transform property.  The shared
         * XYZ key sequence remains available to the Graph Editor; component
         * zero is the initial graph channel selected by the aggregate row. */
        props.push_back(vector3_timeline_channel(
            layer.position_3d, 0, "position", &layer.position_3d_path_enabled));
        props.push_back({nullptr, &layer.scale});
        props.push_back(scalar_group_timeline_property(
            "rotation", {&layer.rotation_x, &layer.rotation_y, &layer.rotation}));
        props.push_back(scalar_group_timeline_property(
            "orientation", {&layer.orientation_x, &layer.orientation_y,
                              &layer.orientation_z}));
        props.push_back({&layer.material_ambient, nullptr});
        props.push_back({&layer.material_diffuse, nullptr});
        props.push_back({&layer.material_specular, nullptr});
        props.push_back({&layer.material_shininess, nullptr});
        props.push_back({&layer.material_metallic, nullptr});
        props.push_back({&layer.material_roughness, nullptr});
        props.push_back({&layer.material_reflection_intensity, nullptr});
        props.push_back(scalar_group_timeline_property(
            "material_emissive_color", {&layer.material_emissive_color_a,
                                         &layer.material_emissive_color_r,
                                         &layer.material_emissive_color_g,
                                         &layer.material_emissive_color_b}));
        props.push_back({&layer.material_emissive_intensity, nullptr});
        if (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
            layer.type == LayerType::Ticker || layer.type == LayerType::Shape) {
        }
    } else {
        props.push_back({nullptr, &layer.position});
        props.push_back({nullptr, &layer.scale});
        props.push_back({&layer.rotation, nullptr});
    }

    if (layer.type == LayerType::Light) {
        TitleLight &light = layer.light;
        props.push_back(vector3_timeline_channel(
            light.target, 0, "light_target"));
        props.push_back(scalar_group_timeline_property(
            "light_color", {&light.color_a, &light.color_r,
                            &light.color_g, &light.color_b}));
        props.push_back({&light.intensity, nullptr});
        props.push_back({&light.source_size, nullptr});
        props.push_back({&light.falloff_distance, nullptr});
        props.push_back({&light.cone_angle, nullptr});
        props.push_back({&light.cone_feather, nullptr});
        props.push_back({&light.shadow_darkness, nullptr});
        props.push_back({&light.shadow_softness, nullptr});
        props.push_back({&light.shadow_bias, nullptr});
    }

    const std::vector<TimelinePropertyRef> common_props {
        {&layer.opacity, nullptr},
        {&layer.stroke_offset_prop, nullptr},
        {nullptr, &layer.size},
        {nullptr, &layer.origin_prop},
        {&layer.paragraph_indent_left_prop, nullptr}, {&layer.paragraph_indent_right_prop, nullptr},
        {&layer.paragraph_indent_first_line_prop, nullptr},
        {&layer.ticker_completion_prop, nullptr},
        {&layer.font_size_prop, nullptr}, {&layer.char_scale_x_prop, nullptr}, {&layer.char_scale_y_prop, nullptr},
        {&layer.char_tracking_prop, nullptr}, {&layer.baseline_shift_prop, nullptr},
        {&layer.paragraph_space_before_prop, nullptr}, {&layer.paragraph_space_after_prop, nullptr},
        scalar_group_timeline_property("text_color", {&layer.text_color_a,
            &layer.text_color_r, &layer.text_color_g, &layer.text_color_b}),
        scalar_group_timeline_property("fill_color", {&layer.fill_color_a,
            &layer.fill_color_r, &layer.fill_color_g, &layer.fill_color_b}),
        scalar_group_timeline_property("stroke_color", {&layer.stroke_color_a,
            &layer.stroke_color_r, &layer.stroke_color_g, &layer.stroke_color_b}),
        {nullptr, nullptr, nullptr, QString(), std::string(), QJsonValue(),
         EasingType::Linear, {}, &layer.camera_assignment}
    };
    props.insert(props.end(), common_props.begin(), common_props.end());
    if (layer.type == LayerType::Chart) {
        for (AnimatedProperty *property : {
                 &layer.chart_animation_progress,
                 &layer.chart_reveal_progress,
                 &layer.chart_value_transition,
                 &layer.chart_label_opacity,
                 &layer.chart_element_opacity,
                 &layer.chart_font_size,
                 &layer.chart_line_thickness,
                 &layer.chart_bar_width,
                 &layer.chart_bar_spacing,
                 &layer.chart_corner_radius,
                 &layer.chart_padding,
                 &layer.chart_margin_left,
                 &layer.chart_margin_top,
                 &layer.chart_margin_right,
                 &layer.chart_margin_bottom})
            props.push_back({property, nullptr});
    }
    if (layer.type == LayerType::Video) {
        /* Development Version 243: source-time is a first-class scalar
         * animation track. Selecting it opens the same Graph Editor in
         * source-time value view or speed view, where negative speed is the
         * clear authoring indication for reverse sections. */
        props.push_back({&layer.video_source_time, nullptr, nullptr, QString(),
                         std::string("video_source_time")});
    }

    auto add_effect_props = [&](LayerEffect &effect, size_t effect_index) {
        QString effect_title = effect_type_name(effect.type);
        QString slug = effect_title;
        slug.replace(QStringLiteral(" "), QStringLiteral("_"));
        slug = slug.toLower();
        auto encoded_name = [&](const QString &suffix) {
            return QStringLiteral("__effect__%1__%2__%3")
                .arg(static_cast<qulonglong>(effect_index))
                .arg(slug).arg(suffix).toStdString();
        };
        auto name = [&](AnimatedProperty &prop, const char *suffix) {
            props.push_back({&prop, nullptr, nullptr, QString(),
                             encoded_name(QString::fromLatin1(suffix)), QJsonValue()});
        };
        auto name_group = [&](const char *suffix,
                              std::initializer_list<AnimatedProperty *> group) {
            props.push_back({nullptr, nullptr, nullptr, QString(),
                             encoded_name(QString::fromLatin1(suffix)), QJsonValue(),
                             EasingType::Linear,
                             std::vector<AnimatedProperty *>(group)});
        };
        name(effect.enabled_prop, "enabled");
        switch (effect.type) {
        case LayerEffectType::BackgroundColor:
            name(effect.opacity_prop, "opacity");
            name(effect.gradient_start_pos_prop, "gradient_start_pos");
            name(effect.gradient_end_pos_prop, "gradient_end_pos");
            name(effect.gradient_start_opacity_prop, "gradient_start_opacity");
            name(effect.gradient_end_opacity_prop, "gradient_end_opacity");
            name(effect.gradient_angle_prop, "gradient_angle");
            name(effect.gradient_center_x_prop, "gradient_center_x");
            name(effect.gradient_center_y_prop, "gradient_center_y");
            name(effect.gradient_scale_prop, "gradient_scale");
            name(effect.gradient_focal_x_prop, "gradient_focal_x");
            name(effect.gradient_focal_y_prop, "gradient_focal_y");
            name(effect.gradient_opacity_prop, "gradient_opacity");
            name_group("gradient_start_color", {&effect.gradient_start_color_a,
                &effect.gradient_start_color_r, &effect.gradient_start_color_g,
                &effect.gradient_start_color_b});
            name_group("gradient_end_color", {&effect.gradient_end_color_a,
                &effect.gradient_end_color_r, &effect.gradient_end_color_g,
                &effect.gradient_end_color_b});
            name(effect.padding_left_prop, "padding_left");
            name(effect.padding_right_prop, "padding_right");
            name(effect.padding_top_prop, "padding_top");
            name(effect.padding_bottom_prop, "padding_bottom");
            name(effect.corner_radius_tl_prop, "corner_tl");
            name(effect.corner_radius_tr_prop, "corner_tr");
            name(effect.corner_radius_br_prop, "corner_br");
            name(effect.corner_radius_bl_prop, "corner_bl");
            name(effect.stroke_width_prop, "stroke_width");
            name(effect.stroke_opacity_prop, "stroke_opacity");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            name_group("stroke_color", {&effect.stroke_color_a,
                &effect.stroke_color_r, &effect.stroke_color_g,
                &effect.stroke_color_b});
            break;
        case LayerEffectType::TrimPaths:
            name(effect.trim_start_prop, "trim_start");
            name(effect.trim_end_prop, "trim_end");
            name(effect.trim_offset_prop, "trim_offset");
            break;
        case LayerEffectType::Outline:
            name(effect.size_prop, "width");
            name(effect.opacity_prop, "opacity");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::DropShadow:
            name(effect.opacity_prop, "opacity");
            name(effect.distance_prop, "distance");
            name(effect.angle_prop, "angle");
            name(effect.size_prop, "blur");
            name(effect.spread_prop, "spread");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::InnerShadow:
            name(effect.opacity_prop, "opacity");
            name(effect.distance_prop, "distance");
            name(effect.angle_prop, "angle");
            name(effect.size_prop, "size");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::BrightnessContrast:
            name(effect.brightness_prop, "brightness");
            name(effect.contrast_prop, "contrast");
            break;
        case LayerEffectType::Saturation:
            name(effect.saturation_prop, "saturation");
            break;
        case LayerEffectType::LongShadow:
            name(effect.opacity_prop, "opacity");
            name(effect.distance_prop, "length");
            name(effect.angle_prop, "angle");
            name(effect.falloff_prop, "falloff");
            name(effect.size_prop, "blur");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::LensFlare:
            name(effect.opacity_prop, "opacity");
            name(effect.amount_prop, "amount");
            name(effect.scale_prop, "scale");
            name(effect.size_prop, "size");
            name(effect.spread_prop, "spread");
            name(effect.falloff_prop, "falloff");
            name(effect.angle_prop, "angle");
            name(effect.center_x_prop, "center_x");
            name(effect.center_y_prop, "center_y");
            name(effect.complexity_prop, "ghosts");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            name_group("secondary_color", {&effect.secondary_color_a,
                &effect.secondary_color_r, &effect.secondary_color_g,
                &effect.secondary_color_b});
            break;
        case LayerEffectType::Vignette:
            name(effect.opacity_prop, "opacity");
            name(effect.amount_prop, "amount");
            name(effect.scale_prop, "scale");
            name(effect.softness_prop, "softness");
            name(effect.roundness_prop, "roundness");
            name(effect.center_x_prop, "center_x");
            name(effect.center_y_prop, "center_y");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::Noise:
        case LayerEffectType::Grain:
        case LayerEffectType::FilmDistortion:
        case LayerEffectType::AnalogDistortion:
        case LayerEffectType::DigitalDistortion:
            name(effect.opacity_prop, "opacity");
            name(effect.amount_prop, "amount");
            name(effect.scale_prop, "scale");
            name(effect.softness_prop, "softness");
            name(effect.complexity_prop, "complexity");
            name(effect.evolution_prop, "evolution");
            name(effect.speed_prop, "speed");
            name(effect.roundness_prop, "aspect");
            name(effect.center_x_prop, "offset_x");
            name(effect.center_y_prop, "offset_y");
            name(effect.spread_prop, "lacunarity");
            name(effect.falloff_prop, "gain");
            name(effect.brightness_prop, "brightness");
            name(effect.contrast_prop, "contrast");
            name_group("channel_intensity", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::RoughenEdges:
            name(effect.opacity_prop, "opacity");
            name(effect.amount_prop, "amount");
            name(effect.scale_prop, "scale");
            name(effect.softness_prop, "softness");
            name(effect.complexity_prop, "complexity");
            name(effect.evolution_prop, "evolution");
            break;
        case LayerEffectType::ColorOverlay:
            name(effect.opacity_prop, "opacity");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::Glow:
        case LayerEffectType::InnerGlow:
            name(effect.opacity_prop, "opacity");
            name(effect.size_prop, "size");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::Blur:
        case LayerEffectType::MotionBlur:
            name(effect.opacity_prop, "amount");
            name(effect.size_prop, effect.type == LayerEffectType::MotionBlur
                ? "shutter_angle" : "size");
            break;
        case LayerEffectType::Bloom:
            name(effect.opacity_prop, "opacity");
            name(effect.spread_prop, "threshold");
            name(effect.size_prop, "size");
            name(effect.falloff_prop, "intensity");
            name_group("color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::Emboss:
            name(effect.size_prop, "depth");
            name(effect.distance_prop, "height");
            name(effect.angle_prop, "angle");
            name(effect.spread_prop, "softness");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::Sharpen:
        case LayerEffectType::UnsharpMask:
        case LayerEffectType::HighPass:
        case LayerEffectType::Clarity:
        case LayerEffectType::BilateralSharpen:
        case LayerEffectType::Glare:
        case LayerEffectType::Halation:
            name(effect.amount_prop, "amount");
            name(effect.size_prop, "radius");
            name(effect.softness_prop, "threshold");
            name(effect.spread_prop, "highlight_or_edge_protection");
            name(effect.falloff_prop, "shadow_protection");
            name(effect.brightness_prop, "midtone_bias");
            break;
        case LayerEffectType::LensDistortion:
            name(effect.roundness_prop, "distortion");
            name(effect.center_x_prop, "center_x");
            name(effect.center_y_prop, "center_y");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::ChromaticAberration:
            name(effect.amount_prop, "separation");
            name(effect.center_x_prop, "center_x");
            name(effect.center_y_prop, "center_y");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::DirectionalBlur:
            name(effect.size_prop, "radius");
            name(effect.angle_prop, "angle");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::ZoomBlur:
        case LayerEffectType::RadialBlur:
            name(effect.size_prop, "amount");
            name(effect.center_x_prop, "center_x");
            name(effect.center_y_prop, "center_y");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::Ripple:
        case LayerEffectType::WaveWarp:
            name(effect.amount_prop, "amplitude");
            name(effect.scale_prop, "frequency");
            name(effect.evolution_prop, "evolution");
            name(effect.angle_prop, "angle");
            name(effect.center_x_prop, "center_x");
            name(effect.center_y_prop, "center_y");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::Pixelate:
            name(effect.size_prop, "block_size");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::EdgeDetect:
            name(effect.amount_prop, "strength");
            name(effect.size_prop, "radius");
            name(effect.spread_prop, "threshold");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::Posterize:
            name(effect.complexity_prop, "levels");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::Threshold:
            name(effect.spread_prop, "threshold");
            name(effect.softness_prop, "feather");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::Scanlines:
            name(effect.amount_prop, "strength");
            name(effect.scale_prop, "spacing");
            name(effect.softness_prop, "softness");
            name(effect.angle_prop, "angle");
            name(effect.evolution_prop, "evolution");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::ChromaKey:
            name(effect.amount_prop, "similarity");
            name(effect.softness_prop, "smoothness");
            name(effect.spread_prop, "spill_suppression");
            name(effect.falloff_prop, "edge_recovery");
            name(effect.opacity_prop, "opacity");
            name(effect.color_a, "key_color.a");
            name(effect.color_r, "key_color.r");
            name(effect.color_g, "key_color.g");
            name(effect.color_b, "key_color.b");
            break;
        case LayerEffectType::LumaKey:
            name(effect.amount_prop, "threshold");
            name(effect.softness_prop, "feather");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::ColorRange:
            name(effect.amount_prop, "tolerance");
            name(effect.softness_prop, "feather");
            name(effect.opacity_prop, "opacity");
            name(effect.color_a, "key_color.a");
            name(effect.color_r, "key_color.r");
            name(effect.color_g, "key_color.g");
            name(effect.color_b, "key_color.b");
            break;
        case LayerEffectType::SpillSuppression:
            name(effect.amount_prop, "strength");
            name(effect.softness_prop, "range");
            name(effect.opacity_prop, "opacity");
            name(effect.color_a, "spill_color.a");
            name(effect.color_r, "spill_color.r");
            name(effect.color_g, "spill_color.g");
            name(effect.color_b, "spill_color.b");
            break;
        case LayerEffectType::MatteChoker:
            name(effect.amount_prop, "choke");
            name(effect.size_prop, "radius");
            name(effect.softness_prop, "feather");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::LightWrap:
            name(effect.size_prop, "radius");
            name(effect.amount_prop, "intensity");
            name(effect.spread_prop, "edge_width");
            name(effect.falloff_prop, "foreground_luminance_protection");
            name(effect.opacity_prop, "opacity");
            name_group("spill_color", {&effect.color_a, &effect.color_r,
                &effect.color_g, &effect.color_b});
            break;
        case LayerEffectType::DisplacementMap:
            name(effect.amount_prop, "horizontal_amount");
            name(effect.distance_prop, "vertical_amount");
            name(effect.opacity_prop, "opacity");
            break;
        case LayerEffectType::FourColorGradient:
            break;
        default:
            break;
        }

        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty()) catalog.reload();
        const auto *definition = effect.extension_id.empty()
            ? nullptr : catalog.find(QString::fromStdString(effect.extension_id));
        const bool schema_driven = definition &&
            (!definition->builtIn || effect.type == LayerEffectType::FourColorGradient);
        QSet<QString> extension_paths;
        if (schema_driven) {
            for (auto it = definition->parameterSchema.begin();
                 it != definition->parameterSchema.end(); ++it) {
                const QJsonObject meta = it.value().toObject();
                if (!meta.value(QStringLiteral("animatable")).toBool(false)) continue;
                extension_paths.insert(it.key());
                const QJsonValue fallback = fxm::effects::animation::state_path_value(effect, it.key()).isUndefined()
                    ? meta.value(QStringLiteral("default"))
                    : fxm::effects::animation::state_path_value(effect, it.key());
                const QString declared_type = meta.value(QStringLiteral("type")).toString();
                const EasingType default_easing =
                    (declared_type == QStringLiteral("bool") ||
                     declared_type == QStringLiteral("enum"))
                        ? EasingType::Hold : EasingType::Linear;
                props.push_back({nullptr, nullptr, &effect, it.key(),
                    encoded_name(it.key()), fallback, default_easing});
            }
        }
        const QJsonObject existing_tracks = fxm::effects::animation::tracks_object(effect);
        for (auto it = existing_tracks.begin(); it != existing_tracks.end(); ++it) {
            if (it.value().toArray().isEmpty() || extension_paths.contains(it.key())) continue;
            const QJsonArray track = it.value().toArray();
            const EasingType default_easing = track.isEmpty()
                ? EasingType::Linear
                : fxm::effects::animation::easing_from_key(track.at(0).toObject());
            props.push_back({nullptr, nullptr, &effect, it.key(),
                encoded_name(it.key()),
                fxm::effects::animation::state_path_value(effect, it.key()),
                default_easing});
        }
    };
    for (size_t index = 0; index < layer.effects.size(); ++index)
        add_effect_props(layer.effects[index], index);

    /* Text Animator tracks are ordinary AnimatedProperty instances. Expose
     * them through the same TimelinePropertyRef adapter used by layer/effect
     * properties so copy/paste, graph editing, temporal Bezier handles and
     * multi-keyframe operations require no separate timeline implementation. */
    auto encoded_text_name = [](size_t animator_index,
                                const std::string &animator_name,
                                const char *kind,
                                size_t item_index,
                                const std::string &item_name,
                                const char *component) {
        auto clean = [](std::string value) {
            size_t pos = 0;
            while ((pos = value.find("__", pos)) != std::string::npos)
                value.replace(pos, 2, "_");
            return value;
        };
        return std::string("__textanim__") + std::to_string(animator_index) +
               "__" + clean(animator_name) + "__" + kind + "__" +
               std::to_string(item_index) + "__" + clean(item_name) +
               "__" + component;
    };
    auto add_text_scalar = [&](AnimatedProperty &property,
                               const std::string &encoded) {
        props.push_back({&property, nullptr, nullptr, QString(), encoded,
                         QJsonValue(), EasingType::Linear});
    };
    auto add_text_group = [&](std::initializer_list<AnimatedProperty *> group,
                              const std::string &encoded) {
        props.push_back({nullptr, nullptr, nullptr, QString(), encoded,
                         QJsonValue(), EasingType::Linear,
                         std::vector<AnimatedProperty *>(group)});
    };

    for (size_t animator_index = 0;
         animator_index < layer.text_animators.animators.size();
         ++animator_index) {
        TextAnimator &animator = layer.text_animators.animators[animator_index];
        for (size_t property_index = 0;
             property_index < animator.properties.size(); ++property_index) {
            TextAnimatorProperty &property = animator.properties[property_index];
            const auto name = [&](const char *component) {
                return encoded_text_name(animator_index, animator.name,
                                         "property", property_index,
                                         property.name, component);
            };
            switch (property.type) {
            case TextAnimatorPropertyType::Position:
            case TextAnimatorPropertyType::AnchorPoint:
            case TextAnimatorPropertyType::Scale:
            case TextAnimatorPropertyType::ShadowOffset:
            case TextAnimatorPropertyType::TextBoxPositionOffset:
            case TextAnimatorPropertyType::TextBoxScale:
            case TextAnimatorPropertyType::LineAlignmentOffset:
                add_text_group({&property.value, &property.secondary}, name("xy"));
                break;
            case TextAnimatorPropertyType::FillColor:
            case TextAnimatorPropertyType::StrokeColor:
                add_text_group({&property.value, &property.secondary,
                                &property.tertiary, &property.quaternary},
                               name("color"));
                break;
            default:
                add_text_scalar(property.value, name("value"));
                break;
            }
        }

        for (size_t selector_index = 0;
             selector_index < animator.selectors.size(); ++selector_index) {
            TextSelector &selector = animator.selectors[selector_index];
            auto add_selector = [&](AnimatedProperty &property,
                                    const char *component) {
                add_text_scalar(property,
                    encoded_text_name(animator_index, animator.name,
                                      "selector", selector_index,
                                      selector.name, component));
            };
            add_selector(selector.start, "start");
            add_selector(selector.end, "end");
            add_selector(selector.offset, "offset");
            add_selector(selector.amount, "amount");
            add_selector(selector.ease_high, "ease_high");
            add_selector(selector.ease_low, "ease_low");
            add_selector(selector.smoothness, "smoothness");
            add_selector(selector.completion, "completion");
            add_selector(selector.stagger_percent, "stagger");
            add_selector(selector.amplitude, "amplitude");
            add_selector(selector.frequency, "frequency");
            add_selector(selector.phase, "phase");
            add_selector(selector.speed, "speed");
            add_selector(selector.falloff, "falloff");
            add_selector(selector.minimum, "minimum");
            add_selector(selector.maximum, "maximum");
            add_selector(selector.custom_index, "custom_index");
            add_selector(selector.wiggly_amount, "wiggly_amount");
            add_selector(selector.wiggly_frequency, "wiggly_frequency");
            add_selector(selector.correlation, "correlation");
            add_selector(selector.temporal_phase, "temporal_phase");
            add_selector(selector.spatial_phase, "spatial_phase");
            add_selector(selector.minimum_influence, "minimum_influence");
            add_selector(selector.maximum_influence, "maximum_influence");
        }
    }
    return props;
}


static std::string camera_timeline_owner_id(const std::string &camera_id)
{
    return std::string("camera:") + camera_id;
}

static constexpr const char *kTitleCameraSwitchOwner = "title:camera-switch";

static TitleCamera *timeline_camera_from_owner(Title &title, const std::string &owner_id)
{
    static const std::string prefix = "camera:";
    if (owner_id.rfind(prefix, 0) != 0) return nullptr;
    const std::string camera_id = owner_id.substr(prefix.size());
    for (auto &camera : title.cameras)
        if (camera.id == camera_id) return &camera;
    return nullptr;
}

static const TitleCamera *timeline_camera_from_owner(const Title &title, const std::string &owner_id)
{
    static const std::string prefix = "camera:";
    if (owner_id.rfind(prefix, 0) != 0) return nullptr;
    const std::string camera_id = owner_id.substr(prefix.size());
    for (const auto &camera : title.cameras)
        if (camera.id == camera_id) return &camera;
    return nullptr;
}

static std::vector<TimelinePropertyRef> timeline_camera_properties(TitleCamera &camera)
{
    promote_camera_spatial_tracks(camera, false);
    return {
        vector3_timeline_channel(camera.position_3d, 0, "camera_position",
                                 &camera.position_3d_path_enabled),
        vector3_timeline_channel(camera.target_3d, 0, "camera_target",
                                 &camera.target_3d_path_enabled),
        scalar_group_timeline_property(
            "camera_orientation", {&camera.orientation_x, &camera.orientation_y,
                                    &camera.orientation_z}),
        scalar_group_timeline_property(
            "camera_rotation", {&camera.rotation_x, &camera.rotation_y,
                                 &camera.rotation_z}),
        {&camera.focal_length, nullptr}, {&camera.field_of_view, nullptr},
        {&camera.zoom, nullptr}, {&camera.near_clip, nullptr},
        {&camera.far_clip, nullptr}, {&camera.projection_mode, nullptr}
    };
}

static std::string light_timeline_owner_id(const std::string &light_id)
{
    return std::string("light:") + light_id;
}

static TitleLight *timeline_light_from_owner(Title &title,
                                              const std::string &owner_id)
{
    static const std::string prefix = "light:";
    if (owner_id.rfind(prefix, 0) != 0) return nullptr;
    const std::string light_id = owner_id.substr(prefix.size());
    for (auto &light : title.lights)
        if (light.id == light_id) return &light;
    return nullptr;
}

static const TitleLight *timeline_light_from_owner(
    const Title &title, const std::string &owner_id)
{
    static const std::string prefix = "light:";
    if (owner_id.rfind(prefix, 0) != 0) return nullptr;
    const std::string light_id = owner_id.substr(prefix.size());
    for (const auto &light : title.lights)
        if (light.id == light_id) return &light;
    return nullptr;
}

static std::vector<TimelinePropertyRef> timeline_light_properties(
    TitleLight &light)
{
    return {
        vector3_timeline_channel(light.position, 0, "light_position"),
        vector3_timeline_channel(light.target, 0, "light_target"),
        scalar_group_timeline_property(
            "light_color", {&light.color_a, &light.color_r,
                            &light.color_g, &light.color_b}),
        {&light.intensity, nullptr},
        {&light.source_size, nullptr},
        {&light.falloff_distance, nullptr},
        {&light.cone_angle, nullptr},
        {&light.cone_feather, nullptr},
        {&light.shadow_darkness, nullptr},
        {&light.shadow_softness, nullptr},
        {&light.shadow_bias, nullptr},
        {&light.environment_rotation, nullptr}
    };
}

static TimelinePropertyRef timeline_property_for_owner(Title &title,
                                                        const std::string &owner_id,
                                                        const std::string &property_name)
{
    auto properties_for_owner = [&]() {
        std::vector<TimelinePropertyRef> properties;
        if (owner_id == kTitleCameraSwitchOwner) {
            TimelinePropertyRef result;
            result.discrete = &title.active_camera;
            properties.push_back(result);
            return properties;
        }
        if (TitleCamera *camera = timeline_camera_from_owner(title, owner_id))
            return timeline_camera_properties(*camera);
        if (TitleLight *light = timeline_light_from_owner(title, owner_id))
            return timeline_light_properties(*light);
        if (auto layer = title.find_layer(owner_id))
            return timeline_properties(*layer);
        return properties;
    };
    for (auto property : properties_for_owner())
        if (property.name() == property_name) return property;
    return {};
}

static std::vector<TimelinePropertyRef> timeline_properties_for_owner(
    Title &title, const std::string &owner_id)
{
    if (owner_id == kTitleCameraSwitchOwner) {
        TimelinePropertyRef result;
        result.discrete = &title.active_camera;
        return {result};
    }
    if (TitleCamera *camera = timeline_camera_from_owner(title, owner_id))
        return timeline_camera_properties(*camera);
    if (TitleLight *light = timeline_light_from_owner(title, owner_id))
        return timeline_light_properties(*light);
    if (auto layer = title.find_layer(owner_id))
        return timeline_properties(*layer);
    return {};
}

static double timeline_owner_in_time(const Title &title, const std::string &owner_id)
{
    if (owner_id == kTitleCameraSwitchOwner ||
        timeline_camera_from_owner(title, owner_id) ||
        timeline_light_from_owner(title, owner_id)) return 0.0;
    if (auto layer = title.find_layer(owner_id)) return layer->in_time;
    return 0.0;
}

static double timeline_owner_out_time(const Title &title, const std::string &owner_id)
{
    if (owner_id == kTitleCameraSwitchOwner ||
        timeline_camera_from_owner(title, owner_id) ||
        timeline_light_from_owner(title, owner_id)) return title.duration;
    if (auto layer = title.find_layer(owner_id)) return layer->out_time;
    return title.duration;
}

static bool timeline_owner_locked(const Title &title, const std::string &owner_id)
{
    if (auto layer = title.find_layer(owner_id)) return layer->locked;
    return false;
}

static std::shared_ptr<Layer> timeline_owner_proxy(const Title &title,
                                                   const std::string &owner_id,
                                                   const QString &label)
{
    if (auto layer = title.find_layer(owner_id)) return layer;
    auto proxy = std::make_shared<Layer>();
    proxy->id = owner_id;
    proxy->name = label.toStdString();
    proxy->in_time = 0.0;
    proxy->out_time = title.duration;
    proxy->locked = false;
    proxy->visible = true;
    return proxy;
}

static QString property_label(const std::string &name)
{
    if (name == "active_camera") return QStringLiteral("Active Camera");
    if (name == "camera_assignment") return QStringLiteral("Camera Assignment");
    if (name == "light_position") return QStringLiteral("Position");
    if (name == "light_target") return QStringLiteral("Point of Interest");
    if (name == "light_color") return QStringLiteral("Color");
    if (name == "light_intensity") return QStringLiteral("Intensity");
    if (name == "light_source_size") return QStringLiteral("Source Size");
    if (name == "light_falloff_distance") return QStringLiteral("Falloff Distance");
    if (name == "light_cone_angle") return QStringLiteral("Cone Angle");
    if (name == "light_cone_feather") return QStringLiteral("Cone Feather");
    if (name == "light_shadow_darkness") return QStringLiteral("Shadow Darkness");
    if (name == "light_shadow_softness") return QStringLiteral("Shadow Softness");
    if (name == "light_shadow_bias") return QStringLiteral("Shadow Bias");
    if (name == "light_environment_rotation") return QStringLiteral("Environment Rotation");
    if (name == "material_ambient") return QStringLiteral("Material Ambient");
    if (name == "material_diffuse") return QStringLiteral("Material Diffuse");
    if (name == "material_specular") return QStringLiteral("Material Specular");
    if (name == "material_shininess") return QStringLiteral("Material Shininess");
    if (name == "material_metallic") return QStringLiteral("Material Metallic");
    if (name == "material_roughness") return QStringLiteral("Material Roughness");
    if (name == "material_reflection_intensity") return QStringLiteral("Reflection Intensity");
    if (name == "material_emissive_color") return QStringLiteral("Emissive Color");
    if (name == "material_emissive_intensity") return QStringLiteral("Emission Intensity");
    if (name == "geometry_extrusion_depth") return QStringLiteral("Extrusion Depth");
    if (name == "geometry_bevel_depth") return QStringLiteral("Bevel Depth");
    if (name == "position_x") return QStringLiteral("Position X");
    if (name == "position_y") return QStringLiteral("Position Y");
    if (name == "orientation") return QStringLiteral("Orientation");
    if (name == "camera_position") return QStringLiteral("Position");
    if (name == "camera_target") return QStringLiteral("Point of Interest");
    if (name == "camera_orientation") return QStringLiteral("Orientation");
    if (name == "camera_rotation") return QStringLiteral("Rotation");
    if (name == "position_z") return QStringLiteral("Position Z");
    if (name == "camera_position_x") return QStringLiteral("Camera Position X");
    if (name == "camera_position_y") return QStringLiteral("Camera Position Y");
    if (name == "camera_position_z") return QStringLiteral("Camera Position Z");
    if (name == "camera_target_x") return QStringLiteral("Point of Interest X");
    if (name == "camera_target_y") return QStringLiteral("Point of Interest Y");
    if (name == "camera_target_z") return QStringLiteral("Point of Interest Z");
    if (name == "camera_orientation_x") return QStringLiteral("Camera Orientation X");
    if (name == "camera_orientation_y") return QStringLiteral("Camera Orientation Y");
    if (name == "camera_orientation_z") return QStringLiteral("Camera Orientation Z");
    if (name == "camera_rotation_x") return QStringLiteral("Camera Rotation X");
    if (name == "camera_rotation_y") return QStringLiteral("Camera Rotation Y");
    if (name == "camera_rotation_z") return QStringLiteral("Camera Rotation Z");
    if (name == "camera_focal_length") return QStringLiteral("Focal Length");
    if (name == "camera_field_of_view") return QStringLiteral("Field of View");
    if (name == "camera_zoom") return QStringLiteral("Camera Zoom");
    if (name == "camera_near_clip") return QStringLiteral("Near Clipping Plane");
    if (name == "camera_far_clip") return QStringLiteral("Far Clipping Plane");
    if (name == "camera_projection") return QStringLiteral("Projection Mode");
    QString qname = QString::fromStdString(name);
    QString encoded_effect_slug;
    QString encoded_effect_suffix;
    if (qname.startsWith(QStringLiteral("__textanim__"))) {
        const QStringList parts = qname.split(QStringLiteral("__"), Qt::KeepEmptyParts);
        if (parts.size() >= 8) {
            const QString animator_name = parts.at(3);
            const QString item_name = parts.at(6);
            QString component = parts.mid(7).join(QStringLiteral("__"));
            component.replace(QLatin1Char('_'), QLatin1Char(' '));
            if (component == QStringLiteral("value") ||
                component == QStringLiteral("xy") ||
                component == QStringLiteral("color"))
                component.clear();
            else if (!component.isEmpty())
                component[0] = component.at(0).toUpper();
            return QStringLiteral("Text › %1 › %2%3")
                .arg(animator_name, item_name,
                     component.isEmpty() ? QString()
                         : QStringLiteral(" › ") + component);
        }
    }
    if (qname.startsWith(QStringLiteral("__effect__"))) {
        const QStringList parts = qname.split(QStringLiteral("__"), Qt::KeepEmptyParts);
        if (parts.size() >= 5) {
            encoded_effect_slug = parts.at(3);
            encoded_effect_suffix = parts.mid(4).join(QStringLiteral("__"));
            qname = encoded_effect_slug + QStringLiteral("_") + encoded_effect_suffix;
        }
    }
    auto effect_label = [&](const QString &prefix, const QString &label) -> QString {
        QString title = prefix;
        title.replace(QStringLiteral("_"), QStringLiteral(" "));
        for (int i = 0; i < title.size(); ++i)
            if (i == 0 || title.at(i - 1).isSpace())
                title[i] = title.at(i).toUpper();
        return title + QStringLiteral(" ") + label;
    };
    if (qname.startsWith(QStringLiteral("background_color_"))) {
        QString suffix = qname.mid(QStringLiteral("background_color_").size());
        if (suffix == QStringLiteral("enabled")) return effect_label(fxm_tr("OBSTitles.BackgroundColor"), fxm_tr("OBSTitles.Enabled"));
        if (suffix == QStringLiteral("opacity")) return effect_label(fxm_tr("OBSTitles.BackgroundColor"), fxm_tr("OBSTitles.Opacity"));
        if (suffix.startsWith(QStringLiteral("padding_"))) return effect_label(fxm_tr("OBSTitles.BackgroundColor"), suffix.replace(QStringLiteral("_"), QStringLiteral(" ")));
        if (suffix.startsWith(QStringLiteral("corner_"))) return effect_label(fxm_tr("OBSTitles.BackgroundColor"), suffix.replace(QStringLiteral("_"), QStringLiteral(" ")));
        if (suffix.startsWith(QStringLiteral("stroke_"))) return effect_label(fxm_tr("OBSTitles.BackgroundColor"), suffix.replace(QStringLiteral("_"), QStringLiteral(" ")));
        if (suffix.startsWith(QStringLiteral("color_"))) return effect_label(fxm_tr("OBSTitles.BackgroundColor"), fxm_tr("OBSTitles.Color"));
    }
    if (qname.startsWith(QStringLiteral("outline_"))) {
        QString suffix = qname.mid(QStringLiteral("outline_").size());
        if (suffix == QStringLiteral("enabled")) return effect_label(fxm_tr("OBSTitles.Outline"), fxm_tr("OBSTitles.Enabled"));
        if (suffix == QStringLiteral("width")) return effect_label(fxm_tr("OBSTitles.Outline"), fxm_tr("OBSTitles.Width"));
        if (suffix == QStringLiteral("opacity")) return effect_label(fxm_tr("OBSTitles.Outline"), fxm_tr("OBSTitles.Opacity"));
        if (suffix.startsWith(QStringLiteral("color_"))) return effect_label(fxm_tr("OBSTitles.Outline"), fxm_tr("OBSTitles.Color"));
    }
    for (const QString &prefix : {QStringLiteral("drop_shadow_"), QStringLiteral("long_shadow_"), QStringLiteral("inner_shadow_")}) {
        if (!qname.startsWith(prefix)) continue;
        QString suffix = qname.mid(prefix.size()).replace(QStringLiteral("_"), QStringLiteral(" "));
        QString title = prefix.left(prefix.size() - 1);
        title.replace(QStringLiteral("_"), QStringLiteral(" "));
        return effect_label(title, suffix);
    }
    if (name == "position") return fxm_tr("OBSTitles.Position");
    if (name == "scale") return fxm_tr("OBSTitles.Scale");
    if (name == "size") return fxm_tr("OBSTitles.Size");
    if (name == "origin") return fxm_tr("OBSTitles.Origin");
    if (name == "paragraph_indent_left") return fxm_tr("OBSTitles.ParagraphIndentLeft");
    if (name == "paragraph_indent_right") return fxm_tr("OBSTitles.ParagraphIndentRight");
    if (name == "paragraph_indent_first_line") return fxm_tr("OBSTitles.ParagraphIndentFirstLine");
    if (name == "font_size") return fxm_tr("OBSTitles.Size");
    if (name == "char_scale_x") return fxm_tr("OBSTitles.HScale");
    if (name == "char_scale_y") return fxm_tr("OBSTitles.VScale");
    if (name == "char_tracking") return fxm_tr("OBSTitles.Tracking");
    if (name == "baseline_shift") return fxm_tr("OBSTitles.Baseline");
    if (name == "paragraph_space_before") return fxm_tr("OBSTitles.ParagraphSpaceBefore");
    if (name == "paragraph_space_after") return fxm_tr("OBSTitles.ParagraphSpaceAfter");
    if (name == "text_color" || name == "text_color_a" || name == "text_color_r" ||
        name == "text_color_g" || name == "text_color_b") return fxm_tr("OBSTitles.TextColor");
    if (name == "fill_color" || name == "fill_color_a" || name == "fill_color_r" ||
        name == "fill_color_g" || name == "fill_color_b") return fxm_tr("OBSTitles.FillColor");
    if (name == "stroke_color" || name == "stroke_color_a" || name == "stroke_color_r" ||
        name == "stroke_color_g" || name == "stroke_color_b") return fxm_tr("OBSTitles.StrokeColor");
    if (name == "shadow_color_a" || name == "shadow_color_r" ||
        name == "shadow_color_g" || name == "shadow_color_b") return fxm_tr("OBSTitles.ShadowColor");
    if (name == "background_color_a" || name == "background_color_r" ||
        name == "background_color_g" || name == "background_color_b") return fxm_tr("OBSTitles.BackgroundColor");
    if (name == "background_enabled") return fxm_tr("OBSTitles.EnableColorBackground");
    if (name == "background_opacity") return fxm_tr("OBSTitles.BackgroundOpacityLabel");
    if (name == "background_padding_x") return fxm_tr("OBSTitles.BackgroundHorizontalPaddingLabel");
    if (name == "background_padding_y") return fxm_tr("OBSTitles.BackgroundVerticalPaddingLabel");
    if (name == "background_padding_left") return fxm_tr("OBSTitles.LeftPadding");
    if (name == "background_padding_right") return fxm_tr("OBSTitles.RightPadding");
    if (name == "background_padding_top") return fxm_tr("OBSTitles.TopPadding");
    if (name == "background_padding_bottom") return fxm_tr("OBSTitles.BottomPadding");
    if (name == "background_corner_radius") return fxm_tr("OBSTitles.BackgroundCornerLabel");
    if (name == "background_corner_radius_tl") return fxm_tr("OBSTitles.CornerTL");
    if (name == "background_corner_radius_tr") return fxm_tr("OBSTitles.CornerTR");
    if (name == "background_corner_radius_br") return fxm_tr("OBSTitles.CornerBR");
    if (name == "background_corner_radius_bl") return fxm_tr("OBSTitles.CornerBL");
    if (name == "stroke_offset") return fxm_tr("OBSTitles.StrokeOffset");
    if (name == "background_stroke_width") return fxm_tr("OBSTitles.BackgroundStrokeWidth");
    if (name == "background_stroke_opacity") return fxm_tr("OBSTitles.BackgroundStrokeOpacity");
    if (name == "background_stroke_color_a" || name == "background_stroke_color_r" || name == "background_stroke_color_g" || name == "background_stroke_color_b") return fxm_tr("OBSTitles.BackgroundStroke");
    if (name == "shadow_enabled") return fxm_tr("OBSTitles.ShadowEnable");
    if (name == "shadow_opacity") return fxm_tr("OBSTitles.ShadowOpacity");
    if (name == "shadow_distance") return fxm_tr("OBSTitles.ShadowDistance");
    if (name == "shadow_angle") return fxm_tr("OBSTitles.ShadowAngle");
    if (name == "shadow_blur") return fxm_tr("OBSTitles.ShadowBlur");
    if (name == "shadow_spread") return fxm_tr("OBSTitles.ShadowSpread");
    if (name == "position_z") return QStringLiteral("Position Z");
    if (name == "rotation_x") return QStringLiteral("Rotation X");
    if (name == "rotation_y") return QStringLiteral("Rotation Y");
    if (name == "scale_z") return QStringLiteral("Scale Z");
    if (name == "anchor_z") return QStringLiteral("Anchor Z");
    if (name == "orientation_x") return QStringLiteral("Orientation X");
    if (name == "orientation_y") return QStringLiteral("Orientation Y");
    if (name == "orientation_z") return QStringLiteral("Orientation Z");
    if (name == "rotation") return fxm_tr("OBSTitles.Rotation");
    if (name == "opacity") return fxm_tr("OBSTitles.Opacity");
    if (!encoded_effect_slug.isEmpty()) {
        QString suffix = encoded_effect_suffix;
        if (suffix.startsWith(QStringLiteral("elements."))) {
            const QStringList element_parts = suffix.split(QLatin1Char('.'));
            if (element_parts.size() == 3)
                suffix = QStringLiteral("Element %1 %2")
                    .arg(element_parts.at(1).toInt() + 1)
                    .arg(element_parts.at(2));
        }
        suffix.replace(QLatin1Char('.'), QLatin1Char(' '));
        suffix.replace(QLatin1Char('_'), QLatin1Char(' '));
        return effect_label(encoded_effect_slug, suffix);
    }
    return QString::fromStdString(name);
}

static QString property_value_text(const AnimatedProperty &prop, const Layer &layer)
{
    double value = prop.static_value;
    if (prop.name == "pos_x")
        return QString("%1,%2").arg(layer.position.static_value.x, 0, 'f', 1)
                                .arg(layer.position.static_value.y, 0, 'f', 1);
    if (prop.name == "scale_x")
        return QString("%1,%2%").arg(layer.scale.static_value.x * 100.0, 0, 'f', 1)
                                 .arg(layer.scale.static_value.y * 100.0, 0, 'f', 1);
    if (prop.name == "box_width")
        return QString("%1 × %2").arg(layer.size.static_value.x, 0, 'f', 0)
                                  .arg(layer.size.static_value.y, 0, 'f', 0);
    if (prop.name == "origin_x")
        return QString("%1,%2").arg(layer.origin_prop.static_value.x, 0, 'f', 2)
                                .arg(layer.origin_prop.static_value.y, 0, 'f', 2);
    if (prop.name == "char_scale_x" || prop.name == "char_scale_y" ||
        prop.name == "scale_z") value *= 100.0;
    if (prop.name == "opacity" || prop.name == "shadow_opacity" || prop.name == "background_opacity") value *= 100.0;
    if (prop.name == "shadow_enabled" || prop.name == "background_enabled") return value >= 0.5 ? fxm_tr("OBSTitles.On") : fxm_tr("OBSTitles.Off");
    return QString::number(value, 'f', (prop.name == "opacity" || prop.name == "shadow_opacity" || prop.name == "background_opacity") ? 1 : 2);
}

static QString property_value_text(const TimelinePropertyRef &prop, const Layer &layer)
{
    if (prop.vector3) {
        const Vec3Value value = prop.vector3->static_value;
        if (prop.name() == "position" || prop.name() == "position_3d") {
            return QString("%1,%2,%3")
                .arg(value.x, 0, 'f', 1)
                .arg(value.y, 0, 'f', 1)
                .arg(value.z, 0, 'f', 1);
        }
        return QString("%1,%2,%3")
            .arg(value.x, 0, 'f', 2)
            .arg(value.y, 0, 'f', 2)
            .arg(value.z, 0, 'f', 2);
    }
    if (!prop.vector)
        return prop.scalar ? property_value_text(*prop.scalar, layer) : QString();

    if (prop.name() == "position")
        return QString("%1,%2").arg(layer.position.static_value.x, 0, 'f', 1)
                                .arg(layer.position.static_value.y, 0, 'f', 1);
    if (prop.name() == "scale")
        return QString("%1,%2%").arg(layer.scale.static_value.x * 100.0, 0, 'f', 1)
                                 .arg(layer.scale.static_value.y * 100.0, 0, 'f', 1);
    if (prop.name() == "size")
        return QString("%1 x %2").arg(layer.size.static_value.x, 0, 'f', 0)
                                  .arg(layer.size.static_value.y, 0, 'f', 0);
    if (prop.name() == "origin")
        return QString("%1,%2").arg(layer.origin_prop.static_value.x, 0, 'f', 2)
                                .arg(layer.origin_prop.static_value.y, 0, 'f', 2);
    return QString::fromStdString(prop.name());
}

struct LayerHierarchyRow {
    std::shared_ptr<Layer> layer;
    int depth = 0;
};

static bool layer_has_collapsed_group_ancestor(const std::shared_ptr<Title> &title,
                                               const Layer &layer)
{
    if (!title) return false;
    std::string parent_id = layer.parent_id;
    std::set<std::string> visited;
    while (!parent_id.empty() && visited.insert(parent_id).second) {
        auto parent = title->find_layer(parent_id);
        if (!parent || !layer_type_can_have_children(parent->type))
            break;
        if (parent->group_collapsed)
            return true;
        parent_id = parent->parent_id;
    }
    return false;
}

/* Shared hierarchy model for the Layers panel and timeline.  The source model
 * remains bottom-to-top; this helper returns the visible UI order (top-to-bottom)
 * and hides descendants of collapsed groups.  Malformed legacy/cyclic rows are
 * still surfaced, but never by bypassing an intentional collapsed ancestor. */
static std::vector<LayerHierarchyRow> visible_layer_hierarchy_rows(
    const std::shared_ptr<Title> &title)
{
    std::vector<LayerHierarchyRow> rows;
    if (!title) return rows;
    const bool show_transition_inputs =
        title->graphic_type == TitleGraphicType::Stinger &&
        title->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation;
    auto layer_is_editor_visible = [show_transition_inputs](const std::shared_ptr<Layer> &layer) {
        return layer && (!layer_type_is_transition_input(layer->type) || show_transition_inputs);
    };

    std::map<std::string, std::vector<std::shared_ptr<Layer>>> children;
    auto valid_hierarchy_parent = [&](const std::shared_ptr<Layer> &layer) {
        if (!layer || layer->parent_id.empty()) return false;
        const auto parent = title->find_layer(layer->parent_id);
        if (!parent) return false;
        if (layer_type_is_container(parent->type)) return true;
        return parent->type == LayerType::Video &&
               layer->type == LayerType::Audio &&
               layer->linked_media_stream &&
               layer->linked_media_layer_id == parent->id;
    };
    for (auto it = title->layers.rbegin(); it != title->layers.rend(); ++it) {
        const auto &layer = *it;
        if (layer_is_editor_visible(layer) && valid_hierarchy_parent(layer))
            children[layer->parent_id].push_back(layer);
    }

    std::set<std::string> emitted;
    std::function<void(const std::shared_ptr<Layer> &, int)> append;
    append = [&](const std::shared_ptr<Layer> &layer, int depth) {
        if (!layer_is_editor_visible(layer) || !emitted.insert(layer->id).second)
            return;
        rows.push_back({layer, depth});
        if (layer->type == LayerType::Asset ||
            !layer_type_can_have_children(layer->type) || layer->group_collapsed)
            return;
        const auto found = children.find(layer->id);
        if (found == children.end())
            return;
        for (const auto &child : found->second)
            append(child, depth + 1);
    };

    for (auto it = title->layers.rbegin(); it != title->layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer) continue;
        if (!valid_hierarchy_parent(layer))
            append(layer, 0);
    }

    for (auto it = title->layers.rbegin(); it != title->layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer || emitted.find(layer->id) != emitted.end())
            continue;
        if (layer_has_collapsed_group_ancestor(title, *layer))
            continue;
        append(layer, 0);
    }
    return rows;
}

static int group_descendant_object_count(const std::shared_ptr<Title> &title,
                                         const std::string &group_id)
{
    if (!title || group_id.empty()) return 0;
    int count = 0;
    for (const auto &layer : title->layers) {
        if (!layer || layer_type_can_have_children(layer->type))
            continue;
        std::string parent_id = layer->parent_id;
        std::set<std::string> visited;
        while (!parent_id.empty() && visited.insert(parent_id).second) {
            if (parent_id == group_id) {
                ++count;
                break;
            }
            auto parent = title->find_layer(parent_id);
            if (!parent || !layer_type_can_have_children(parent->type))
                break;
            parent_id = parent->parent_id;
        }
    }
    return count;
}

/* Layer list and timeline must use one expansion predicate.  Group child
 * visibility is a third caret state, but an open group also exposes its own
 * animated-property rows in both panels. */
static bool layer_keyframe_sections_expanded(const Layer &layer)
{
    if (layer.type == LayerType::Audio) return false;
    return layer.properties_expanded ||
        (layer.type == LayerType::Group && !layer.group_collapsed);
}

static bool timeline_owner_keyframe_sections_expanded(
    const Title &title, const std::string &owner_id)
{
    if (owner_id == kTitleCameraSwitchOwner)
        return title.camera_switches_expanded;
    if (const TitleCamera *camera = timeline_camera_from_owner(title, owner_id))
        return camera->timeline_expanded;
    if (const TitleLight *light = timeline_light_from_owner(title, owner_id))
        return light->timeline_expanded;
    if (auto layer = title.find_layer(owner_id))
        return layer_keyframe_sections_expanded(*layer);
    return false;
}

struct TimelineRow {
    std::shared_ptr<Layer> layer;
    TimelinePropertyRef prop;
    bool is_property = false;
    std::string owner_id;
    QString owner_label;
    double in_time = 0.0;
    double out_time = 0.0;
    bool locked = false;
    bool is_camera = false;
    bool is_camera_switch = false;
    /* Multi-channel properties remain one authored track, but expanded X/Y/Z
     * views are real rows in the shared flattened model.  Both the layer list
     * and timeline consume these exact rows. */
    bool is_property_channel = false;
    int property_channel = -1;
    bool is_effect_group = false;
    int effect_index = -1;
};

static QString timeline_property_channel_label(
    const TimelinePropertyRef &property, int component)
{
    component = std::max(0, component);
    const QString property_name = QString::fromStdString(property.name()).toLower();
    const bool color_group = property.is_scalar_group() &&
        (property_name.contains(QStringLiteral("color")) ||
         property_name.contains(QStringLiteral("colour")));
    if (color_group) {
        static const char *argb_names[] = {"A", "R", "G", "B"};
        return QString::fromLatin1(argb_names[std::clamp(component, 0, 3)]);
    }
    if (property_name == QStringLiteral("video_source_time"))
        return component == 0 ? QStringLiteral("Source Time") : QStringLiteral("Speed");
    static const char *axis_names[] = {"X", "Y", "Z", "W"};
    return QString::fromLatin1(axis_names[std::clamp(component, 0, 3)]);
}

static std::string timeline_property_channel_disclosure_key(
    const std::string &owner_id, const std::string &property_name)
{
    return owner_id + std::string("\x1f") + property_name;
}

static bool timeline_property_channels_expanded(
    const Title &title, const std::string &owner_id,
    const TimelinePropertyRef &property)
{
    return property && property.graph_channel_count() > 1 &&
        title.expanded_property_channels.count(
            timeline_property_channel_disclosure_key(owner_id,
                                                      property.name())) != 0;
}

static void append_timeline_property_rows(
    std::vector<TimelineRow> &rows, const Title &title,
    const std::shared_ptr<Layer> &owner, const TimelinePropertyRef &property,
    const std::string &owner_id, const QString &label, double in_time,
    double out_time, bool locked, bool is_camera, bool is_camera_switch,
    int effect_index = -1)
{
    rows.push_back({owner, property, true, owner_id, label, in_time, out_time,
                    locked, is_camera, is_camera_switch, false, -1, false,
                    effect_index});
    if (!timeline_property_channels_expanded(title, owner_id, property))
        return;

    const int channels = std::min(4, property.graph_channel_count());
    for (int channel = 0; channel < channels; ++channel) {
        rows.push_back({owner, property.graph_channel(channel), true, owner_id,
                        QStringLiteral("%1 %2")
                            .arg(label, timeline_property_channel_label(property,
                                                                        channel)),
                        in_time, out_time, locked, is_camera, is_camera_switch,
                        true, channel, false, effect_index});
    }
}

static std::vector<TimelineRow> timeline_rows(const std::shared_ptr<Title> &title)
{
    std::vector<TimelineRow> rows;
    if (!title) return rows;

    // AE-style disclosure: owner rows are always visible, while the actual
    // keyframe channels appear only after their disclosure caret is opened.
    const bool show_camera_switches = title->active_camera.is_animated();

    /* Camera Switches is an authored track, not a permanent camera header.
     * Keep it absent until the active-camera property owns a keyframe. */
    if (show_camera_switches) {
        TimelinePropertyRef camera_switch;
        camera_switch.discrete = &title->active_camera;
        const QString switch_label = QStringLiteral("Camera Switches");
        auto switch_proxy = timeline_owner_proxy(*title, kTitleCameraSwitchOwner,
                                                 switch_label);
        rows.push_back({switch_proxy, {}, false, kTitleCameraSwitchOwner,
                        switch_label, 0.0, title->duration,
                        false, false, true});
        if (title->camera_switches_expanded && camera_switch.is_animated()) {
            append_timeline_property_rows(
                rows, *title, switch_proxy, camera_switch,
                kTitleCameraSwitchOwner, property_label(camera_switch.name()),
                0.0, title->duration, false, false, true);
        }
    }

    for (auto &camera : title->cameras) {
        if (!camera.asset_space_owner_id.empty())
            continue;
        const bool default_camera = camera.id == "default";
        if (default_camera && !title_camera_has_authored_keyframes(camera))
            continue;
        const std::string owner_id = camera_timeline_owner_id(camera.id);
        const QString camera_label = QStringLiteral("Camera: %1")
            .arg(QString::fromStdString(camera.name));
        auto camera_proxy = timeline_owner_proxy(*title, owner_id, camera_label);
        rows.push_back({camera_proxy, {}, false, owner_id,
                        camera_label, 0.0, title->duration, false, true, false});
        if (!camera.timeline_expanded)
            continue;
        for (auto property : timeline_camera_properties(camera)) {
            if (!property.is_animated())
                continue;
            append_timeline_property_rows(
                rows, *title, camera_proxy, property, owner_id,
                property_label(property.name()), 0.0, title->duration,
                false, true, false);
        }
    }

    for (auto &light : title->lights) {
        const std::vector<TimelinePropertyRef> light_properties =
            timeline_light_properties(light);
        const std::string owner_id = light_timeline_owner_id(light.id);
        const QString light_label = QStringLiteral("Light: %1")
            .arg(QString::fromStdString(light.name));
        auto light_proxy = timeline_owner_proxy(*title, owner_id, light_label);
        /* Title-owned 3D rows share the camera-row plumbing so the Layer List
         * and Timeline remain backed by one flattened model. The owner prefix
         * distinguishes light UI, disclosure and property resolution. */
        rows.push_back({light_proxy, {}, false, owner_id,
                        light_label, 0.0, title->duration, false, true, false});
        if (!light.timeline_expanded)
            continue;
        for (auto property : light_properties) {
            if (!property.is_animated())
                continue;
            append_timeline_property_rows(
                rows, *title, light_proxy, property, owner_id,
                property_label(property.name()), 0.0, title->duration,
                false, true, false);
        }
    }

    for (const auto &hierarchy_row : visible_layer_hierarchy_rows(title)) {
        auto layer = hierarchy_row.layer;
        if (!layer) continue;
        rows.push_back({layer, TimelinePropertyRef{}, false, layer->id,
                        QString::fromStdString(layer->name), layer->in_time,
                        layer->out_time, layer->locked, false, false});
        const bool show_properties = layer_keyframe_sections_expanded(*layer);
        if (!show_properties)
            continue;
        std::set<std::string> seen;
        std::map<int, std::vector<TimelinePropertyRef>> effect_properties;
        for (auto prop : timeline_properties(*layer)) {
            const QString encoded = QString::fromStdString(prop.name());
            if (encoded.startsWith(QStringLiteral("__effect__"))) {
                const QStringList parts = encoded.split(QStringLiteral("__"));
                bool valid_index = false;
                const int effect_index = parts.size() >= 3
                    ? parts.at(2).toInt(&valid_index) : -1;
                /* FX strips are a keyframe navigation surface, not a mirror of
                 * the full Effects panel.  Emit an effect strip only when that
                 * effect owns at least one authored keyframe, and expose only
                 * the keyframed properties/channels below it. */
                if (prop.is_animated() && valid_index && effect_index >= 0 &&
                    effect_index < static_cast<int>(layer->effects.size()))
                    effect_properties[effect_index].push_back(prop);
                continue;
            }
            if (!prop.is_animated()) continue;
            const QString label = property_label(prop.name());
            if (seen.insert(label.toStdString()).second)
                append_timeline_property_rows(
                    rows, *title, layer, prop, layer->id, label,
                    layer->in_time, layer->out_time, layer->locked,
                    false, false);
        }

        /* The stack is the hierarchy authority. Effect parent rows are emitted
         * in exact stack order, and their keyframeable parameters follow
         * immediately below them in both the Layer List and Timeline. */
        for (int effect_index = 0;
             effect_index < static_cast<int>(layer->effects.size());
             ++effect_index) {
            auto found = effect_properties.find(effect_index);
            if (found == effect_properties.end() || found->second.empty())
                continue;
            const LayerEffect &effect = layer->effects[static_cast<size_t>(effect_index)];
            QString effect_label = effect_type_name(effect.type);
            if (!effect.extension_id.empty()) {
                auto &catalog = FxmEffectExtensionCatalog::instance();
                if (catalog.effects().empty()) catalog.reload();
                if (const auto *definition = catalog.find(
                        QString::fromStdString(effect.extension_id)))
                    effect_label = definition->displayName;
            }
            rows.push_back({layer, {}, false, layer->id, effect_label,
                            layer->in_time, layer->out_time, layer->locked,
                            false, false, false, -1, true, effect_index});
            std::set<std::string> effect_seen;
            for (const TimelinePropertyRef &prop : found->second) {
                QString label = property_label(prop.name());
                const QString redundant_prefix = effect_label + QLatin1Char(' ');
                if (label.startsWith(redundant_prefix, Qt::CaseInsensitive))
                    label = label.mid(redundant_prefix.size());
                if (!effect_seen.insert(label.toStdString()).second)
                    continue;
                append_timeline_property_rows(
                    rows, *title, layer, prop, layer->id, label,
                    layer->in_time, layer->out_time, layer->locked,
                    false, false, effect_index);
            }
        }
    }
    return rows;
}

/* ══════════════════════════════════════════════════════════════════
 *  TitleEditor
 * ══════════════════════════════════════════════════════════════════ */
