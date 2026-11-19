
constexpr const char *kEditorLayoutSettingsGroup = "TitleEditorLayout";
constexpr const char *kEditorGeometryKey = "geometry";
constexpr const char *kEditorWindowStateKey = "windowState";
constexpr const char *kEditorDockWidthsKey = "dockWidths";
constexpr const char *kEditorDockHeightsKey = "dockHeights";
constexpr const char *kEditorLayoutVersionKey = "layoutVersion";
constexpr int kEditorLayoutVersion = 9;
constexpr const char *kDefaultEditorWindowStateBase64 =
    "AAAA/wAAAAD9AAAAAwAAAAAAAAE3AAADCfwCAAAAAfwAAAAVAAADCQAAAo8A///+/AEAAAAC+wAAACYARgBsAHUAeABNAG8AdABpAG8AbgBUAG8AbwBsAHMARABvAGMAawEAAAAAAAAALwAAAC4AAAAy/AAAADMAAAEEAAABBAD////8AgAAAAP8AAAAFQAAALoAAAC6AQAAGPoAAAABAQAAAAL7AAAAPgBGAGwAdQB4AE0AbwB0AGkAbwBuAEcAcgBhAHAAaABpAGMAUAByAG8AcABlAHIAdABpAGUAcwBEAG8AYwBrAQAAAAD/////AAAA8AD////7AAAALgBGAGwAdQB4AE0AbwB0AGkAbwBuAFAAcgBlAHIAZQBuAGQAZQByAEQAbwBjAGsBAAAAMAAAALMAAACvAP////wAAADTAAABqAAAAV8BAAAY+gAAAAABAAAAAvsAAAA4AEYAbAB1AHgATQBvAHQAaQBvAG4ARQBmAGYAZQBjAHQAcwBQAHIAZQBzAGUAdABzAEQAbwBjAGsBAAAAAP////8AAACWAP////sAAAAoAEYAbAB1AHgATQBvAHQAaQBvAG4AUwB0AHkAbABlAHMARABvAGMAawEAAAAA/////wAAAQQA////+wAAACoARgBsAHUAeABNAG8AdABpAG8AbgBFAGYAZgBlAGMAdABzAEQAbwBjAGsBAAACfwAAAJ8AAABuAP///wAAAAEAAAHXAAADCfwCAAAAAvsAAAAqAEYAbAB1AHgATQBvAHQAaQBvAG4AUAByAG8AagBlAGMAdABEAG8AYwBrAQAAABUAAADLAAAAjgD////8AAAA5AAAAjoAAABGAQAAGPoAAAAAAQAAAAL7AAAAOgBGAGwAdQB4AE0AbwB0AGkAbwBuAEwAYQB5AGUAcgBQAHIAbwBwAGUAcgB0AGkAZQBzAEQAbwBjAGsBAAAI/AAAAQQAAAC0AP////sAAAAqAEYAbAB1AHgATQBvAHQAaQBvAG4AMwBEAFMAYwBlAG4AZQBEAG8AYwBrAQAAAAD/////AAABBAD///8AAAADAAAKAAAAAiH8AQAAAAL7AAAALABGAGwAdQB4AE0AbwB0AGkAbwBuAFQAaQBtAGUAbABpAG4AZQBEAG8AYwBrAQAAAAAAAAmcAAAE8AD////7AAAAMgBGAGwAdQB4AE0AbwB0AGkAbwBuAEUAZABpAHQAbwByAEEAdQBkAGkAbwBEAG8AYwBrAQAACaAAAABgAAAATgAAAGAAAAbqAAADCQAAAAQAAAAEAAAACAAAAAj8AAAAAA==";
constexpr const char *kEditorPanelsLockedKey = "panelsLocked";
constexpr const char *kEditorCanvasTransparencyKey = "canvasTransparency";
constexpr const char *kEditorSafeGuidesVisibleKey = "safeGuidesVisible";
constexpr const char *kGraphicPropertiesDockObjectName = "FluxMotionGraphicPropertiesDock";
constexpr const char *kLayerPropertiesDockObjectName = "FluxMotionLayerPropertiesDock";
constexpr const char *kThreeDSceneDockObjectName = "FluxMotion3DSceneDock";
constexpr const char *kEffectsDockObjectName = "FluxMotionEffectsDock";
constexpr const char *kEffectsPresetsDockObjectName = "FluxMotionEffectsPresetsDock";
constexpr const char *kStylesDockObjectName = "FluxMotionStylesDock";
constexpr const char *kTimelineDockObjectName = "FluxMotionTimelineDock";
constexpr const char *kPrerenderDockObjectName = "FluxMotionPrerenderDock";
constexpr const char *kEditorAudioDockObjectName = "FluxMotionEditorAudioDock";

static QString fxm_color_dialog_style()
{
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor window_text = pal.color(QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor hover = button.lightness() < 128 ? button.lighter(122) : button.darker(108);
    const QColor disabled = text.lightness() < 128 ? text.lighter(160) : text.darker(160);

    QString css = QStringLiteral(
        "QDialog{background:@window@;color:@windowText@;}"
        "QWidget{color:@windowText@;selection-background-color:@highlight@;selection-color:@highlightedText@;}"
        "QLabel{color:@windowText@;background:transparent;}"
        "QLineEdit,QSpinBox{color:@text@;background:@base@;border:1px solid @mid@;border-radius:2px;padding:2px 4px;selection-background-color:@highlight@;selection-color:@highlightedText@;}"
        "QLineEdit:focus,QSpinBox:focus{border-color:@highlight@;}"
        "QLineEdit:disabled,QSpinBox:disabled{color:@disabled@;background:@window@;}"
        "QPushButton{color:@buttonText@;background:@button@;border:1px solid @mid@;border-radius:3px;padding:3px 8px;}"
        "QPushButton:hover{background:@hover@;border-color:@mid@;}"
        "QPushButton:pressed{background:@highlight@;color:@highlightedText@;border-color:@highlight@;}"
        "QPushButton:disabled{color:@disabled@;background:@window@;}"
        "QDialogButtonBox QPushButton{min-width:64px;}"
        "QSpinBox::up-button,QSpinBox::down-button{background:@button@;border-left:1px solid @mid@;}"
        "QSpinBox::up-button:hover,QSpinBox::down-button:hover{background:@hover@;}");
    css.replace(QStringLiteral("@window@"), window.name(QColor::HexRgb));
    css.replace(QStringLiteral("@windowText@"), window_text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@base@"), base.name(QColor::HexRgb));
    css.replace(QStringLiteral("@text@"), text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@button@"), button.name(QColor::HexRgb));
    css.replace(QStringLiteral("@buttonText@"), button_text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@mid@"), mid.name(QColor::HexRgb));
    css.replace(QStringLiteral("@highlight@"), highlight.name(QColor::HexRgb));
    css.replace(QStringLiteral("@highlightedText@"), highlighted_text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@hover@"), hover.name(QColor::HexRgb));
    css.replace(QStringLiteral("@disabled@"), disabled.name(QColor::HexRgb));
    return css;
}

static void fxm_apply_color_dialog_theme(color_widgets::ColorDialog *dialog)
{
    if (!dialog)
        return;

    const QPalette pal = qApp->palette();
    dialog->setPalette(pal);
    for (QWidget *child : dialog->findChildren<QWidget *>())
        child->setPalette(pal);
    dialog->setStyleSheet(fxm_color_dialog_style());
}

static QColor fxm_pick_color(const QColor &initial, QWidget *parent, const QString &title, bool alpha = true)
{
    color_widgets::ColorDialog picker(parent);
    picker.setWindowTitle(title);
    picker.setAlphaEnabled(alpha);
    picker.setButtonMode(color_widgets::ColorDialog::OkCancel);
    picker.setColor(initial.isValid() ? initial : QColor(Qt::white));
    fxm_apply_color_dialog_theme(&picker);
    return picker.exec() == QDialog::Accepted ? picker.color() : QColor();
}

static QStringList fxm_load_recent_color_hexes()
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Color"));
    return settings.value(QStringLiteral("recentColorHexes")).toStringList();
}

static void fxm_save_recent_color_hexes(const QStringList &hexes)
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Color"));
    settings.setValue(QStringLiteral("recentColorHexes"), hexes);
    settings.sync();
}

static QString fxm_color_swatch_tooltip(const QString &name, const QColor &color, const QString &hex)
{
    const QString safe_name = (name.trimmed().isEmpty() ? hex : name.trimmed()).toHtmlEscaped();
    const QString safe_hex = hex.toHtmlEscaped();
    const QString fill = color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("#00000000");
    const QPalette pal = qApp->palette();
    const QString base = pal.color(QPalette::ToolTipBase).name(QColor::HexRgb);
    const QString text = pal.color(QPalette::ToolTipText).name(QColor::HexRgb);
    const QString border = pal.color(QPalette::Mid).name(QColor::HexRgb);
    const QString subtle = (pal.color(QPalette::ToolTipText).lightness() < 128
                                ? pal.color(QPalette::ToolTipText).lighter(145)
                                : pal.color(QPalette::ToolTipText).darker(145)).name(QColor::HexRgb);
    return QStringLiteral(
        "<table cellspacing='0' cellpadding='0' style='background:%1;color:%2;'>"
        "<tr>"
        "<td style='padding:8px 14px 8px 8px;'>"
        "<div style='width:72px;height:72px;background:%3;border:1px solid %4;'></div>"
        "</td>"
        "<td style='vertical-align:middle;padding:8px 12px 8px 0;color:%2;'>"
        "<div style='font-size:13pt;font-weight:600;white-space:nowrap;'>%5</div>"
        "<div style='font-size:10pt;color:%6;white-space:nowrap;margin-top:4px;'>%7</div>"
        "</td>"
        "</tr>"
        "</table>")
        .arg(base, text, fill, border, safe_name, subtle, safe_hex);
}

class ResponsiveSwatchGrid : public QWidget {
public:
    explicit ResponsiveSwatchGrid(QWidget *parent = nullptr, int swatch_size = 24, int spacing = 6)
        : QWidget(parent),
          swatch_size_(swatch_size),
          spacing_(spacing)
    {
        layout_ = new QGridLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setHorizontalSpacing(spacing_);
        layout_->setVerticalSpacing(spacing_);
        layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }

    void clearSwatches()
    {
        swatches_.clear();
        while (QLayoutItem *item = layout_->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        last_columns_ = 0;
        updateGeometry();
    }

    void addSwatch(QWidget *swatch)
    {
        if (!swatch)
            return;
        swatch->setParent(this);
        swatches_.push_back(swatch);
        reflow();
    }

    QSize sizeHint() const override
    {
        const int columns = columnCount();
        const int rows = swatches_.empty() ? 0 : (int)((swatches_.size() + columns - 1) / columns);
        return QSize(columns * swatch_size_ + std::max(0, columns - 1) * spacing_,
                     rows * swatch_size_ + std::max(0, rows - 1) * spacing_);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(swatch_size_, swatches_.empty() ? 0 : swatch_size_);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (columnCount() != last_columns_)
            reflow();
    }

private:
    int columnCount() const
    {
        int available = width();
        if (available <= 0 && parentWidget())
            available = parentWidget()->width();
        available = std::max(available, swatch_size_);
        return std::max(1, (available + spacing_) / (swatch_size_ + spacing_));
    }

    void reflow()
    {
        const int columns = columnCount();
        last_columns_ = columns;
        for (QWidget *swatch : swatches_)
            layout_->removeWidget(swatch);
        for (int i = 0; i < (int)swatches_.size(); ++i)
            layout_->addWidget(swatches_[(size_t)i], i / columns, i % columns);
        updateGeometry();
    }

    QGridLayout *layout_ = nullptr;
    std::vector<QWidget *> swatches_;
    int swatch_size_ = 24;
    int spacing_ = 6;
    int last_columns_ = 0;
};

static void fxm_prepare_embedded_color_dialog(color_widgets::ColorDialog *dialog, const QColor &initial, bool alpha = true)
{
    if (!dialog)
        return;

    dialog->setWindowFlags(Qt::Widget);
    dialog->setAlphaEnabled(alpha);
    dialog->setButtonMode(color_widgets::ColorDialog::Close);
    dialog->setColor(initial.isValid() ? initial : QColor(Qt::white));
    dialog->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    if (auto *button_box = dialog->findChild<QDialogButtonBox *>())
        button_box->hide();
    fxm_apply_color_dialog_theme(dialog);
}

static QPoint clamp_popup_position_to_screen(const QPoint &desired, const QSize &popup_size, QWidget *anchor)
{
    QScreen *screen = QApplication::screenAt(desired);
    if (!screen && anchor && anchor->window() && anchor->window()->windowHandle())
        screen = anchor->window()->windowHandle()->screen();
    if (!screen)
        screen = QApplication::primaryScreen();
    if (!screen)
        return desired;

    const QRect available = screen->availableGeometry().adjusted(6, 6, -6, -6);
    QPoint pos = desired;
    pos.setX(std::clamp(pos.x(), available.left(), std::max(available.left(), available.right() - popup_size.width() + 1)));
    pos.setY(std::clamp(pos.y(), available.top(), std::max(available.top(), available.bottom() - popup_size.height() + 1)));
    return pos;
}

class StrokeOptionsLabel : public QLabel {
public:
    StrokeOptionsLabel(const QString &text, QDoubleSpinBox *spin, QWidget *parent = nullptr,
                       std::function<void()> clicked = {},
                       std::function<void()> drag_started = {},
                       std::function<void()> drag_finished = {})
        : QLabel(text, parent), spin_(spin), clicked_(std::move(clicked)),
          drag_started_(std::move(drag_started)), drag_finished_(std::move(drag_finished))
    {
        setCursor(Qt::PointingHandCursor);
        if (spin_ && !spin_->property("fxmDefaultValue").isValid())
            spin_->setProperty("fxmDefaultValue", spin_->value());
        setToolTip(fxm_tr("OBSTitles.DragNumericLabelTooltip"));
    }

    ~StrokeOptionsLabel() override
    {
        if (dragging_)
            QApplication::restoreOverrideCursor();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !spin_ || !spin_->isEnabled()) {
            QLabel::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        moved_ = false;
        drag_start_x_ = event->globalPosition().x();
        drag_start_value_ = spin_->value();
        grabMouse(Qt::SizeHorCursor);
        QApplication::setOverrideCursor(Qt::SizeHorCursor);
        if (drag_started_)
            drag_started_();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_ || !spin_) {
            QLabel::mouseMoveEvent(event);
            return;
        }
        const double delta = event->globalPosition().x() - drag_start_x_;
        if (std::abs(delta) >= 2.0)
            moved_ = true;
        double next = drag_start_value_ + delta * spin_->singleStep();
        const QVariant default_value = spin_->property("fxmDefaultValue");
        if (default_value.isValid()) {
            const double target = default_value.toDouble();
            const double threshold = std::max(spin_->singleStep() * 3.0, 0.0001);
            if (std::abs(next - target) <= threshold)
                next = target;
        }
        spin_->setValue(std::clamp(next, spin_->minimum(), spin_->maximum()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!dragging_ || event->button() != Qt::LeftButton) {
            QLabel::mouseReleaseEvent(event);
            return;
        }
        dragging_ = false;
        releaseMouse();
        QApplication::restoreOverrideCursor();
        if (drag_finished_)
            drag_finished_();
        if (!moved_ && clicked_)
            clicked_();
        event->accept();
    }

private:
    QDoubleSpinBox *spin_ = nullptr;
    std::function<void()> clicked_;
    std::function<void()> drag_started_;
    std::function<void()> drag_finished_;
    bool dragging_ = false;
    bool moved_ = false;
    double drag_start_x_ = 0.0;
    double drag_start_value_ = 0.0;
};

class AnchorGridButton : public QPushButton {
public:
    explicit AnchorGridButton(QWidget *parent = nullptr) : QPushButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setProperty("active_index", 4);
    }

    std::function<void(int)> anchor_selected;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = rect().adjusted(1, 1, -1, -1);
        p.setPen(QPen(underMouse() ? QColor(74, 74, 74) : QColor(55, 55, 55), 1));
        p.setBrush(underMouse() ? QColor(48, 48, 48) : QColor(36, 36, 36));
        p.drawRoundedRect(outer, 2, 2);

        const int active = property("active_index").toInt();
        constexpr int cell = 7;
        constexpr int gap = 3;
        const int grid = cell * 3 + gap * 2;
        const int origin_x = (width() - grid) / 2;
        const int origin_y = (height() - grid) / 2;
        for (int i = 0; i < 9; ++i) {
            QRectF cell_rect(origin_x + (i % 3) * (cell + gap),
                             origin_y + (i / 3) * (cell + gap),
                             cell, cell);
            p.setPen(QPen(i == active ? QColor(111, 143, 196) : QColor(160, 160, 160), 1.2));
            p.setBrush(i == active ? QColor(75, 110, 168) : Qt::NoBrush);
            p.drawRect(cell_rect);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QPushButton::mouseReleaseEvent(event);
            return;
        }

        constexpr int cell = 7;
        constexpr int gap = 3;
        const int grid = cell * 3 + gap * 2;
        const int origin_x = (width() - grid) / 2;
        const int origin_y = (height() - grid) / 2;
        const QPoint pos = event->pos();
        for (int i = 0; i < 9; ++i) {
            QRect cell_rect(origin_x + (i % 3) * (cell + gap),
                            origin_y + (i / 3) * (cell + gap),
                            cell + gap, cell + gap);
            if (cell_rect.contains(pos)) {
                setProperty("active_index", i);
                update();
                if (anchor_selected)
                    anchor_selected(i);
                break;
            }
        }
        event->accept();
    }
};

static QString gradient_editor_hex(const QColor &color)
{
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
}

static bool gradient_editor_parse_hex(QString text, QColor &color)
{
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
}

static QColor gradient_editor_color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t gradient_editor_argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

static int normalized_gradient_type(int gradient_type)
{
    switch (std::clamp(gradient_type, 0, 4)) {
    case 1: return 1;
    case 2: return 2;
    case 4: return 1;
    case 0:
    case 3:
    default: return 0;
    }
}

class GradientEditorPreview : public QWidget {
public:
    explicit GradientEditorPreview(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(440, 128);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void set_gradient(int type, uint32_t start_color, uint32_t end_color,
                      double start_pos, double end_pos, double start_opacity, double end_opacity,
                      double opacity, double angle, double center_x, double center_y, double scale)
    {
        type_ = normalized_gradient_type(type);
        colors_.assign({gradient_editor_color_from_argb(start_color), gradient_editor_color_from_argb(end_color)});
        for (auto &color : colors_) color.setAlpha(255);
        positions_.assign({std::clamp(start_pos, 0.0, 1.0), std::clamp(end_pos, 0.0, 1.0)});
        stop_opacities_.assign({std::clamp(start_opacity, 0.0, 1.0), std::clamp(end_opacity, 0.0, 1.0)});
        if (selected_stop_ >= stop_count()) selected_stop_ = -1;
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        angle_ = angle;
        center_x_ = center_x;
        center_y_ = center_y;
        scale_ = std::clamp(scale, 0.01, 100.0);
        update();
    }

    void set_extra_stops(const std::vector<GradientStop> &stops)
    {
        while (colors_.size() > 2) {
            colors_.pop_back();
            positions_.pop_back();
            stop_opacities_.pop_back();
        }
        for (const auto &stop : stops) {
            QColor c = gradient_editor_color_from_argb(stop.color);
            c.setAlpha(255);
            colors_.push_back(c);
            positions_.push_back(std::clamp((double)stop.position, 0.0, 1.0));
            stop_opacities_.push_back(std::clamp((double)stop.opacity, 0.0, 1.0));
        }
        if (selected_stop_ >= stop_count()) selected_stop_ = -1;
        update();
    }

    std::vector<GradientStop> extra_stops() const
    {
        std::vector<GradientStop> stops;
        for (int i = 2; i < stop_count(); ++i) {
            GradientStop stop;
            stop.color = gradient_editor_argb_from_color(colors_[i]);
            stop.position = (float)std::clamp(positions_[i], 0.0, 1.0);
            stop.opacity = (float)std::clamp(stop_opacities_[i], 0.0, 1.0);
            stops.push_back(stop);
        }
        std::sort(stops.begin(), stops.end(), [](const GradientStop &a, const GradientStop &b) {
            return a.position < b.position;
        });
        return stops;
    }

    int gradient_type() const { return type_; }
    int selected_stop() const { return selected_stop_; }
    int stop_count() const { return (int)std::min({colors_.size(), positions_.size(), stop_opacities_.size()}); }
    uint32_t stop_color_argb(int index) const { return gradient_editor_argb_from_color(colors_[clamp_stop_index(index)]); }
    QColor stop_color(int index) const
    {
        const int i = clamp_stop_index(index);
        QColor color = colors_[i];
        color.setAlphaF(std::clamp((double)color.alphaF() * stop_opacities_[i] * opacity_, 0.0, 1.0));
        return color;
    }
    double stop_position(int index) const { return positions_[clamp_stop_index(index)]; }
    double stop_opacity(int index) const { return stop_opacities_[clamp_stop_index(index)]; }
    double gradient_opacity() const { return opacity_; }
    double angle() const { return angle_; }
    double center_x() const { return center_x_; }
    double center_y() const { return center_y_; }
    double scale() const { return scale_; }

    void set_gradient_type(int type) { type_ = normalized_gradient_type(type); changed(); }
    void set_angle(double value) { angle_ = value; changed(); }
    void set_center_x(double value) { center_x_ = value; changed(); }
    void set_center_y(double value) { center_y_ = value; changed(); }
    void set_scale(double value) { scale_ = std::clamp(value, 0.01, 100.0); changed(); }
    void set_stop_position(int index, double value) { positions_[clamp_stop_index(index)] = std::clamp(value, 0.0, 1.0); changed(); }
    void set_stop_opacity(int index, double value) { stop_opacities_[clamp_stop_index(index)] = std::clamp(value, 0.0, 1.0); changed(); }
    void set_stop_color(int index, const QColor &color)
    {
        if (!color.isValid()) return;
        const int i = clamp_stop_index(index);
        colors_[i] = color;
        colors_[i].setAlpha(255);
        stop_opacities_[i] = color.alphaF();
        changed();
    }
    int add_stop(double position, QColor color = QColor())
    {
        const double p = std::clamp(position, 0.0, 1.0);
        if (!color.isValid()) color = interpolated_color(p);
        color.setAlpha(255);
        colors_.push_back(color);
        positions_.push_back(p);
        stop_opacities_.push_back(std::clamp((double)color.alphaF(), 0.0, 1.0));
        selected_stop_ = stop_count() - 1;
        changed();
        if (selection_changed) selection_changed(selected_stop_);
        return selected_stop_;
    }
    void remove_stop(int index)
    {
        const int i = clamp_stop_index(index);
        if (i < 0 || stop_count() <= 2) return;
        colors_.erase(colors_.begin() + i);
        positions_.erase(positions_.begin() + i);
        stop_opacities_.erase(stop_opacities_.begin() + i);
        selected_stop_ = stop_count() > 0 ? std::min(selected_stop_, stop_count() - 1) : -1;
        changed();
        if (selection_changed) selection_changed(selected_stop_);
    }
    void set_selected_stop(int index)
    {
        selected_stop_ = (index >= 0 && index < stop_count()) ? index : -1;
        update();
        if (selection_changed) selection_changed(selected_stop_);
    }

    QPoint stop_global_anchor(int index) const
    {
        const QPointF p = stop_point(clamp_stop_index(index));
        return mapToGlobal(QPoint((int)std::round(p.x()), (int)std::round(p.y() + 14.0)));
    }

    std::function<void()> gradient_changed;
    std::function<void(int)> selection_changed;
    std::function<void(int, const QPoint &)> color_popup_requested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(43, 43, 43));

        const QRectF ramp = gradient_line_rect();
        draw_checkerboard(p, ramp.toAlignedRect());
        p.fillRect(ramp, preview_brush(ramp));
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(ramp);

        // Photoshop-style midpoint diamonds between adjacent color stops.
        auto sorted = sorted_stop_indices();
        p.setPen(QPen(QColor(145, 145, 145), 1));
        for (int i = 0; i + 1 < (int)sorted.size(); ++i) {
            double mid = (positions_[sorted[i]] + positions_[sorted[i + 1]]) * 0.5;
            QPointF m(ramp.left() + mid * ramp.width(), ramp.bottom() + 8.0);
            QPolygonF diamond;
            diamond << QPointF(m.x(), m.y() - 5) << QPointF(m.x() + 5, m.y())
                    << QPointF(m.x(), m.y() + 5) << QPointF(m.x() - 5, m.y());
            p.setBrush(QColor(75, 75, 75));
            p.drawPolygon(diamond);
        }

        for (int i = 0; i < stop_count(); ++i) {
            const QPointF pos(ramp.left() + positions_[i] * ramp.width(), 0.0);

            // Opacity stop handle above the ramp.
            QPolygonF opacity_handle;
            opacity_handle << QPointF(pos.x() - 6, ramp.top() - 18)
                           << QPointF(pos.x() + 6, ramp.top() - 18)
                           << QPointF(pos.x() + 6, ramp.top() - 7)
                           << QPointF(pos.x(), ramp.top() - 2)
                           << QPointF(pos.x() - 6, ramp.top() - 7);
            p.setPen(QPen(i == selected_stop_ ? QColor(120, 170, 230) : QColor(18, 18, 18), i == selected_stop_ ? 2 : 1));
            p.setBrush(QColor(210, 210, 210, (int)std::round(stop_opacities_[i] * 255.0)));
            p.drawPolygon(opacity_handle);

            // Color stop handle below the ramp.
            QPolygonF color_handle;
            color_handle << QPointF(pos.x(), ramp.bottom() + 2)
                         << QPointF(pos.x() + 7, ramp.bottom() + 8)
                         << QPointF(pos.x() + 7, ramp.bottom() + 23)
                         << QPointF(pos.x() - 7, ramp.bottom() + 23)
                         << QPointF(pos.x() - 7, ramp.bottom() + 8);
            p.setPen(QPen(i == selected_stop_ ? QColor(120, 170, 230) : QColor(18, 18, 18), i == selected_stop_ ? 2 : 1));
            p.setBrush(QColor(36, 36, 36));
            p.drawPolygon(color_handle);
            QRectF swatch(pos.x() - 5, ramp.bottom() + 10, 10, 10);
            draw_checkerboard(p, swatch.toAlignedRect());
            p.fillRect(swatch, stop_color(i));
            p.setPen(QColor(18, 18, 18));
            p.drawRect(swatch.adjusted(0, 0, -1, -1));
        }

        p.setPen(QColor(210, 210, 210));
        p.drawText(QRectF(ramp.left(), 6, ramp.width(), 20),
                   fxm_tr("OBSTitles.GradientStopsHint")
                       .arg(stop_count()));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        int hit = hit_stop(event->pos());
        if (hit >= 0) {
            set_selected_stop(hit);
            dragging_stop_ = true;
            dragging_stop_index_ = hit;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging_stop_ && (event->buttons() & Qt::LeftButton)) {
            const int drag_index = clamp_stop_index(dragging_stop_index_);
            if (drag_index < 0 || drag_index >= stop_count()) {
                dragging_stop_ = false;
                dragging_stop_index_ = -1;
                QWidget::mouseMoveEvent(event);
                return;
            }
            selected_stop_ = drag_index;
            const QRectF line = gradient_line_rect();
            positions_[drag_index] = std::clamp((double)(event->position().x() - line.left()) / std::max(1.0, (double)line.width()), 0.0, 1.0);
            changed();
            event->accept();
            return;
        }
        setCursor(hit_stop(event->pos()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        dragging_stop_ = false;
        dragging_stop_index_ = -1;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        int hit = hit_stop(event->pos());
        if (hit >= 0) {
            set_selected_stop(hit);
            if (stop_count() > 2) {
                remove_stop(hit);
            }
            event->accept();
            return;
        }
        if (gradient_line_rect().adjusted(-6, -12, 6, 12).contains(event->position())) {
            const QRectF line = gradient_line_rect();
            const double pos = std::clamp((double)(event->position().x() - line.left()) / std::max(1.0, (double)line.width()), 0.0, 1.0);
            add_stop(pos);
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }
