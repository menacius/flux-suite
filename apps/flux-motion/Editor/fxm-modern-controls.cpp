#include "fxm-modern-controls.h"
#include "title-assets.h"
#include "title-localization.h"

#include <QAbstractScrollArea>
#include <QTextEdit>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QComboBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

NumericDragLabel::NumericDragLabel(
    const QString &text, QWidget *field, QWidget *parent,
    std::function<void()> drag_started,
    std::function<void()> drag_finished)
    : QLabel(text, parent), spin_box_(find_spin_box(field)),
      drag_started_(std::move(drag_started)),
      drag_finished_(std::move(drag_finished))
{
    if (!spin_box_)
        return;
    if (!spin_box_->property("fxmDefaultValue").isValid())
        spin_box_->setProperty("fxmDefaultValue", spin_value());
    setToolTip(fxm_tr("OBSTitles.DragNumericLabelTooltip"));
    setCursor(Qt::SizeHorCursor);
}

NumericDragLabel::~NumericDragLabel()
{
    if (dragging_)
        QApplication::restoreOverrideCursor();
}

void NumericDragLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !can_drag()) {
        QLabel::mousePressEvent(event);
        return;
    }

    dragging_ = true;
    drag_start_x_ = event->globalPosition().x();
    drag_start_value_ = spin_value();
    grabMouse(Qt::SizeHorCursor);
    QApplication::setOverrideCursor(Qt::SizeHorCursor);
    if (drag_started_)
        drag_started_();
    event->accept();
}

void NumericDragLabel::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_) {
        QLabel::mouseMoveEvent(event);
        return;
    }
    if (!can_drag()) {
        finish_drag();
        event->accept();
        return;
    }

    const double delta = event->globalPosition().x() - drag_start_x_;
    double next = drag_start_value_ + delta * spin_step();
    const QVariant default_value = spin_box_->property("fxmDefaultValue");
    if (default_value.isValid()) {
        const double target = default_value.toDouble();
        const double threshold = std::max(spin_step() * 3.0, 0.0001);
        if (std::abs(next - target) <= threshold)
            next = target;
    }
    set_spin_value(next);
    event->accept();
}

void NumericDragLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (!dragging_ || event->button() != Qt::LeftButton) {
        QLabel::mouseReleaseEvent(event);
        return;
    }

    finish_drag();
    event->accept();
}

void NumericDragLabel::leaveEvent(QEvent *event)
{
    if (!dragging_)
        QLabel::leaveEvent(event);
}

QAbstractSpinBox *NumericDragLabel::find_spin_box(QWidget *field)
{
    if (!field)
        return nullptr;
    if (auto *spin = qobject_cast<QAbstractSpinBox *>(field))
        return spin;
    return field->findChild<QAbstractSpinBox *>();
}

bool NumericDragLabel::can_drag() const
{
    return spin_box_ && spin_box_->isEnabled() && spin_box_->isVisible() &&
           isEnabled();
}

double NumericDragLabel::spin_value() const
{
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_box_))
        return spin->value();
    if (auto *spin = qobject_cast<QSpinBox *>(spin_box_))
        return spin->value();
    return 0.0;
}

double NumericDragLabel::spin_step() const
{
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_box_))
        return spin->singleStep();
    if (auto *spin = qobject_cast<QSpinBox *>(spin_box_))
        return spin->singleStep();
    return 1.0;
}

void NumericDragLabel::set_spin_value(double value)
{
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_box_)) {
        spin->setValue(std::clamp(value, spin->minimum(), spin->maximum()));
    } else if (auto *spin = qobject_cast<QSpinBox *>(spin_box_)) {
        spin->setValue(
            std::clamp(static_cast<int>(std::round(value)), spin->minimum(),
                       spin->maximum()));
    }
}

void NumericDragLabel::finish_drag()
{
    dragging_ = false;
    releaseMouse();
    QApplication::restoreOverrideCursor();
    if (drag_finished_)
        drag_finished_();
}

namespace {
constexpr int kSwitchTrackWidth = 28;
constexpr int kSwitchTrackHeight = 14;
constexpr int kSwitchKnobDiameter = 10;
constexpr int kSwitchSpacing = 7;
constexpr int kPanelHeaderHeight = 28;
constexpr int kPanelHandleWidth = 23;
constexpr int kPanelCaretWidth = 24;
/* Match the 3D Camera form's compact content inset. */
constexpr int kPanelContentLeft = 5;
constexpr int kPanelContentTop = 4;
constexpr int kPanelContentRight = 5;
constexpr int kPanelContentBottom = 5;
constexpr char kPanelMimeType[] = "application/x-fxm-collapsible-panel";
constexpr double kPi = 3.14159265358979323846;
constexpr int kKeyframeButtonExtent = 18;
constexpr int kKeyframeIconExtent = 12;
constexpr int kKeyframeCaretWidth = 10;
constexpr int kKeyframeCaretHeight = 14;

QPointF mouse_position(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

QPointF drag_position(QDragMoveEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->pos();
#endif
}

QPointF drop_position(QDropEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->pos();
#endif
}

QColor blended(const QColor &a, const QColor &b, int a_weight = 1, int b_weight = 1)
{
    const int total = std::max(1, a_weight + b_weight);
    return QColor((a.red() * a_weight + b.red() * b_weight) / total,
                  (a.green() * a_weight + b.green() * b_weight) / total,
                  (a.blue() * a_weight + b.blue() * b_weight) / total,
                  (a.alpha() * a_weight + b.alpha() * b_weight) / total);
}

FxmCollapsiblePanel *panel_from_drag(const QMimeData *mime, const QObject *source)
{
    if (!mime || !source || !mime->hasFormat(kPanelMimeType))
        return nullptr;
    bool mime_ok = false;
    bool source_ok = false;
    const quintptr mime_address = mime->data(kPanelMimeType).toULongLong(&mime_ok, 16);
    const quintptr source_address = source->property("fxmPanelAddress").toULongLong(&source_ok);
    if (!mime_ok || !source_ok || mime_address != source_address)
        return nullptr;
    return reinterpret_cast<FxmCollapsiblePanel *>(mime_address);
}

QString inferred_panel_title(QWidget *section, const QString &requested)
{
    if (!requested.isEmpty())
        return requested;
    if (section) {
        const QString property_title = section->property("fxmPanelTitle").toString();
        if (!property_title.isEmpty())
            return property_title;
        if (auto *group = qobject_cast<QGroupBox *>(section); group && !group->title().isEmpty())
            return group->title();
    }
    return QObject::tr("Panel");
}

void draw_caret(QPainter &painter, const QRectF &rect, int state, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const QPointF center = rect.center();
    QPolygonF triangle;
    if (state > 0) {
        triangle << QPointF(center.x() - 4.0, center.y() - 2.0)
                 << QPointF(center.x() + 4.0, center.y() - 2.0)
                 << QPointF(center.x(), center.y() + 3.0);
    } else {
        triangle << QPointF(center.x() - 2.0, center.y() - 4.0)
                 << QPointF(center.x() - 2.0, center.y() + 4.0)
                 << QPointF(center.x() + 3.0, center.y());
    }
    painter.drawPolygon(triangle);
    if (state == 1) {
        QColor marker = color;
        marker.setAlpha(std::min(255, color.alpha() + 35));
        painter.setBrush(marker);
        painter.drawEllipse(QPointF(center.x() + 6.0, center.y() + 5.0), 1.45, 1.45);
    }
    painter.restore();
}

/* Qt styles do not consistently preserve the native combo arrow once the
 * inspector supplies a custom drop-down subcontrol. Keep a tiny painted child
 * above the styled field so every host theme has an explicit, palette-aware
 * caret without replacing the application's QStyle. */
class FxmComboCaretOverlay final : public QWidget {
public:
    explicit FxmComboCaretOverlay(QComboBox *combo)
        : QWidget(combo), combo_(combo)
    {
        setObjectName(QStringLiteral("fxmComboCaretOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        combo_->installEventFilter(this);
        syncGeometry();
        show();
        raise();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == combo_ && event) {
            switch (event->type()) {
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::StyleChange:
            case QEvent::PaletteChange:
            case QEvent::EnabledChange:
                syncGeometry();
                update();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        if (!combo_)
            return;
        const QPalette::ColorGroup group = combo_->isEnabled()
            ? QPalette::Active : QPalette::Disabled;
        QColor color = combo_->palette().color(group, QPalette::Text);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const QPointF center = rect().center();
        QPolygonF caret;
        caret << QPointF(center.x() - 3.5, center.y() - 2.0)
              << QPointF(center.x() + 3.5, center.y() - 2.0)
              << QPointF(center.x(), center.y() + 2.5);
        painter.drawPolygon(caret);
    }

private:
    void syncGeometry()
    {
        if (!combo_)
            return;
        constexpr int kCaretAreaWidth = 16;
        setGeometry(std::max(0, combo_->width() - kCaretAreaWidth), 0,
                    kCaretAreaWidth, combo_->height());
        raise();
    }

    QComboBox *combo_ = nullptr;
};

void ensure_combo_caret(QComboBox *combo)
{
    if (!combo || combo->findChild<QWidget *>(
                      QStringLiteral("fxmComboCaretOverlay"),
                      Qt::FindDirectChildrenOnly))
        return;
    new FxmComboCaretOverlay(combo);
}

QString normalized_panel_key(QString value)
{
    value = value.trimmed();
    for (int index = 0; index < value.size(); ++index) {
        const QChar ch = value.at(index);
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_') && ch != QLatin1Char('-'))
            value[index] = QLatin1Char('_');
    }
    while (value.contains(QStringLiteral("__")))
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    return value.isEmpty() ? QStringLiteral("Panel") : value;
}

QString panel_group_for(QWidget *parent)
{
    QStringList path;
    for (QWidget *widget = parent; widget && path.size() < 5;
         widget = widget->parentWidget()) {
        QString segment = widget->objectName();
        if (segment.isEmpty()) {
            segment = QString::fromLatin1(widget->metaObject()->className());
            int sibling_index = 0;
            if (QWidget *container = widget->parentWidget()) {
                const auto siblings = container->findChildren<QWidget *>(
                    QString(), Qt::FindDirectChildrenOnly);
                for (QWidget *sibling : siblings) {
                    if (sibling == widget)
                        break;
                    if (QString::fromLatin1(sibling->metaObject()->className()) == segment)
                        ++sibling_index;
                }
            }
            segment += QStringLiteral("_%1").arg(sibling_index);
        }
        path.prepend(normalized_panel_key(segment));
        if (!widget->objectName().isEmpty())
            break;
    }
    return path.isEmpty() ? QStringLiteral("EditorInspector")
                          : path.join(QStringLiteral("_"));
}

QStringList persisted_panel_order(const QString &group)
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("EditorPanels"));
    return settings.value(QStringLiteral("order/%1").arg(group)).toStringList();
}

void save_panel_order(QBoxLayout *layout, const QString &group)
{
    if (!layout || group.isEmpty())
        return;
    QStringList order;
    for (int index = 0; index < layout->count(); ++index) {
        auto *panel = qobject_cast<FxmCollapsiblePanel *>(layout->itemAt(index)->widget());
        if (panel && panel->property("fxmPersistOrder").toBool() &&
            !panel->persistenceKey().isEmpty())
            order.push_back(panel->persistenceKey());
    }
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("EditorPanels"));
    settings.setValue(QStringLiteral("order/%1").arg(group), order);
}

void restore_panel_order(QBoxLayout *layout, const QString &group)
{
    if (!layout || group.isEmpty())
        return;
    const QStringList saved = persisted_panel_order(group);
    if (saved.isEmpty())
        return;
    int destination = 0;
    for (const QString &key : saved) {
        for (int index = destination; index < layout->count(); ++index) {
            auto *panel = qobject_cast<FxmCollapsiblePanel *>(layout->itemAt(index)->widget());
            if (!panel || !panel->property("fxmPersistOrder").toBool() ||
                panel->persistenceKey() != key)
                continue;
            if (index != destination) {
                const int stretch = layout->stretch(index);
                QLayoutItem *item = layout->takeAt(index);
                const Qt::Alignment alignment = item ? item->alignment() : Qt::Alignment();
                layout->insertWidget(destination, panel, stretch, alignment);
                delete item;
            }
            ++destination;
            break;
        }
    }
}
} // namespace

namespace {

class FxmKeyframeControls final : public QWidget {
public:
    FxmKeyframeControls(QAbstractButton *diamond, QWidget *parent,
                        FxmKeyframeTimesProvider times_provider,
                        FxmCurrentTimeProvider current_time_provider,
                        FxmKeyframeNavigateCallback navigate_callback)
        : QWidget(parent), diamond_(diamond),
          times_provider_(std::move(times_provider)),
          current_time_provider_(std::move(current_time_provider)),
          navigate_callback_(std::move(navigate_callback))
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        previous_ = make_navigation_button(false);
        next_ = make_navigation_button(true);
        layout->addWidget(previous_);
        if (diamond_) {
            diamond_->setParent(this);
            fxm_style_keyframe_button(diamond_);
            layout->addWidget(diamond_);
            diamond_->installEventFilter(this);
            connect(diamond_, &QAbstractButton::clicked, this,
                    [this]() { QTimer::singleShot(0, this, [this]() { refresh(); }); });
            diamond_->setProperty("fxmKeyframeControls",
                                  QVariant::fromValue<qulonglong>(
                                      reinterpret_cast<quintptr>(this)));
        }
        layout->addWidget(next_);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (diamond_ && diamond_->isHidden())
            hide();
        refresh();
    }

    void refresh()
    {
        std::vector<double> times = times_provider_ ? times_provider_()
                                                    : std::vector<double>{};
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
            return std::abs(a - b) <= 1.0e-7;
        }), times.end());
        const double current = current_time_provider_ ? current_time_provider_() : 0.0;
        constexpr double epsilon = 1.0e-7;
        previous_time_.reset();
        next_time_.reset();
        for (double time : times) {
            if (time < current - epsilon)
                previous_time_ = time;
            else if (time > current + epsilon && !next_time_)
                next_time_ = time;
        }
        const bool diamond_enabled = diamond_ && diamond_->isEnabled();
        previous_->setEnabled(diamond_enabled && previous_time_.has_value());
        next_->setEnabled(diamond_enabled && next_time_.has_value());
        previous_->setAccessibleDescription(times.empty()
            ? tr("No keyframes exist for this property")
            : tr("Moves to the previous keyframe for this property"));
        next_->setAccessibleDescription(times.empty()
            ? tr("No keyframes exist for this property")
            : tr("Moves to the next keyframe for this property"));
    }

private:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == diamond_ && event &&
            (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
             event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent ||
             event->type() == QEvent::EnabledChange)) {
            QTimer::singleShot(0, this, [this]() {
                if (!diamond_)
                    return;
                setVisible(!diamond_->isHidden());
                refresh();
            });
        }
        return QWidget::eventFilter(watched, event);
    }

    QToolButton *make_navigation_button(bool next)
    {
        auto *button = new QToolButton(this);
        button->setObjectName(next
            ? QStringLiteral("FxmNextPropertyKeyframeButton")
            : QStringLiteral("FxmPreviousPropertyKeyframeButton"));
        button->setAutoRaise(true);
        button->setFixedSize(kKeyframeCaretWidth, kKeyframeCaretHeight);
        button->setArrowType(next ? Qt::RightArrow : Qt::LeftArrow);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:none;padding:0;}"
            "QToolButton:hover{background:palette(button);border:none;}"
            "QToolButton:pressed{background:palette(highlight);border:none;}"
            "QToolButton:disabled{background:transparent;border:none;}"));
        button->setToolTip(next ? tr("Next keyframe") : tr("Previous keyframe"));
        button->setAccessibleName(button->toolTip());
        button->setFocusPolicy(Qt::StrongFocus);
        connect(button, &QToolButton::clicked, this, [this, next]() {
            refresh();
            const std::optional<double> target = next ? next_time_ : previous_time_;
            if (target && navigate_callback_)
                navigate_callback_(*target);
        });
        return button;
    }

    QPointer<QAbstractButton> diamond_;
    QToolButton *previous_ = nullptr;
    QToolButton *next_ = nullptr;
    FxmKeyframeTimesProvider times_provider_;
    FxmCurrentTimeProvider current_time_provider_;
    FxmKeyframeNavigateCallback navigate_callback_;
    std::optional<double> previous_time_;
    std::optional<double> next_time_;
};

} // namespace

void fxm_style_keyframe_button(QAbstractButton *button)
{
    if (!button)
        return;
    button->setObjectName(QStringLiteral("FxmKeyframeDiamondButton"));
    button->setFixedSize(kKeyframeButtonExtent, kKeyframeButtonExtent);
    button->setIconSize(QSize(kKeyframeIconExtent, kKeyframeIconExtent));
    button->setText(QString());
    button->setProperty("fxmPreserveButtonMetrics", true);
    if (auto *tool = qobject_cast<QToolButton *>(button))
        tool->setAutoRaise(true);

    const QPalette palette = button->palette();
    const QColor hover = palette.color(QPalette::Button).lightness() < 128
        ? palette.color(QPalette::Button).lighter(125)
        : palette.color(QPalette::Button).darker(108);
    QColor marked = palette.color(QPalette::Highlight);
    marked.setAlpha(44);
    button->setStyleSheet(QStringLiteral(
        "QPushButton,QToolButton{background:transparent;border:none;border-radius:2px;padding:0;}"
        "QPushButton:hover,QToolButton:hover{background:%1;}"
        "QPushButton:pressed,QToolButton:pressed{background:%2;}"
        "QPushButton[outlined=\"true\"],QToolButton[outlined=\"true\"],"
        "QPushButton[active=\"true\"],QToolButton[active=\"true\"]{background:%3;}"
        "QPushButton:disabled,QToolButton:disabled{background:transparent;}")
        .arg(hover.name(QColor::HexRgb),
             palette.color(QPalette::Highlight).name(QColor::HexRgb),
             marked.name(QColor::HexArgb)));
}

QColor fxm_keyframe_color()
{
    return QColor(0xff, 0xd2, 0x3f);
}

QIcon fxm_keyframe_diamond_icon(bool active, bool outlined)
{
    const QColor keyframe_color = fxm_keyframe_color();
    if (active)
        return fxm_icon("keyframe-active.svg", keyframe_color);
    if (outlined)
        return fxm_icon("keyframe-outline.svg", keyframe_color);
    return fxm_icon("keyframe-inactive.svg");
}

QWidget *fxm_make_keyframe_controls(
    QAbstractButton *diamond, QWidget *parent,
    FxmKeyframeTimesProvider times_provider,
    FxmCurrentTimeProvider current_time_provider,
    FxmKeyframeNavigateCallback navigate_callback)
{
    return new FxmKeyframeControls(diamond, parent, std::move(times_provider),
                                   std::move(current_time_provider),
                                   std::move(navigate_callback));
}

void fxm_refresh_keyframe_navigation(QAbstractButton *diamond)
{
    if (!diamond)
        return;
    const quintptr address = static_cast<quintptr>(
        diamond->property("fxmKeyframeControls").toULongLong());
    if (address != 0)
        reinterpret_cast<FxmKeyframeControls *>(address)->refresh();
}

void fxm_apply_editor_field_style(QWidget *widget)
{
    if (!widget || widget->property("fxmSkipEditorFieldMetrics").toBool())
        return;
    if (widget->property("fxmEditorFieldStyled").toBool())
        return;
    if (auto *line = qobject_cast<QLineEdit *>(widget)) {
        if (qobject_cast<QAbstractSpinBox *>(line->parentWidget()))
            return;
        line->setProperty("fxmEditorFieldStyled", true);
        line->setFixedHeight(20);
        line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        line->setStyleSheet(fxm_transform_panel_control_style(line->palette()));
        return;
    }
    if (auto *spin = qobject_cast<QAbstractSpinBox *>(widget)) {
        spin->setProperty("fxmEditorFieldStyled", true);
        spin->setFixedHeight(20);
        spin->setStyleSheet(fxm_transform_panel_control_style(spin->palette()));
        return;
    }
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        combo->setProperty("fxmEditorFieldStyled", true);
        combo->setFixedHeight(20);
        combo->setStyleSheet(fxm_transform_panel_control_style(combo->palette()));
        ensure_combo_caret(combo);
    }
}

FxmSwitch::FxmSwitch(QWidget *parent)
    : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(19);
}

FxmSwitch::FxmSwitch(const QString &text, QWidget *parent)
    : FxmSwitch(parent)
{
    setText(text);
}

QSize FxmSwitch::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int text_width = text().isEmpty() ? 0 : metrics.horizontalAdvance(text()) + kSwitchSpacing;
    return QSize(text_width + kSwitchTrackWidth + 2,
                 std::max(metrics.height(), kSwitchTrackHeight) + 4);
}

QSize FxmSwitch::minimumSizeHint() const
{
    return QSize(kSwitchTrackWidth + 2, kSwitchTrackHeight + 4);
}

bool FxmSwitch::hitButton(const QPoint &position) const
{
    return rect().contains(position);
}

void FxmSwitch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const bool rtl = layoutDirection() == Qt::RightToLeft;
    const QRect track_rect(rtl ? 1 : width() - kSwitchTrackWidth - 1,
                           (height() - kSwitchTrackHeight) / 2,
                           kSwitchTrackWidth, kSwitchTrackHeight);
    QRect text_rect = rect();
    if (rtl)
        text_rect.setLeft(track_rect.right() + kSwitchSpacing);
    else
        text_rect.setRight(track_rect.left() - kSwitchSpacing);

    if (!text().isEmpty()) {
        QColor text_color = pal.color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                      QPalette::WindowText);
        if (isEnabled())
            text_color.setAlpha(225);
        painter.setPen(text_color);
        painter.drawText(text_rect, (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                         text());
    }

    const Qt::CheckState state = checkState();
    const bool partially_checked = state == Qt::PartiallyChecked;
    const bool fully_checked = state == Qt::Checked;

    QColor track;
    QColor border;
    if (fully_checked || partially_checked) {
        track = pal.color(QPalette::Highlight);
        track.setAlpha(partially_checked ? 78 : 118);
        border = pal.color(QPalette::Highlight);
        border.setAlpha(hasFocus() ? 210 : 150);
    } else {
        track = pal.color(QPalette::Button);
        track.setAlpha(88);
        border = pal.color(QPalette::Mid);
        border.setAlpha(hasFocus() ? 210 : 118);
    }
    if (!isEnabled()) {
        track = pal.color(QPalette::Disabled, QPalette::Button);
        track.setAlpha(64);
        border = pal.color(QPalette::Disabled, QPalette::Mid);
        border.setAlpha(82);
    } else if (underMouse()) {
        track.setAlpha(std::min(165, track.alpha() + 18));
        border.setAlpha(std::min(220, border.alpha() + 18));
    }

    painter.setPen(QPen(border, hasFocus() ? 1.25 : 1.0));
    painter.setBrush(track);
    painter.drawRoundedRect(QRectF(track_rect), kSwitchTrackHeight / 2.0, kSwitchTrackHeight / 2.0);

    const int knob_y = track_rect.top() + (kSwitchTrackHeight - kSwitchKnobDiameter) / 2;
    int knob_x = track_rect.left() + 2;
    if (fully_checked)
        knob_x = track_rect.right() - kSwitchKnobDiameter - 1;
    else if (partially_checked)
        knob_x = track_rect.center().x() - kSwitchKnobDiameter / 2;
    if (rtl && !partially_checked)
        knob_x = fully_checked ? track_rect.left() + 2
                               : track_rect.right() - kSwitchKnobDiameter - 1;

    QColor knob = fully_checked || partially_checked
                      ? pal.color(QPalette::HighlightedText)
                      : pal.color(QPalette::ButtonText);
    knob.setAlpha(isEnabled() ? 205 : 105);
    painter.setPen(Qt::NoPen);
    painter.setBrush(knob);
    painter.drawEllipse(QRectF(knob_x, knob_y, kSwitchKnobDiameter, kSwitchKnobDiameter));
}

FxmCaretButton::FxmCaretButton(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(18, 20);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
}

QSize FxmCaretButton::sizeHint() const
{
    return QSize(18, 20);
}

void FxmCaretButton::setCaretState(int state)
{
    state = std::clamp(state, 0, 2);
    if (state_ == state)
        return;
    state_ = state;
    update();
    emit caretStateChanged(state_);
}

void FxmCaretButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (underMouse() && isEnabled()) {
        QColor hover = palette().color(QPalette::Button);
        hover.setAlpha(90);
        painter.fillRect(rect().adjusted(1, 1, -1, -1), hover);
    }
    QColor caret = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                   QPalette::WindowText);
    caret.setAlpha(isEnabled() ? 175 : 85);
    draw_caret(painter, rect(), state_, caret);
}

FxmAngleControl::FxmAngleControl(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(tr("Direction"));
    setToolTip(tr("Drag to set direction; use the numeric field for an exact angle"));
}

void FxmAngleControl::setRange(double minimum, double maximum)
{
    if (minimum > maximum)
        std::swap(minimum, maximum);
    minimum_ = minimum;
    maximum_ = maximum;
    setValue(value_);
}

void FxmAngleControl::setSingleStep(double step)
{
    step_ = std::max(0.01, std::abs(step));
}

QSize FxmAngleControl::sizeHint() const
{
    return QSize(38, 38);
}

QSize FxmAngleControl::minimumSizeHint() const
{
    return QSize(30, 30);
}

void FxmAngleControl::setValue(double value)
{
    const double bounded = std::clamp(value, minimum_, maximum_);
    if (std::abs(value_ - bounded) < 0.000001)
        return;
    value_ = bounded;
    update();
    emit valueChanged(value_);
}

void FxmAngleControl::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const qreal side = std::min(width(), height()) - 5.0;
    const QRectF circle((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const QPointF center = circle.center();
    const qreal radius = circle.width() / 2.0;

    QColor fill = pal.color(QPalette::Button);
    if (underMouse() && isEnabled())
        fill = fill.lightness() < 128 ? fill.lighter(114) : fill.darker(105);
    painter.setBrush(fill);
    painter.setPen(QPen(hasFocus() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid),
                        hasFocus() ? 2.0 : 1.0));
    painter.drawEllipse(circle);

    painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    for (int angle = 0; angle < 360; angle += 45) {
        const double radians = angle * kPi / 180.0;
        const QPointF outer(center.x() + std::cos(radians) * (radius - 3.0),
                            center.y() + std::sin(radians) * (radius - 3.0));
        const QPointF inner(center.x() + std::cos(radians) * (radius - (angle % 90 == 0 ? 7.0 : 5.0)),
                            center.y() + std::sin(radians) * (radius - (angle % 90 == 0 ? 7.0 : 5.0)));
        painter.drawLine(inner, outer);
    }

    const double normalized = std::fmod(std::fmod(value_, 360.0) + 360.0, 360.0);
    const double radians = normalized * kPi / 180.0;
    const QPointF tip(center.x() + std::cos(radians) * (radius - 7.0),
                      center.y() + std::sin(radians) * (radius - 7.0));
    const QColor accent = isEnabled() ? pal.color(QPalette::Highlight)
                                      : pal.color(QPalette::Disabled, QPalette::Text);
    painter.setPen(QPen(accent, 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(center, tip);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(center, 2.6, 2.6);
    painter.drawEllipse(tip, 3.2, 3.2);
}

double FxmAngleControl::nearestEquivalent(double normalized) const
{
    double candidate = normalized;
    while (candidate - value_ > 180.0)
        candidate -= 360.0;
    while (value_ - candidate > 180.0)
        candidate += 360.0;
    while (candidate < minimum_ && candidate + 360.0 <= maximum_)
        candidate += 360.0;
    while (candidate > maximum_ && candidate - 360.0 >= minimum_)
        candidate -= 360.0;
    return std::clamp(candidate, minimum_, maximum_);
}

void FxmAngleControl::setValueFromPosition(const QPointF &position)
{
    const QPointF delta = position - rect().center();
    if (std::abs(delta.x()) < 0.001 && std::abs(delta.y()) < 0.001)
        return;
    double angle = std::atan2(delta.y(), delta.x()) * 180.0 / kPi;
    if (angle < 0.0)
        angle += 360.0;
    setValue(nearestEquivalent(angle));
}

void FxmAngleControl::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        dragging_ = true;
        setFocus(Qt::MouseFocusReason);
        setValueFromPosition(mouse_position(event));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void FxmAngleControl::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        setValueFromPosition(mouse_position(event));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void FxmAngleControl::mouseReleaseEvent(QMouseEvent *event)
{
    if (dragging_ && event->button() == Qt::LeftButton) {
        dragging_ = false;
        setValueFromPosition(mouse_position(event));
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FxmAngleControl::wheelEvent(QWheelEvent *event)
{
    if (!isEnabled()) {
        QWidget::wheelEvent(event);
        return;
    }
    const int direction = event->angleDelta().y() >= 0 ? 1 : -1;
    setValue(value_ + direction * step_);
    event->accept();
}

void FxmAngleControl::keyPressEvent(QKeyEvent *event)
{
    const double multiplier = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) {
        setValue(value_ - step_ * multiplier);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Up) {
        setValue(value_ + step_ * multiplier);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        setValue(0.0);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

class FxmCollapsiblePanel::Header final : public QWidget {
public:
    explicit Header(FxmCollapsiblePanel *owner)
        : QWidget(owner), owner_(owner)
    {
        setFixedHeight(kPanelHeaderHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setAccessibleName(owner_->title_);
        setToolTip(tr("Click to collapse or expand. Drag the handle to reorder."));
        setProperty("fxmPanelAddress",
                    QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(owner_)));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kPanelHandleWidth + 5, 3, kPanelCaretWidth + 3, 3);
        layout->setSpacing(4);
        owner_->header_leading_ = new QHBoxLayout();
        owner_->header_leading_->setContentsMargins(0, 0, 0, 0);
        owner_->header_leading_->setSpacing(3);
        layout->addLayout(owner_->header_leading_);
        layout->addStretch(1);
        owner_->header_actions_ = new QHBoxLayout();
        owner_->header_actions_->setContentsMargins(0, 0, 0, 0);
        owner_->header_actions_->setSpacing(3);
        layout->addLayout(owner_->header_actions_);
    }

    QRect handleRect() const { return QRect(2, 2, kPanelHandleWidth - 3, height() - 4); }
    QRect caretRect() const { return QRect(width() - kPanelCaretWidth, 0, kPanelCaretWidth, height()); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPalette pal = palette();

        QColor header = blended(pal.color(QPalette::Window), pal.color(QPalette::Button), 2, 1);
        if (underMouse())
            header = header.lightness() < 128 ? header.lighter(106) : header.darker(102);
        painter.fillRect(rect(), header);
        QColor accent = pal.color(QPalette::Highlight);
        accent.setAlpha(155);
        painter.fillRect(QRect(0, 0, width(), 2), accent);

        QColor border = pal.color(QPalette::Mid);
        border.setAlpha(155);
        painter.fillRect(QRect(0, height() - 1, width(), 1), border);

        QColor handle_color = pal.color(QPalette::WindowText);
        handle_color.setAlpha(handle_hovered_ || drag_armed_ ? 185 : 92);
        painter.setPen(Qt::NoPen);
        painter.setBrush(handle_color);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 2; ++column)
                painter.drawEllipse(QPointF(8.0 + column * 5.0, 9.0 + row * 5.0), 1.25, 1.25);
        }

        const int action_left = owner_->header_actions_ && owner_->header_actions_->count() > 0
                                    ? owner_->header_actions_->geometry().left() - 6
                                    : width() - kPanelCaretWidth - 4;
        const int title_left = owner_->header_leading_ && owner_->header_leading_->count() > 0
                                   ? owner_->header_leading_->geometry().right() + 6
                                   : kPanelHandleWidth + 4;
        QRect title_rect(title_left, 0, std::max(0, action_left - title_left), height());
        QFont title_font = font();
        title_font.setBold(true);
        painter.setFont(title_font);
        QColor title_color = pal.color(QPalette::WindowText);
        title_color.setAlpha(225);
        painter.setPen(title_color);
        painter.drawText(title_rect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(title_font).elidedText(owner_->title_, Qt::ElideRight,
                                                             title_rect.width()));

        QColor caret_color = pal.color(QPalette::WindowText);
        caret_color.setAlpha(175);
        draw_caret(painter, caretRect(), owner_->expanded_ ? 2 : 0, caret_color);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            press_position_ = mouse_position(event).toPoint();
            drag_armed_ = handleRect().contains(press_position_);
            toggle_armed_ = !drag_armed_;
            if (drag_armed_)
                setCursor(Qt::ClosedHandCursor);
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint position = mouse_position(event).toPoint();
        const bool hovered = handleRect().contains(position);
        if (handle_hovered_ != hovered) {
            handle_hovered_ = hovered;
            if (!drag_armed_)
                setCursor(handle_hovered_ ? Qt::OpenHandCursor : Qt::PointingHandCursor);
            update();
        }
        if (drag_armed_ && (position - press_position_).manhattanLength() >= QApplication::startDragDistance()) {
            drag_armed_ = false;
            setCursor(Qt::OpenHandCursor);
            owner_->beginDrag();
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            const QPoint position = mouse_position(event).toPoint();
            const bool toggle = toggle_armed_ &&
                                (position - press_position_).manhattanLength() < QApplication::startDragDistance();
            drag_armed_ = false;
            toggle_armed_ = false;
            setCursor(handleRect().contains(position) ? Qt::OpenHandCursor : Qt::PointingHandCursor);
            update();
            if (toggle) {
                emit owner_->activated();
                owner_->toggleExpanded();
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        handle_hovered_ = false;
        if (!drag_armed_)
            setCursor(Qt::PointingHandCursor);
        update();
        QWidget::leaveEvent(event);
    }

private:
    FxmCollapsiblePanel *owner_ = nullptr;
    QPoint press_position_;
    bool drag_armed_ = false;
    bool toggle_armed_ = false;
    bool handle_hovered_ = false;
};

FxmCollapsiblePanel::FxmCollapsiblePanel(const QString &title, QWidget *content,
                                         QWidget *parent)
    : QWidget(parent), title_(title), content_(content)
{
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAttribute(Qt::WA_StyledBackground, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    header_ = new Header(this);
    layout->addWidget(header_);

    body_ = new QWidget(this);
    body_->setObjectName(QStringLiteral("FxmCollapsiblePanelBody"));
    auto *body_layout = new QVBoxLayout(body_);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);
    layout->addWidget(body_);

    if (content_) {
        content_->setParent(body_);
        content_->installEventFilter(this);
        content_->setProperty("fxmCollapsiblePanelAddress",
                              QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(this)));
        if (!content_->property("fxmPreservePanelMargins").toBool() &&
            !qobject_cast<QAbstractScrollArea *>(content_.data()) && content_->layout()) {
            content_->layout()->setContentsMargins(kPanelContentLeft, kPanelContentTop,
                                                   kPanelContentRight, kPanelContentBottom);
        }
        if (auto *group = qobject_cast<QGroupBox *>(content_.data())) {
            group->setTitle(QString());
            group->setCheckable(false);
            group->setFlat(true);
            if (group->objectName().isEmpty())
                group->setObjectName(QStringLiteral("FxmPanelContent_%1")
                                         .arg(reinterpret_cast<quintptr>(group), 0, 16));
            const QString selector = QStringLiteral("QGroupBox#%1{border:none;margin:0;padding:0;background:transparent;}"
                                                    "QGroupBox#%1::title{height:0;padding:0;margin:0;}")
                                         .arg(group->objectName());
            group->setStyleSheet(group->styleSheet() + selector);
        }
        body_layout->addWidget(content_);
    }

    drop_indicator_widget_ = new QWidget(this);
    drop_indicator_widget_->setAttribute(Qt::WA_TransparentForMouseEvents);
    drop_indicator_widget_->setAutoFillBackground(true);
    QPalette indicator_palette = drop_indicator_widget_->palette();
    indicator_palette.setColor(QPalette::Window, palette().color(QPalette::Highlight));
    drop_indicator_widget_->setPalette(indicator_palette);
    drop_indicator_widget_->hide();
}

void FxmCollapsiblePanel::setTitle(const QString &title)
{
    if (title_ == title)
        return;
    title_ = title;
    if (header_) {
        header_->setAccessibleName(title_);
        header_->update();
    }
}

void FxmCollapsiblePanel::addHeaderWidget(QWidget *widget)
{
    if (!widget || !header_actions_)
        return;
    widget->setParent(header_);
    fxm_apply_transform_panel_widget_style(widget);
    widget->setMaximumHeight(kPanelHeaderHeight - 6);
    header_actions_->addWidget(widget, 0, Qt::AlignVCenter);
    header_->updateGeometry();
    header_->update();
}

void FxmCollapsiblePanel::addHeaderLeadingWidget(QWidget *widget)
{
    if (!widget || !header_leading_)
        return;
    widget->setParent(header_);
    fxm_apply_transform_panel_widget_style(widget);
    widget->setMaximumHeight(kPanelHeaderHeight - 6);
    header_leading_->addWidget(widget, 0, Qt::AlignVCenter);
    header_->updateGeometry();
    header_->update();
}

void FxmCollapsiblePanel::setOrderPersistenceEnabled(bool enabled)
{
    persist_order_ = enabled;
    setProperty("fxmPersistOrder", persist_order_);
}

void FxmCollapsiblePanel::setPersistenceKey(const QString &group, const QString &key)
{
    persistence_group_ = normalized_panel_key(group);
    persistence_key_ = normalized_panel_key(key);
    setProperty("fxmPersistOrder", persist_order_);
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("EditorPanels"));
    const QString setting = QStringLiteral("expanded/%1/%2")
                                .arg(persistence_group_, persistence_key_);
    if (settings.contains(setting)) {
        expanded_ = settings.value(setting, true).toBool();
        applyExpandedState();
    }
}

void FxmCollapsiblePanel::saveExpandedState() const
{
    if (persistence_group_.isEmpty() || persistence_key_.isEmpty())
        return;
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("EditorPanels"));
    settings.setValue(QStringLiteral("expanded/%1/%2")
                          .arg(persistence_group_, persistence_key_), expanded_);
}

void FxmCollapsiblePanel::setExpanded(bool expanded)
{
    if (expanded_ == expanded)
        return;

    int scroll_value = -1;
    QPointer<QScrollBar> scroll_bar;
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(ancestor)) {
            scroll_bar = area->verticalScrollBar();
            if (scroll_bar)
                scroll_value = scroll_bar->value();
            break;
        }
    }

    expanded_ = expanded;
    applyExpandedState();
    saveExpandedState();
    emit expandedChanged(expanded_);

    if (scroll_bar && scroll_value >= 0) {
        QTimer::singleShot(0, this, [scroll_bar, scroll_value]() {
            if (scroll_bar)
                scroll_bar->setValue(std::min(scroll_value, scroll_bar->maximum()));
        });
    }
}

void FxmCollapsiblePanel::toggleExpanded()
{
    setExpanded(!expanded_);
}

void FxmCollapsiblePanel::applyExpandedState()
{
    if (body_)
        body_->setVisible(expanded_);
    if (expanded_) {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else {
        const int collapsed_height = kPanelHeaderHeight + 2;
        setMinimumHeight(collapsed_height);
        setMaximumHeight(collapsed_height);
    }
    if (header_)
        header_->update();
    updateGeometry();
    update();
}

void FxmCollapsiblePanel::beginDrag()
{
    emit activated();
    QDrag drag(header_);
    auto *mime = new QMimeData();
    mime->setData(kPanelMimeType,
                  QByteArray::number(reinterpret_cast<quintptr>(this), 16));
    drag.setMimeData(mime);
    const QPixmap preview = header_->grab();
    drag.setPixmap(preview);
    drag.setHotSpot(QPoint(10, preview.height() / 2));
    drag.exec(Qt::MoveAction);
}

bool FxmCollapsiblePanel::canAcceptPanel(const FxmCollapsiblePanel *panel) const
{
    return panel && panel != this && panel->parentWidget() == parentWidget() &&
           parentWidget() && parentWidget()->layout() &&
           parentWidget()->layout()->indexOf(const_cast<FxmCollapsiblePanel *>(panel)) >= 0 &&
           parentWidget()->layout()->indexOf(const_cast<FxmCollapsiblePanel *>(this)) >= 0;
}

void FxmCollapsiblePanel::setDropIndicator(int edge)
{
    if (drop_indicator_edge_ == edge)
        return;
    drop_indicator_edge_ = edge;
    if (!drop_indicator_widget_)
        return;
    if (edge == 0) {
        drop_indicator_widget_->hide();
        return;
    }
    const int y = edge < 0 ? 0 : std::max(0, height() - 3);
    drop_indicator_widget_->setGeometry(0, y, width(), 3);
    QPalette indicator_palette = drop_indicator_widget_->palette();
    indicator_palette.setColor(QPalette::Window, palette().color(QPalette::Highlight));
    drop_indicator_widget_->setPalette(indicator_palette);
    drop_indicator_widget_->show();
    drop_indicator_widget_->raise();
}

void FxmCollapsiblePanel::movePanelHere(FxmCollapsiblePanel *source, bool before)
{
    if (!canAcceptPanel(source))
        return;
    auto *box_layout = qobject_cast<QBoxLayout *>(parentWidget()->layout());
    if (!box_layout)
        return;

    const int source_index = box_layout->indexOf(source);
    const int target_index = box_layout->indexOf(this);
    if (source_index < 0 || target_index < 0)
        return;

    int insertion_index = target_index + (before ? 0 : 1);
    const int source_stretch = box_layout->stretch(source_index);
    QLayoutItem *source_item = box_layout->takeAt(source_index);
    if (!source_item)
        return;
    const Qt::Alignment source_alignment = source_item->alignment();
    if (source_index < insertion_index)
        --insertion_index;
    insertion_index = std::clamp(insertion_index, 0, box_layout->count());
    box_layout->insertWidget(insertion_index, source, source_stretch, source_alignment);
    delete source_item;
    if (source->persist_order_ && persist_order_ &&
        !source->persistence_group_.isEmpty() &&
        source->persistence_group_ == persistence_group_)
        save_panel_order(box_layout, persistence_group_);
    emit source->orderChanged();
    emit orderChanged();
}

void FxmCollapsiblePanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();

    QColor body = pal.color(QPalette::Window);
    body = body.lightness() < 128 ? body.lighter(112) : body.darker(104);
    QColor border = pal.color(QPalette::Mid);
    border.setAlpha(165);

    painter.setPen(QPen(border, 1.0));
    painter.setBrush(body);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 2.0, 2.0);

    QColor accent = pal.color(QPalette::Highlight);
    accent.setAlpha(155);
    painter.fillRect(QRect(1, 0, std::max(0, width() - 2), 2), accent);

}

void FxmCollapsiblePanel::dragEnterEvent(QDragEnterEvent *event)
{
    FxmCollapsiblePanel *source = panel_from_drag(event ? event->mimeData() : nullptr, event ? event->source() : nullptr);
    if (event && canAcceptPanel(source)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    if (event)
        event->ignore();
}

void FxmCollapsiblePanel::dragMoveEvent(QDragMoveEvent *event)
{
    FxmCollapsiblePanel *source = panel_from_drag(event ? event->mimeData() : nullptr, event ? event->source() : nullptr);
    if (event && canAcceptPanel(source)) {
        setDropIndicator(drag_position(event).y() < height() / 2.0 ? -1 : 1);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    setDropIndicator(0);
    if (event)
        event->ignore();
}

void FxmCollapsiblePanel::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropIndicator(0);
    if (event)
        event->accept();
}

void FxmCollapsiblePanel::dropEvent(QDropEvent *event)
{
    FxmCollapsiblePanel *source = panel_from_drag(event ? event->mimeData() : nullptr, event ? event->source() : nullptr);
    if (event && canAcceptPanel(source)) {
        const bool before = drop_position(event).y() < height() / 2.0;
        movePanelHere(source, before);
        setDropIndicator(0);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    setDropIndicator(0);
    if (event)
        event->ignore();
}

void FxmCollapsiblePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (drop_indicator_edge_ != 0 && drop_indicator_widget_) {
        const int y = drop_indicator_edge_ < 0 ? 0 : std::max(0, height() - 3);
        drop_indicator_widget_->setGeometry(0, y, width(), 3);
        drop_indicator_widget_->raise();
    }
}

bool FxmCollapsiblePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == content_.data() && event) {
        if ((event->type() == QEvent::Hide || event->type() == QEvent::HideToParent) &&
            content_ && content_->isHidden()) {
            setVisible(false);
        } else if ((event->type() == QEvent::Show || event->type() == QEvent::ShowToParent) &&
                   content_ && !content_->isHidden()) {
            setVisible(true);
        }
    }
    return QWidget::eventFilter(watched, event);
}


QString fxm_transform_panel_control_style(const QPalette &palette)
{
    const QColor control_bg = palette.color(QPalette::Base);
    const QColor control_text = palette.color(QPalette::Text);
    const QColor button_bg = palette.color(QPalette::Button);
    const QColor border = palette.color(QPalette::Mid);
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor hover_bg = button_bg.lightness() < 128 ? button_bg.lighter(122)
                                                        : button_bg.darker(108);
    return QStringLiteral(
        "QDoubleSpinBox,QSpinBox,QComboBox,QLineEdit,QTextEdit{color:%1;background:%2;"
        "border:1px solid %3;border-radius:2px;padding:1px 3px;font-size:10px;"
        "min-height:18px;max-height:20px;selection-background-color:%4;}"
        "QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus,QLineEdit:focus,QTextEdit:focus{border-color:%4;}"
        "QDoubleSpinBox::up-button,QDoubleSpinBox::down-button,"
        "QSpinBox::up-button,QSpinBox::down-button{width:12px;background:%5;border-left:1px solid %3;}"
        "QDoubleSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover,"
        "QSpinBox::up-button:hover,QSpinBox::down-button:hover{background:%6;}"
        "QComboBox::drop-down{width:16px;background:%5;border-left:1px solid %3;}"
        "QComboBox::drop-down:hover{background:%6;}"
        "QComboBox QAbstractItemView{color:%1;background:%2;border:1px solid %3;"
        "selection-background-color:%4;selection-color:%7;outline:0;padding:2px;}"
        "QComboBox QAbstractItemView::item{min-height:22px;padding:4px 8px;background:%2;}"
        "QComboBox QAbstractItemView::item:selected{background:%4;color:%7;}")
        .arg(control_text.name(QColor::HexRgb), control_bg.name(QColor::HexRgb),
             border.name(QColor::HexRgb), highlight.name(QColor::HexRgb),
             button_bg.name(QColor::HexRgb), hover_bg.name(QColor::HexRgb),
             palette.color(QPalette::HighlightedText).name(QColor::HexRgb));
}

QString fxm_transform_panel_button_style(const QPalette &palette)
{
    const QColor button_bg = palette.color(QPalette::Button);
    const QColor button_text = palette.color(QPalette::ButtonText);
    const QColor border = palette.color(QPalette::Mid);
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor highlighted_text = palette.color(QPalette::HighlightedText);
    const QColor hover_bg = button_bg.lightness() < 128 ? button_bg.lighter(122)
                                                        : button_bg.darker(108);
    return QStringLiteral(
        "QPushButton,QToolButton{color:%1;background:%2;border:1px solid %3;"
        "border-radius:2px;font-size:10px;padding:1px 6px;min-height:18px;max-height:20px;}"
        "QPushButton:hover,QToolButton:hover{background:%4;border-color:%3;}"
        "QPushButton:pressed,QToolButton:pressed,QToolButton:checked{background:%5;color:%6;border-color:%5;}"
        "QPushButton:disabled,QToolButton:disabled{color:%7;background:%2;border-color:%3;}")
        .arg(button_text.name(QColor::HexRgb), button_bg.name(QColor::HexRgb),
             border.name(QColor::HexRgb), hover_bg.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb), highlighted_text.name(QColor::HexRgb),
             palette.color(QPalette::Disabled, QPalette::ButtonText).name(QColor::HexRgb));
}

QIcon fxm_panel_defaults_icon(const QPalette &palette)
{
    const QColor color = palette.color(QPalette::ButtonText);
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    QRectF arc_rect(4.0, 4.0, 10.0, 10.0);
    painter.drawArc(arc_rect, 35 * 16, 285 * 16);
    QPainterPath arrow;
    arrow.moveTo(5.2, 4.2);
    arrow.lineTo(4.0, 8.0);
    arrow.lineTo(7.8, 6.9);
    painter.drawPath(arrow);
    painter.end();
    return QIcon(pixmap);
}

void fxm_apply_transform_panel_widget_style(QWidget *root)
{
    if (!root)
        return;

    const QString control_style = fxm_transform_panel_control_style(root->palette());
    const QString button_style = fxm_transform_panel_button_style(root->palette());
    const bool responsive_fields =
        root->property("fxmResponsivePropertyFields").toBool();

    auto normalize_one = [&](QWidget *widget) {
        if (!widget || widget->property("fxmSkipTransformPanelMetrics").toBool())
            return;
        if (qobject_cast<FxmAngleControl *>(widget))
            return;
        if (auto *spin = qobject_cast<QAbstractSpinBox *>(widget)) {
            spin->setFixedHeight(20);
            spin->setMinimumWidth(0);
            spin->setMaximumWidth(QWIDGETSIZE_MAX);
            spin->setSizePolicy(responsive_fields ? QSizePolicy::Ignored
                                                  : QSizePolicy::Expanding,
                                QSizePolicy::Fixed);
            spin->setStyleSheet(control_style);
            return;
        }
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            combo->setFixedHeight(20);
            combo->setMinimumWidth(0);
            combo->setMaximumWidth(QWIDGETSIZE_MAX);
            combo->setSizePolicy(responsive_fields ? QSizePolicy::Ignored
                                                   : QSizePolicy::Expanding,
                                 QSizePolicy::Fixed);
            combo->setStyleSheet(control_style);
            ensure_combo_caret(combo);
            combo->setMaxVisibleItems(12);
            if (combo->view()) {
                combo->view()->setTextElideMode(Qt::ElideRight);
                combo->view()->setMinimumWidth(std::max(0, combo->width()));
                combo->view()->setAutoFillBackground(true);
                combo->view()->viewport()->setAutoFillBackground(true);
                combo->view()->setStyleSheet(QStringLiteral(
                    "QAbstractItemView{color:%1;background:%2;border:1px solid %3;"
                    "selection-background-color:%4;selection-color:%5;outline:0;padding:2px;}"
                    "QAbstractItemView::item{min-height:22px;padding:4px 8px;background:%2;color:%1;}"
                    "QAbstractItemView::item:hover{background:%6;color:%1;}"
                    "QAbstractItemView::item:selected{background:%4;color:%5;}")
                    .arg(root->palette().color(QPalette::Text).name(QColor::HexRgb),
                         root->palette().color(QPalette::Base).name(QColor::HexRgb),
                         root->palette().color(QPalette::Mid).name(QColor::HexRgb),
                         root->palette().color(QPalette::Highlight).name(QColor::HexRgb),
                         root->palette().color(QPalette::HighlightedText).name(QColor::HexRgb),
                         root->palette().color(QPalette::Button).name(QColor::HexRgb)));
            }
            return;
        }
        if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            line->setFixedHeight(20);
            line->setMinimumWidth(0);
            line->setMaximumWidth(QWIDGETSIZE_MAX);
            line->setSizePolicy(responsive_fields ? QSizePolicy::Ignored
                                                  : QSizePolicy::Expanding,
                                QSizePolicy::Fixed);
            line->setStyleSheet(control_style);
            return;
        }
        if (auto *text = qobject_cast<QTextEdit *>(widget)) {
            text->setStyleSheet(control_style);
            return;
        }
        if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
            /* Properties-panel buttons had intentional per-control metrics in
             * Development Version 239 (for example 28x22 binding buttons,
             * 30x24 swatches and 24x22 keyframe toggles). The shared inspector
             * normalizer must not collapse those already-authored buttons to
             * the Transform panel's 20x20 icon metric. Only normalize buttons
             * that still use default metrics and default QSS. */
            const bool has_explicit_button_style =
                !button->styleSheet().trimmed().isEmpty() ||
                button->property("fxmPreserveButtonMetrics").toBool();
            const bool has_fixed_button_height =
                button->minimumHeight() > 0 &&
                button->minimumHeight() == button->maximumHeight() &&
                button->maximumHeight() < QWIDGETSIZE_MAX;
            const bool has_fixed_button_width =
                button->minimumWidth() > 0 &&
                button->minimumWidth() == button->maximumWidth() &&
                button->maximumWidth() < QWIDGETSIZE_MAX;
            if (has_explicit_button_style || has_fixed_button_height ||
                has_fixed_button_width)
                return;

            const bool has_text = !button->text().trimmed().isEmpty();
            const bool compact_square = !has_text || qobject_cast<QToolButton *>(button);
            if (compact_square) {
                button->setFixedSize(20, 20);
                button->setIconSize(QSize(14, 14));
            } else {
                button->setFixedHeight(20);
            }
            const bool preserve_custom_fill = button->property("argb").isValid() ||
                                              button->property("buttonFillKind").isValid();
            if (!preserve_custom_fill)
                button->setStyleSheet(button_style);
            return;
        }
        if (auto *slider = qobject_cast<QSlider *>(widget)) {
            if (slider->orientation() == Qt::Horizontal)
                slider->setFixedHeight(20);
        }
    };

    normalize_one(root);
    const auto widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets)
        normalize_one(widget);

    /* Match the 3D Camera inspector's exact form geometry across every
     * Properties section. */
    const auto forms = root->findChildren<QFormLayout *>();
    for (QFormLayout *form : forms) {
        form->setContentsMargins(5, 4, 5, 5);
        form->setHorizontalSpacing(4);
        form->setVerticalSpacing(2);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
}

QWidget *fxm_make_angle_field(QDoubleSpinBox *spin_box, QWidget *parent,
                              FxmAngleControl **angle_control)
{
    if (!spin_box)
        return nullptr;

    auto *field = new QWidget(parent);
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    auto *dial = new FxmAngleControl(field);
    dial->setRange(spin_box->minimum(), spin_box->maximum());
    dial->setSingleStep(spin_box->singleStep());
    dial->setValue(spin_box->value());
    dial->setFixedSize(36, 36);
    spin_box->setParent(field);
    spin_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    layout->addWidget(dial, 0, Qt::AlignVCenter);
    layout->addWidget(spin_box, 1, Qt::AlignVCenter);

    QObject::connect(spin_box, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     dial, [dial](double value) {
        const QSignalBlocker blocker(dial);
        dial->setValue(value);
    });
    QObject::connect(dial, &FxmAngleControl::valueChanged,
                     spin_box, [spin_box](double value) {
        spin_box->setValue(value);
    });

    if (angle_control)
        *angle_control = dial;
    return field;
}

FxmCollapsiblePanel *fxm_add_panel_section(QVBoxLayout *layout, QWidget *section,
                                           const QString &title,
                                           QWidget *header_widget,
                                           int insert_index)
{
    if (!layout || !section)
        return nullptr;

    if (FxmCollapsiblePanel *existing = fxm_panel_for_content(section))
        return existing;

    const QString panel_title = inferred_panel_title(section, title);
    QWidget *parent = layout->parentWidget();
    const QString persistence_group = section->property("fxmPanelPersistenceGroup").toString().isEmpty()
                                          ? panel_group_for(parent)
                                          : section->property("fxmPanelPersistenceGroup").toString();
    const QString persistence_key = section->property("fxmPanelPersistenceKey").toString().isEmpty()
                                        ? (section->objectName().isEmpty() ? panel_title : section->objectName())
                                        : section->property("fxmPanelPersistenceKey").toString();
    fxm_apply_transform_panel_widget_style(section);
    auto *panel = new FxmCollapsiblePanel(panel_title, section, parent);
    panel->setObjectName(QStringLiteral("FxmCollapsiblePanel_%1")
                             .arg(normalized_panel_key(persistence_key)));
    const QVariant persist_order_property = section->property("fxmPersistPanelOrder");
    panel->setOrderPersistenceEnabled(!persist_order_property.isValid() ||
                                      persist_order_property.toBool());
    QSettings panel_settings(QStringLiteral("FluxMotion"), QStringLiteral("EditorPanels"));
    const QString expanded_setting = QStringLiteral("expanded/%1/%2")
                                         .arg(normalized_panel_key(persistence_group),
                                              normalized_panel_key(persistence_key));
    const bool has_saved_expanded_state = panel_settings.contains(expanded_setting);
    panel->setPersistenceKey(persistence_group, persistence_key);
    const QVariant default_expanded = section->property("fxmPanelDefaultExpanded");
    if (!has_saved_expanded_state && default_expanded.isValid())
        panel->setExpanded(default_expanded.toBool());

    if (!header_widget) {
        QObject *property_widget = section->property("fxmPanelHeaderWidget").value<QObject *>();
        header_widget = qobject_cast<QWidget *>(property_widget);
    }
    if (!header_widget && section->property("fxmPanelDefaultsDisabled").toBool() != true) {
        QString defaults_key = section->property("fxmPanelDefaultsKey").toString();
        if (defaults_key.isEmpty())
            defaults_key = persistence_key;
        auto *defaults_button = new QToolButton(panel);
        defaults_button->setObjectName(QStringLiteral("FxmPanelDefaultsButton"));
        defaults_button->setAutoRaise(true);
        defaults_button->setFixedSize(22, 20);
        defaults_button->setIcon(fxm_panel_defaults_icon(panel->palette()));
        defaults_button->setIconSize(QSize(14, 14));
        defaults_button->setToolTip(QObject::tr("Restore this panel to defaults"));
        defaults_button->setStyleSheet(fxm_transform_panel_button_style(panel->palette()));
        QObject::connect(defaults_button, &QToolButton::clicked, panel, [section, defaults_key]() {
            for (QWidget *candidate = section; candidate; candidate = candidate->parentWidget()) {
                const bool invoked = QMetaObject::invokeMethod(candidate, "reset_panel_defaults",
                                                               Qt::DirectConnection,
                                                               Q_ARG(QString, defaults_key));
                if (invoked)
                    return;
            }
        });
        header_widget = defaults_button;
    }
    if (header_widget)
        panel->addHeaderWidget(header_widget);

    panel->setVisible(!section->isHidden());
    if (insert_index >= 0)
        layout->insertWidget(std::min(insert_index, layout->count()), panel);
    else
        layout->addWidget(panel);
    if (panel->property("fxmPersistOrder").toBool())
        restore_panel_order(layout, normalized_panel_key(persistence_group));
    return panel;
}

FxmCollapsiblePanel *fxm_panel_for_content(QWidget *section)
{
    if (!section)
        return nullptr;
    bool ok = false;
    const quintptr address = section->property("fxmCollapsiblePanelAddress").toULongLong(&ok);
    return ok ? reinterpret_cast<FxmCollapsiblePanel *>(address) : nullptr;
}
