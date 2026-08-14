#include "title-editor-internal.h"
#include "fxm-modern-controls.h"

#include <functional>
#include <map>
#include <unordered_map>

#include <QPainter>
#include <QColorDialog>
#include <QDropEvent>
#include <QFont>
#include <QStyleOptionToolButton>
#include <QStringList>
#include <QWidgetAction>


namespace {

using LayerPtr = std::shared_ptr<Layer>;

constexpr int kLayerListMargin = 4;
constexpr int kLayerListSpacing = 4;
constexpr int kLayerDragHandleWidth = 20;
constexpr int kLayerVisibilityWidth = 20;
constexpr int kLayerAudioMuteWidth = 20;
constexpr int kLayerLockWidth = 20;
constexpr int kLayerExpandWidth = 18;
constexpr int kLayerIndexWidth = 24;
constexpr int kLayerTypeWidth = 22;
constexpr int kLayerFxWidth = 24;
constexpr int kLayerMatteIndicatorWidth = 20;
constexpr int kLayerNameMinimumWidth = 180;
constexpr int kLayerModeWidth = 110;
constexpr int kLayerParentWidth = 150;
constexpr int kLayerMaskWidth = 130;
constexpr int kLayerMatteControlWidth = 20;
constexpr int kLayerDimensionWidth = 54;
constexpr int kLayerRowIconExtent = 18;
constexpr int kLayerAssetIconExtent = 16;
/* Leave a visible vertical gutter inside the 28 px layer row. The matching
 * local QSS min/max-height declarations are required because the OBS
 * application stylesheet can otherwise repolish these controls back to its
 * global field minimum after setFixedHeight(). */
constexpr int kLayerFieldHeight = 16;
constexpr int kLayerGroupDropTargetRole = Qt::UserRole + 8;

enum class LayerListDropPlacement {
    Before = 0,
    After = 1,
    IntoGroup = 2,
};

static QPoint layer_list_drop_position(QDropEvent *event)
{
    if (!event)
        return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/* Fixed columns + minimum usable layer-name area + layout gaps.  Include
 * extra room for the vertical scrollbar so the splitter can never compress
 * the layer list until controls paint over one another. */
constexpr int kLayerStackMinimumWidth = 946;

class LayerListWidget final : public QListWidget {
public:
    using QListWidget::QListWidget;
    using DropHandler = std::function<void(
        const std::vector<std::string> &, const std::string &,
        LayerListDropPlacement)>;

    void set_layer_drop_handler(DropHandler handler)
    {
        drop_handler_ = std::move(handler);
    }

    void start_row_drag(QListWidgetItem *item)
    {
        if (!item || !(item->flags() & Qt::ItemIsDragEnabled))
            return;
        if (!item->isSelected()) {
            clearSelection();
            item->setSelected(true);
        }
        setCurrentItem(item, QItemSelectionModel::NoUpdate);
        setFocus(Qt::MouseFocusReason);
        dragged_layer_ids_.clear();
        for (QListWidgetItem *selected : selectedItems()) {
            if (!selected ||
                selected->data(Qt::UserRole + 1).toString() !=
                    QStringLiteral("layer") ||
                !(selected->flags() & Qt::ItemIsDragEnabled))
                continue;
            dragged_layer_ids_.push_back(
                selected->data(Qt::UserRole).toString().toStdString());
        }
        startDrag(Qt::MoveAction);
        dragged_layer_ids_.clear();
    }

protected:
    void dropEvent(QDropEvent *event) override
    {
        if (!event || dragged_layer_ids_.empty() || !drop_handler_) {
            if (event)
                event->ignore();
            return;
        }

        QListWidgetItem *target = itemAt(layer_list_drop_position(event));
        const bool drop_into_group = target &&
            target->data(Qt::UserRole + 1).toString() ==
                QStringLiteral("layer") &&
            dropIndicatorPosition() == QAbstractItemView::OnItem &&
            target->data(kLayerGroupDropTargetRole).toBool();
        if (!drop_into_group) {
            /* With ItemIsDropEnabled removed from ordinary rows, Qt exposes
             * only AboveItem/BelowItem insertion lines here. Keep the dragged
             * layer in its current hierarchy scope and let rowsMoved feed the
             * existing canonical sibling-order synchronization. */
            QListWidget::dropEvent(event);
            return;
        }

        const std::string target_id =
            target->data(Qt::UserRole).toString().toStdString();
        if (
            std::find(dragged_layer_ids_.begin(), dragged_layer_ids_.end(),
                      target_id) != dragged_layer_ids_.end()) {
            event->ignore();
            return;
        }

        drop_handler_(dragged_layer_ids_, target_id,
                      LayerListDropPlacement::IntoGroup);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

private:
    DropHandler drop_handler_;
    std::vector<std::string> dragged_layer_ids_;
};

static constexpr const char *kLayerRowContrastIconProperty =
    "fxmLayerRowContrastIcon";
static constexpr const char *kLayerRowDimensionToggleProperty =
    "fxmLayerRowDimensionToggle";
static constexpr const char *kLayerRowForegroundLabelProperty =
    "fxmLayerRowForegroundLabel";
static constexpr const char *kLayerRowCompactFieldProperty =
    "fxmLayerRowCompactField";

static void enforce_layer_row_compact_field(QWidget *widget)
{
    if (!widget ||
        !widget->property(kLayerRowCompactFieldProperty).toBool())
        return;
    widget->setSizePolicy(widget->sizePolicy().horizontalPolicy(),
                          QSizePolicy::Fixed);
    widget->setMinimumHeight(kLayerFieldHeight);
    widget->setMaximumHeight(kLayerFieldHeight);
    widget->resize(widget->width(), kLayerFieldHeight);
}

class CompactLayerLineEdit final : public QLineEdit {
public:
    CompactLayerLineEdit(const QString &text, QWidget *parent)
        : QLineEdit(text, parent)
    {
        setProperty(kLayerRowCompactFieldProperty, true);
        enforce_layer_row_compact_field(this);
    }

    QSize sizeHint() const override
    {
        QSize result = QLineEdit::sizeHint();
        result.setHeight(kLayerFieldHeight);
        return result;
    }

    QSize minimumSizeHint() const override
    {
        QSize result = QLineEdit::minimumSizeHint();
        result.setHeight(kLayerFieldHeight);
        return result;
    }

protected:
    bool event(QEvent *event) override
    {
        const bool handled = QLineEdit::event(event);
        if (event && (event->type() == QEvent::Polish ||
                      event->type() == QEvent::StyleChange ||
                      event->type() == QEvent::PaletteChange))
            enforce_layer_row_compact_field(this);
        return handled;
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QLineEdit::resizeEvent(event);
        if (height() != kLayerFieldHeight)
            enforce_layer_row_compact_field(this);
    }
};

class CompactLayerComboBox final : public QComboBox {
public:
    explicit CompactLayerComboBox(QWidget *parent) : QComboBox(parent)
    {
        setProperty(kLayerRowCompactFieldProperty, true);
        enforce_layer_row_compact_field(this);
    }

    QSize sizeHint() const override
    {
        QSize result = QComboBox::sizeHint();
        result.setHeight(kLayerFieldHeight);
        return result;
    }

    QSize minimumSizeHint() const override
    {
        QSize result = QComboBox::minimumSizeHint();
        result.setHeight(kLayerFieldHeight);
        return result;
    }

protected:
    bool event(QEvent *event) override
    {
        const bool handled = QComboBox::event(event);
        if (event && (event->type() == QEvent::Polish ||
                      event->type() == QEvent::StyleChange ||
                      event->type() == QEvent::PaletteChange))
            enforce_layer_row_compact_field(this);
        return handled;
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QComboBox::resizeEvent(event);
        if (height() != kLayerFieldHeight)
            enforce_layer_row_compact_field(this);
    }
};

/* OBS themes can override QToolButton's effective small-icon metric after the
 * row is polished, even when setIconSize() was explicit. Paint the row glyph
 * into an exact logical-pixel rectangle while still asking the active style
 * to draw hover, pressed and checked button backgrounds. */
class LayerRowIconButton final : public QToolButton {
public:
    explicit LayerRowIconButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        const QIcon glyph = option.icon;
        option.icon = QIcon();
        option.text.clear();

        QPainter painter(this);
        style()->drawComplexControl(QStyle::CC_ToolButton, &option,
                                    &painter, this);
        if (glyph.isNull())
            return;

        const int extent = std::min(
            {kLayerRowIconExtent, std::max(0, width() - 2),
             std::max(0, height() - 2)});
        const QRect icon_rect((width() - extent) / 2,
                              (height() - extent) / 2,
                              extent, extent);
        const QIcon::Mode mode = isEnabled() ? QIcon::Normal
                                              : QIcon::Disabled;
        const QIcon::State state = isChecked() ? QIcon::On : QIcon::Off;
        glyph.paint(&painter, icon_rect, Qt::AlignCenter, mode, state);
    }
};

/* A font glyph is not reliable here: several UI families map the Unicode
 * filled/hollow diamond to a very narrow fallback glyph. Paint the keyframe
 * marker geometrically so its silhouette and weight remain stable in the
 * standalone Satoshi UI and in every OBS host theme. */
class KeyframeDiamondButton final : public QToolButton {
public:
    explicit KeyframeDiamondButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
    }

    void setActiveKeyframe(bool active)
    {
        if (active_keyframe_ == active)
            return;
        active_keyframe_ = active;
        update();
    }

    void setIndicatorColor(const QColor &color)
    {
        indicator_color_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.icon = QIcon();
        option.text.clear();

        QPainter painter(this);
        style()->drawComplexControl(QStyle::CC_ToolButton, &option,
                                    &painter, this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QColor color = indicator_color_.isValid()
            ? indicator_color_ : palette().color(QPalette::Highlight);
        if (!isEnabled())
            color.setAlpha(95);
        else if (underMouse())
            color = color.lighter(122);

        const QPointF center(width() * 0.5, height() * 0.5);
        constexpr qreal radius = 5.25;
        QPainterPath diamond;
        diamond.moveTo(center.x(), center.y() - radius);
        diamond.lineTo(center.x() + radius, center.y());
        diamond.lineTo(center.x(), center.y() + radius);
        diamond.lineTo(center.x() - radius, center.y());
        diamond.closeSubpath();

        painter.setPen(QPen(color, active_keyframe_ ? 1.25 : 1.7,
                            Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter.setBrush(active_keyframe_ ? QBrush(color) : Qt::NoBrush);
        painter.drawPath(diamond);
    }

private:
    QColor indicator_color_;
    bool active_keyframe_ = false;
};

static QColor layer_row_widget_foreground(const QWidget *widget,
                                          QPalette::ColorRole role)
{
    if (!widget)
        return fxm_icon_color();
    return widget->palette().color(
        widget->isEnabled() ? QPalette::Active : QPalette::Disabled, role);
}

static void set_layer_row_button_icon(QToolButton *button,
                                      const char *file_name)
{
    if (!button)
        return;
    const QString name = file_name ? QString::fromUtf8(file_name) : QString();
    button->setProperty(kLayerRowContrastIconProperty, name);
    button->setIcon(name.isEmpty()
        ? QIcon()
        : fxm_icon(name.toUtf8().constData(),
                   layer_row_widget_foreground(button, QPalette::ButtonText)));
}

static void refresh_layer_dimension_toggle(
    QToolButton *button, const QColor &background,
    const QPalette &theme_palette)
{
    if (!button)
        return;
    const QColor foreground =
        fxm_background_aware_foreground(background, theme_palette);
    const QColor muted =
        fxm_background_aware_muted_foreground(background, theme_palette);
    const QColor outline = fxm_mix_colors(foreground, background, 0.38);
    const QColor highlight = theme_palette.color(QPalette::Highlight);
    const QColor highlight_foreground =
        fxm_background_aware_foreground(highlight, theme_palette);

    button->setStyleSheet(QStringLiteral(
        "QToolButton{color:%1;background:transparent;border:1px solid %2;border-radius:3px;"
        "font-size:10px;font-weight:700;padding:0;}"
        "QToolButton:hover:enabled{background:transparent;border-color:%3;}"
        "QToolButton[threeD=\"true\"]{color:%4;background:%3;border-color:%3;}"
        "QToolButton:disabled{color:%5;background:transparent;border-color:%2;}")
        .arg(foreground.name(QColor::HexRgb),
             outline.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlight_foreground.name(QColor::HexRgb),
             muted.name(QColor::HexRgb)));
}

static void refresh_layer_row_contrast_widgets(
    QWidget *row_widget, const QColor &background, const QPalette &theme_palette)
{
    if (!row_widget)
        return;

    const QColor foreground =
        fxm_background_aware_foreground(background, theme_palette);
    const QColor muted =
        fxm_background_aware_muted_foreground(background, theme_palette);
    const QColor highlighted_text = fxm_background_aware_foreground(
        theme_palette.color(QPalette::Highlight), theme_palette);

    auto apply_palette = [&](QWidget *widget) {
        if (!widget || widget->isWindow() ||
            qobject_cast<QAbstractItemView *>(widget))
            return;
        QPalette widget_palette = widget->palette();
        for (QPalette::ColorGroup group :
             {QPalette::Active, QPalette::Inactive}) {
            widget_palette.setColor(group, QPalette::WindowText, foreground);
            widget_palette.setColor(group, QPalette::Text, foreground);
            widget_palette.setColor(group, QPalette::ButtonText, foreground);
            widget_palette.setColor(group, QPalette::BrightText, foreground);
            widget_palette.setColor(group, QPalette::HighlightedText,
                                    highlighted_text);
        }
        widget_palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
        widget_palette.setColor(QPalette::Disabled, QPalette::Text, muted);
        widget_palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
        widget_palette.setColor(QPalette::Disabled, QPalette::BrightText, muted);
        widget->setPalette(widget_palette);
        /* Palette changes repolish widgets under the OBS application
         * stylesheet. Reassert compact geometry after that repolish trigger,
         * otherwise the global input min-height expands the row fields. */
        enforce_layer_row_compact_field(widget);
        if (widget->property(kLayerRowForegroundLabelProperty).toBool()) {
            const QColor label_foreground =
                widget->isEnabled() ? foreground : muted;
            widget->setStyleSheet(QStringLiteral(
                "QLabel{color:%1;background:transparent;}")
                .arg(label_foreground.name(QColor::HexRgb)));
        }
    };

    apply_palette(row_widget);
    for (QWidget *child : row_widget->findChildren<QWidget *>())
        apply_palette(child);

    for (QWidget *child : row_widget->findChildren<QWidget *>())
        enforce_layer_row_compact_field(child);

    for (QToolButton *button : row_widget->findChildren<QToolButton *>()) {
        const QString name =
            button->property(kLayerRowContrastIconProperty).toString();
        if (!name.isEmpty())
            button->setIcon(fxm_icon(
                name.toUtf8().constData(),
                layer_row_widget_foreground(button, QPalette::ButtonText)));
    }
    for (QToolButton *button : row_widget->findChildren<QToolButton *>()) {
        if (button->property(kLayerRowDimensionToggleProperty).toBool())
            refresh_layer_dimension_toggle(button, background, theme_palette);
    }
}

class LayerRowWidget final : public QWidget {
public:
    LayerRowWidget(QListWidgetItem *item, const QColor &layer_color,
                   QWidget *parent)
        : QWidget(parent), item_(item), layer_color_(layer_color),
          theme_palette_(parent ? parent->palette()
                                : (qApp ? qApp->palette() : QPalette())),
          base_background_(theme_palette_.color(QPalette::Window))
    {
        setObjectName(QStringLiteral("layerListColorRow"));
        setAutoFillBackground(false);
    }

    QColor effective_background() const
    {
        QColor tint = layer_color_;
        tint.setAlpha(item_ && item_->isSelected() ? 255 : 72);
        return fxm_composite_over(tint, base_background_);
    }

    void refresh_contrast()
    {
        const QColor background = effective_background();
        if (background == last_contrast_background_)
            return;
        last_contrast_background_ = background;
        refresh_layer_row_contrast_widgets(this, background, theme_palette_);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        refresh_contrast();
        QPainter painter(this);
        painter.fillRect(rect(), base_background_);
        QColor background = layer_color_;
        background.setAlpha(item_ && item_->isSelected() ? 255 : 72);
        painter.fillRect(rect(), background);
        if (item_ && item_->isSelected()) {
            QColor outline = fxm_background_aware_foreground(
                layer_color_, theme_palette_);
            outline.setAlpha(210);
            painter.setPen(QPen(outline, 1.0));
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    }

private:
    QListWidgetItem *item_ = nullptr;
    QColor layer_color_;
    QPalette theme_palette_;
    QColor base_background_;
    QColor last_contrast_background_;
};

class LayerColorIcon final : public QLabel {
public:
    using ClickHandler = std::function<void()>;

    explicit LayerColorIcon(QWidget *parent) : QLabel(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    void set_click_handler(ClickHandler handler)
    {
        click_handler_ = std::move(handler);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event && event->button() == Qt::LeftButton &&
            rect().contains(event->pos()) && click_handler_) {
            click_handler_();
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event &&
            (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) && click_handler_) {
            click_handler_();
            event->accept();
            return;
        }
        QLabel::keyPressEvent(event);
    }

private:
    ClickHandler click_handler_;
};

static const std::array<QColor, 16> &layer_ui_color_palette()
{
    static const std::array<QColor, 16> colors = {
        QColor(QStringLiteral("#e03131")),
        QColor(QStringLiteral("#f76707")),
        QColor(QStringLiteral("#f59f00")),
        QColor(QStringLiteral("#94d82d")),
        QColor(QStringLiteral("#37b24d")),
        QColor(QStringLiteral("#0ca678")),
        QColor(QStringLiteral("#1098ad")),
        QColor(QStringLiteral("#1c7ed6")),
        QColor(QStringLiteral("#4263eb")),
        QColor(QStringLiteral("#7048e8")),
        QColor(QStringLiteral("#ae3ec9")),
        QColor(QStringLiteral("#d6336c")),
        QColor(QStringLiteral("#8d6e63")),
        QColor(QStringLiteral("#adb5bd")),
        QColor(QStringLiteral("#495057")),
        QColor(QStringLiteral("#212529")),
    };
    return colors;
}

static QString layer_ui_color_swatch_style(const QColor &color,
                                           bool selected)
{
    const QPalette palette = qApp->palette();
    const QColor border = selected
        ? palette.color(QPalette::Highlight)
        : palette.color(QPalette::Mid);
    const QColor check = color.lightness() < 135 ? Qt::white : Qt::black;
    return QStringLiteral(
        "QToolButton{background:%1;color:%2;border:%3px solid %4;"
        "border-radius:3px;font-weight:bold;padding:0;}"
        "QToolButton:hover{border:2px solid %5;}")
        .arg(color.name(QColor::HexRgb),
             check.name(QColor::HexRgb),
             selected ? QStringLiteral("2") : QStringLiteral("1"),
             border.name(QColor::HexRgb),
             palette.color(QPalette::Highlight).name(QColor::HexRgb));
}

static QIcon layer_ui_color_action_icon(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.fillRect(QRect(1, 1, 14, 14), color);
    painter.setPen(qApp->palette().color(QPalette::Mid));
    painter.drawRect(QRect(0, 0, 15, 15));
    return QIcon(pixmap);
}

static constexpr const char *kThemedIconProperty = "fxmThemedIconName";

static void assign_themed_action_icon(QAction *action, const char *file_name)
{
    if (!action || !file_name)
        return;
    action->setProperty(kThemedIconProperty, QString::fromUtf8(file_name));
    action->setIcon(obs_icon(file_name));
}

static void refresh_themed_menu_icons(QMenu *menu)
{
    if (!menu)
        return;
    for (QAction *action : menu->actions()) {
        if (!action)
            continue;
        const QString file_name = action->property(kThemedIconProperty).toString();
        if (!file_name.isEmpty())
            action->setIcon(obs_icon(file_name.toUtf8().constData()));
        if (QMenu *sub_menu = action->menu())
            refresh_themed_menu_icons(sub_menu);
    }
}

static void make_layer_row_children_transparent(QWidget *row_widget)
{
    if (!row_widget)
        return;

    const QBrush transparent(QColor(0, 0, 0, 0));
    const QPalette::ColorGroup groups[] = {
        QPalette::Active, QPalette::Inactive, QPalette::Disabled};
    const QPalette::ColorRole roles[] = {
        QPalette::Window, QPalette::Base,
        QPalette::AlternateBase, QPalette::Button};

    for (QWidget *child : row_widget->findChildren<QWidget *>()) {
        /* Combo popup views must retain their opaque OBS-theme surface. */
        if (!child || child->isWindow() ||
            qobject_cast<QAbstractItemView *>(child))
            continue;
        child->setAutoFillBackground(false);
        child->setAttribute(Qt::WA_OpaquePaintEvent, false);
        QPalette child_palette = child->palette();
        for (QPalette::ColorGroup group : groups)
            for (QPalette::ColorRole role : roles)
                child_palette.setBrush(group, role, transparent);
        child->setPalette(child_palette);
    }
}

class LayerRowDragHandle final : public QToolButton {
public:
    LayerRowDragHandle(LayerListWidget *list, QListWidgetItem *item,
                       QWidget *parent)
        : QToolButton(parent), list_(list), item_(item)
    {
        setFixedSize(kLayerDragHandleWidth, 20);
        setObjectName(QStringLiteral("layerRowDragHandle"));
        setAccessibleName(fxm_tr("OBSTitles.DragLayerTooltip"));
        setAutoRaise(true);
        setToolTip(fxm_tr("OBSTitles.DragLayerTooltip"));
        setCursor(item && (item->flags() & Qt::ItemIsDragEnabled)
                      ? Qt::OpenHandCursor : Qt::ArrowCursor);
        setEnabled(item && (item->flags() & Qt::ItemIsDragEnabled));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (!event || event->button() != Qt::LeftButton || !isEnabled()) {
            QToolButton::mousePressEvent(event);
            return;
        }
        press_position_ = event->pos();
        pressed_ = true;
        setDown(true);
        if (list_ && item_)
            list_->setCurrentItem(item_);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!event || !pressed_ ||
            !(event->buttons() & Qt::LeftButton)) {
            QToolButton::mouseMoveEvent(event);
            return;
        }
        if ((event->pos() - press_position_).manhattanLength() <
            QApplication::startDragDistance()) {
            event->accept();
            return;
        }
        pressed_ = false;
        setDown(false);
        setCursor(Qt::ClosedHandCursor);
        if (list_)
            list_->start_row_drag(item_);
        setCursor(Qt::OpenHandCursor);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        pressed_ = false;
        setDown(false);
        if (event)
            event->accept();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor dot = palette().color(isEnabled() ? QPalette::Text
                                                  : QPalette::Mid);
        dot.setAlpha(isDown() ? 245 : 175);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot);
        for (int column = 0; column < 2; ++column)
            for (int row = 0; row < 3; ++row)
                painter.drawEllipse(QPointF(7.0 + column * 6.0,
                                            6.0 + row * 4.0),
                                    1.25, 1.25);
    }

private:
    LayerListWidget *list_ = nullptr;
    QListWidgetItem *item_ = nullptr;
    QPoint press_position_;
    bool pressed_ = false;
};

class FxIndicatorButton final : public QToolButton {
public:
    explicit FxIndicatorButton(QWidget *parent = nullptr) : QToolButton(parent) {}

    void set_effect_stack_disabled(bool disabled)
    {
        if (effect_stack_disabled_ == disabled) return;
        effect_stack_disabled_ = disabled;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        if (!effect_stack_disabled_) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor slash = palette().color(QPalette::WindowText);
        slash.setAlpha(220);
        QPen pen(slash, 1.6, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(4.0, height() - 3.0),
                         QPointF(width() - 4.0, 3.0));
    }

private:
    bool effect_stack_disabled_ = false;
};

struct LayerMediaKinds {
    bool visual = false;
    bool audio = false;
};

static LayerMediaKinds layer_media_kinds(const std::shared_ptr<Title> &title,
                                         const Layer &layer,
                                         std::set<std::string> *visiting = nullptr)
{
    if (layer.type == LayerType::Audio) return {false, true};
    if (layer.type == LayerType::Video) return {true, true};
    if (!layer_type_can_have_children(layer.type)) return {true, false};
    std::set<std::string> local;
    if (!visiting) visiting = &local;
    if (!visiting->insert(layer.id).second) return {};
    LayerMediaKinds kinds;
    if (title) {
        for (const auto &child : title->layers) {
            if (!child || child->parent_id != layer.id) continue;
            const auto ck = layer_media_kinds(title, *child, visiting);
            kinds.visual = kinds.visual || ck.visual;
            kinds.audio = kinds.audio || ck.audio;
        }
    }
    visiting->erase(layer.id);
    return kinds;
}


static TimelinePropertyRef layer_timeline_property(Layer &layer,
                                                   const std::string &property_name)
{
    for (TimelinePropertyRef prop : timeline_properties(layer))
        if (prop.name() == property_name) return prop;
    return {};
}

static int keyframe_index_at_time(const TimelinePropertyRef &prop, double local_time)
{
    if (!prop) return -1;
    const double tolerance = std::max(1e-6, obs_frame_duration() * 0.5 + 1e-6);
    for (int index = 0; index < static_cast<int>(prop.keyframe_count()); ++index)
        if (std::abs(prop.keyframe_time(static_cast<size_t>(index)) - local_time) <= tolerance)
            return index;
    return -1;
}

static int graph_mode_for_property_channel(int channel)
{
    return channel == 3 ? 4 : channel;
}

static QString property_channel_summary(const TimelinePropertyRef &prop,
                                        double local_time,
                                        const std::string &property_name)
{
    const int count = prop.graph_channel_count();
    QStringList values;
    values.reserve(count);
    for (int channel = 0; channel < count && channel < 4; ++channel) {
        double value = prop.graph_channel(channel).graph_value(local_time);
        if (property_name == "scale") value *= 100.0;
        values.push_back(QStringLiteral("%1 %2")
            .arg(timeline_property_channel_label(prop, channel))
            .arg(value, 0, 'f', 2));
    }
    return values.join(QStringLiteral("   "));
}

static std::string hierarchy_scope_id(const std::shared_ptr<Title> &title,
                                      const Layer &layer)
{
    if (!title || layer.parent_id.empty())
        return {};
    const auto parent = title->find_layer(layer.parent_id);
    if (!parent)
        return {};
    if (layer_type_is_container(parent->type))
        return parent->id;
    if (parent->type == LayerType::Video && layer.type == LayerType::Audio &&
        layer.linked_media_stream && layer.linked_media_layer_id == parent->id)
        return parent->id;
    return {};
}

static bool has_group_ancestor(const std::shared_ptr<Title> &title,
                               const Layer &layer,
                               const std::string &candidate_group_id)
{
    if (!title || candidate_group_id.empty()) return false;
    std::set<std::string> visited;
    std::string parent_id = layer.parent_id;
    while (!parent_id.empty() && visited.insert(parent_id).second) {
        if (parent_id == candidate_group_id) return true;
        const auto parent = title->find_layer(parent_id);
        if (!parent || !layer_type_can_have_children(parent->type)) break;
        parent_id = parent->parent_id;
    }
    return false;
}

static void reorder_visible_siblings(std::vector<LayerPtr> &siblings,
                                     const std::vector<std::string> &visual_top_to_bottom)
{
    if (siblings.empty() || visual_top_to_bottom.empty()) return;
    std::map<std::string, LayerPtr> by_id;
    for (const auto &layer : siblings)
        if (layer) by_id[layer->id] = layer;

    std::vector<LayerPtr> desired;
    desired.reserve(visual_top_to_bottom.size());
    for (auto it = visual_top_to_bottom.rbegin();
         it != visual_top_to_bottom.rend(); ++it) {
        const auto found = by_id.find(*it);
        if (found != by_id.end()) desired.push_back(found->second);
    }
    if (desired.size() != siblings.size())
        return; // Collapsed/legacy scopes are intentionally left untouched.
    siblings = std::move(desired);
}

static std::vector<LayerPtr> canonical_group_model_order(
    const std::shared_ptr<Title> &title,
    const std::map<std::string, std::vector<std::string>> &visual_orders)
{
    std::vector<LayerPtr> result;
    if (!title) return result;

    std::map<std::string, std::vector<LayerPtr>> children;
    std::vector<LayerPtr> roots;
    for (const auto &layer : title->layers) {
        if (!layer) continue;
        const std::string scope = hierarchy_scope_id(title, *layer);
        if (scope.empty()) roots.push_back(layer);
        else children[scope].push_back(layer);
    }

    auto root_order = visual_orders.find(std::string());
    if (root_order != visual_orders.end())
        reorder_visible_siblings(roots, root_order->second);
    for (auto &[parent_id, siblings] : children) {
        const auto found = visual_orders.find(parent_id);
        if (found != visual_orders.end())
            reorder_visible_siblings(siblings, found->second);
    }

    std::set<std::string> emitted;
    std::function<void(const LayerPtr &)> append_subtree;
    append_subtree = [&](const LayerPtr &layer) {
        if (!layer || !emitted.insert(layer->id).second) return;
        const auto found = children.find(layer->id);
        if (found != children.end()) {
            for (const auto &child : found->second)
                append_subtree(child);
        }
        result.push_back(layer);
    };
    for (const auto &root : roots)
        append_subtree(root);
    for (const auto &layer : title->layers)
        append_subtree(layer);
    return result;
}

static bool move_layer_within_hierarchy(const std::shared_ptr<Title> &title,
                                        const std::string &layer_id,
                                        int visual_direction)
{
    if (!title || layer_id.empty() || visual_direction == 0) return false;
    const auto layer = title->find_layer(layer_id);
    if (!layer || layer->linked_media_stream) return false;

    std::map<std::string, std::vector<std::string>> visual_orders;
    for (const auto &row : visible_layer_hierarchy_rows(title)) {
        if (!row.layer) continue;
        visual_orders[hierarchy_scope_id(title, *row.layer)].push_back(row.layer->id);
    }
    auto &siblings = visual_orders[hierarchy_scope_id(title, *layer)];
    const auto found = std::find(siblings.begin(), siblings.end(), layer_id);
    if (found == siblings.end()) return false;
    const int index = static_cast<int>(std::distance(siblings.begin(), found));
    const int destination = index + (visual_direction > 0 ? -1 : 1);
    if (destination < 0 || destination >= static_cast<int>(siblings.size()))
        return false;
    std::swap(siblings[static_cast<size_t>(index)],
              siblings[static_cast<size_t>(destination)]);

    auto reordered = canonical_group_model_order(title, visual_orders);
    if (reordered.size() != title->layers.size() || reordered == title->layers)
        return false;
    title->layers = std::move(reordered);
    return true;
}

} // namespace

LayerStack::LayerStack(QWidget *parent) : QWidget(parent)
{
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);
    setObjectName(QStringLiteral("layerStack"));
    setStyleSheet(QStringLiteral("QWidget#layerStack{background:%1;color:%2;}")
                      .arg(window.name(QColor::HexRgb),
                           text.name(QColor::HexRgb)));
    setMinimumWidth(kLayerStackMinimumWidth);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);


    QWidget *columns = new QWidget(this);
    columns->setFixedHeight(44);
    columns->setStyleSheet(QStringLiteral("background:%1;border-top:1px solid %2;border-bottom:1px solid %2;")
                                .arg(window.lightness() < 128 ? window.darker(112).name(QColor::HexRgb)
                                                              : window.darker(104).name(QColor::HexRgb),
                                     border.name(QColor::HexRgb)));
    auto *ch = new QHBoxLayout(columns);
    ch->setContentsMargins(kLayerListMargin, 0, kLayerListMargin, 0);
    ch->setSpacing(kLayerListSpacing);
    auto add_header = [&](const QString &txt, int w, Qt::Alignment align = Qt::AlignCenter) {
        QLabel *label = new QLabel(txt, columns);
        label->setFixedWidth(w);
        label->setAlignment(align);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:bold;")
                                 .arg(disabled_text.name(QColor::HexRgb)));
        ch->addWidget(label);
    };
    auto add_header_icon = [&](const char *icon_name, int w, const QString &tip = QString()) {
        QToolButton *icon = new QToolButton(columns);
        icon->setFixedSize(w, 24);
        icon->setIcon(obs_icon(icon_name));
        icon->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
        icon->setAutoRaise(true);
        icon->setEnabled(false);
        icon->setToolTip(tip);
        icon->setStyleSheet(QStringLiteral("QToolButton{background:transparent;border:none;color:%1;}")
                                .arg(disabled_text.name(QColor::HexRgb)));
        ch->addWidget(icon);
    };
    /* Keep switch columns permanent but visually quiet: the layer-list header
     * should only advertise the editable data columns requested by the user.
     * Visibility, audio mute, lock, expand, type, FX and matte role remain
     * fixed-width columns so rows never shift when a Video/Audio/Group row
     * gains or loses an audio switch. */
    add_header("", kLayerDragHandleWidth);
    add_header("", kLayerVisibilityWidth);
    add_header("", kLayerAudioMuteWidth);
    add_header("", kLayerLockWidth);
    add_header("", kLayerExpandWidth);
    add_header("", kLayerIndexWidth);
    add_header("", kLayerTypeWidth);
    add_header("", kLayerFxWidth);
    add_header("", kLayerMatteIndicatorWidth);
    QLabel *name = new QLabel(fxm_tr("OBSTitles.LayerNameHeader"), columns);
    name->setMinimumWidth(kLayerNameMinimumWidth);
    name->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:bold;")
                            .arg(disabled_text.name(QColor::HexRgb)));
    ch->addWidget(name, 1);
    add_header(fxm_tr("OBSTitles.ModeHeader"), kLayerModeWidth, Qt::AlignLeft | Qt::AlignVCenter);
    add_header(fxm_tr("OBSTitles.MatteSourceHeader"), kLayerMaskWidth, Qt::AlignLeft | Qt::AlignVCenter);
    add_header_icon("matte-alpha.svg", kLayerMatteControlWidth, fxm_tr("OBSTitles.MatteAlphaLumaHeaderTooltip"));
    add_header_icon("matte-normal.svg", kLayerMatteControlWidth, fxm_tr("OBSTitles.MatteNormalInvertedHeaderTooltip"));
    add_header(fxm_tr("OBSTitles.ParentHeader"), kLayerParentWidth, Qt::AlignLeft | Qt::AlignVCenter);
    add_header(QStringLiteral("2D/3D"), kLayerDimensionWidth);
    vl->addWidget(columns);

    auto *layer_list = new LayerListWidget(this);
    list_ = layer_list;
    layer_list->set_layer_drop_handler(
        [this](const std::vector<std::string> &layer_ids,
               const std::string &target_id,
               LayerListDropPlacement placement) {
            emit layer_rows_dropped(layer_ids, target_id,
                                    static_cast<int>(placement));
        });
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setDefaultDropAction(Qt::MoveAction);
    list_->setDropIndicatorShown(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setAlternatingRowColors(false);
    list_->setUniformItemSizes(false);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget{background:%1;border:none;color:%2;}"
        "QListWidget::item{border-bottom:1px solid %3;}"
        "QListWidget::item:selected{background:transparent;color:%4;}"
        "QListWidget::item:hover{background:transparent;}")
        .arg(window.name(QColor::HexRgb),
             text.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             pal.color(QPalette::HighlightedText).name(QColor::HexRgb)));
    vl->addWidget(list_, 1);

    auto *toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setOrientation(Qt::Horizontal);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar{background:%1;border-top:1px solid %2;spacing:2px;}"
        "QToolButton{color:%3;background:transparent;border:none;padding:3px;}"
        "QToolButton:hover{background:%4;border-radius:2px;}"
        "QToolButton:disabled{color:%5;}")
        .arg(window.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             disabled_text.name(QColor::HexRgb)));

    auto make_layer_tool = [&](const QString &text, const QIcon &icon, const QString &tip) {
        auto *button = new QToolButton(toolbar);
        button->setText(text);
        button->setAccessibleName(text);
        button->setToolTip(tip);
        button->setIcon(icon);
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::StrongFocus);
        return button;
    };

    btn_add_ = make_layer_tool(fxm_tr("OBSTitles.AddLayer"),
                               obs_icon("add.svg"),
                               fxm_tr("OBSTitles.AddLayerTooltip"));
    auto *add_menu = new QMenu(btn_add_);
    auto add_themed_action = [this](QMenu *menu, const char *icon_name,
                                    const QString &label, auto slot) {
        QAction *action = menu->addAction(obs_icon(icon_name), label, this, slot);
        assign_themed_action_icon(action, icon_name);
        return action;
    };
    add_themed_action(add_menu, "layer-type-text.svg",
                      fxm_tr("OBSTitles.Text"), &LayerStack::on_add_text);
    add_themed_action(add_menu, "layer-type-clock.svg",
                      fxm_tr("OBSTitles.Clock"), &LayerStack::on_add_clock);
    add_themed_action(add_menu, "layer-type-ticker.svg",
                      fxm_tr("OBSTitles.Ticker"), &LayerStack::on_add_ticker);
    add_themed_action(add_menu, "layer-type-chart.svg",
                      fxm_tr("OBSTitles.Chart"), &LayerStack::on_add_chart);
    add_themed_action(add_menu, "layer-type-shape.svg",
                      fxm_tr("OBSTitles.Shape"), &LayerStack::on_add_rect);
    add_themed_action(add_menu, "layer-type-image.svg",
                      fxm_tr("OBSTitles.Image"), &LayerStack::on_add_image);
    add_themed_action(add_menu, "layer-type-video.svg",
                      fxm_tr("OBSTitles.Video"), &LayerStack::on_add_video);
    add_themed_action(add_menu, "layer-type-audio.svg",
                      fxm_tr("OBSTitles.Audio"), &LayerStack::on_add_audio);
    add_themed_action(add_menu, "layer-type-empty.svg",
                      fxm_tr("OBSTitles.Empty"), &LayerStack::on_add_empty);
    add_menu->addSeparator();
    add_themed_action(add_menu, "layer-type-adjustment.svg",
                      fxm_tr("OBSTitles.AdjustmentLayer"),
                      &LayerStack::on_add_adjustment);
    add_themed_action(add_menu, "layer-type-color-solid.svg",
                      fxm_tr("OBSTitles.ColorSolid"),
                      &LayerStack::on_add_color_solid);
    add_menu->addSeparator();
    add_themed_action(add_menu, "layer-type-camera.svg",
                      QStringLiteral("Camera"), &LayerStack::on_add_camera);
    auto *light_menu = add_menu->addMenu(obs_icon("layer-type-light.svg"),
                                          QStringLiteral("Light"));
    assign_themed_action_icon(light_menu->menuAction(), "layer-type-light.svg");
    add_themed_action(light_menu, "layer-type-light.svg",
                      QStringLiteral("Ambient Light"),
                      &LayerStack::on_add_ambient_light);
    add_themed_action(light_menu, "layer-type-light.svg",
                      QStringLiteral("Point Light"),
                      &LayerStack::on_add_point_light);
    add_themed_action(light_menu, "layer-type-light.svg",
                      QStringLiteral("Spot Light"),
                      &LayerStack::on_add_spot_light);
    add_themed_action(light_menu, "layer-type-light.svg",
                      QStringLiteral("Parallel Light"),
                      &LayerStack::on_add_parallel_light);
    add_themed_action(light_menu, "layer-type-light.svg",
                      QStringLiteral("Environment Light"),
                      &LayerStack::on_add_environment_light);
    connect(add_menu, &QMenu::aboutToShow, add_menu,
            [add_menu]() { refresh_themed_menu_icons(add_menu); });
    btn_add_->setMenu(add_menu);
    btn_add_->setPopupMode(QToolButton::InstantPopup);
    btn_add_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;width:0px;}"));

    btn_move_up_ = make_layer_tool(fxm_tr("OBSTitles.MoveLayerUp"),
                                   obs_icon("move-up.svg"),
                                   fxm_tr("OBSTitles.MoveLayerUpTooltip"));
    btn_move_down_ = make_layer_tool(fxm_tr("OBSTitles.MoveLayerDown"),
                                     obs_icon("move-down.svg"),
                                     fxm_tr("OBSTitles.MoveLayerDownTooltip"));
    btn_del_ = make_layer_tool(fxm_tr("OBSTitles.DeleteLayer"),
                               obs_icon("delete.svg"),
                               fxm_tr("OBSTitles.DeleteLayerTooltip"));
    btn_move_up_->setEnabled(false);
    btn_move_down_->setEnabled(false);
    btn_del_->setEnabled(false);

    toolbar->addWidget(btn_add_);
    toolbar->addWidget(btn_move_up_);
    toolbar->addWidget(btn_move_down_);
    toolbar->addSeparator();
    toolbar->addWidget(btn_del_);
    vl->addWidget(toolbar);

    connect(btn_move_up_, &QToolButton::clicked, this, &LayerStack::on_move_up);
    connect(btn_move_down_, &QToolButton::clicked, this, &LayerStack::on_move_down);
    connect(btn_del_, &QToolButton::clicked, this, &LayerStack::on_delete);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &LayerStack::on_selection_changed);
    connect(list_->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() { sync_order_from_list(); });
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested,
            this, &LayerStack::show_layer_context_menu);
    list_->viewport()->installEventFilter(this);
}

void LayerStack::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event)
        return;

    const QEvent::Type type = event->type();
    if (type != QEvent::PaletteChange &&
        type != QEvent::ApplicationPaletteChange &&
        type != QEvent::StyleChange)
        return;

    if (btn_add_) {
        btn_add_->setIcon(obs_icon("add.svg"));
        refresh_themed_menu_icons(btn_add_->menu());
    }
    if (btn_move_up_)
        btn_move_up_->setIcon(obs_icon("move-up.svg"));
    if (btn_move_down_)
        btn_move_down_->setIcon(obs_icon("move-down.svg"));
    if (btn_del_)
        btn_del_->setIcon(obs_icon("delete.svg"));
    if (list_)
        populate();
}

bool LayerStack::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == list_->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->button() == Qt::LeftButton && !list_->itemAt(mouse_event->pos())) {
            QSignalBlocker blocker(list_);
            list_->clearSelection();
            list_->setCurrentItem(nullptr);
            refresh_layer_row_backgrounds();
            emit layer_selected(std::string());
            emit layers_selected({});
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LayerStack::set_title(std::shared_ptr<Title> t)
{
    title_ = t; populate();
}

void LayerStack::refresh() { populate(); }

void LayerStack::set_layer_clipboard_available(bool available)
{
    layer_clipboard_available_ = available;
}

QScrollBar *LayerStack::vertical_scroll_bar() const
{
    return list_ ? list_->verticalScrollBar() : nullptr;
}

void LayerStack::set_playhead(double timeline_time)
{
    playhead_ = timeline_time;
    update_property_rows();
}

void LayerStack::update_property_rows()
{
    if (!title_ || !list_) return;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        if (!item) continue;
        const QString row_kind = item->data(Qt::UserRole + 1).toString();
        if (row_kind != QStringLiteral("property") &&
            row_kind != QStringLiteral("property_channel"))
            continue;
        const std::string layer_id = item->data(Qt::UserRole).toString().toStdString();
        const std::string property_name = item->data(Qt::UserRole + 3).toString().toStdString();
        TimelinePropertyRef prop = timeline_property_for_owner(*title_, layer_id, property_name);
        if (!prop) continue;
        const double owner_in = timeline_owner_in_time(*title_, layer_id);
        const double owner_out = timeline_owner_out_time(*title_, layer_id);
        const double local_time = std::clamp(playhead_ - owner_in, 0.0,
            std::max(0.0, owner_out - owner_in));
        const bool owner_locked = timeline_owner_locked(*title_, layer_id);
        QWidget *row_widget = list_->itemWidget(item);
        if (!row_widget) continue;

        if (row_kind == QStringLiteral("property")) {
            if (auto *button = row_widget->findChild<QToolButton *>(
                    QStringLiteral("keyframeDiamond"))) {
                auto *diamond = static_cast<KeyframeDiamondButton *>(button);
                diamond->setActiveKeyframe(
                    keyframe_index_at_time(prop, local_time) >= 0);
                diamond->setEnabled(!owner_locked);
            }
            if (auto *summary = row_widget->findChild<QLabel *>(QStringLiteral("keyframeValueSummary"))) {
                summary->setText(property_channel_summary(prop, local_time, property_name));
                summary->setEnabled(!owner_locked);
            }
        }

        const int channel = row_kind == QStringLiteral("property_channel")
            ? item->data(Qt::UserRole + 4).toInt() : -1;
        auto *value_x = row_widget->findChild<QDoubleSpinBox *>(QStringLiteral("keyframeValueX"));
        auto *value_y = row_widget->findChild<QDoubleSpinBox *>(QStringLiteral("keyframeValueY"));
        auto *value_z = row_widget->findChild<QDoubleSpinBox *>(QStringLiteral("keyframeValueZ"));
        auto *value_w = row_widget->findChild<QDoubleSpinBox *>(QStringLiteral("keyframeValueW"));
        if (!value_x && !value_y && !value_z && !value_w) continue;

        double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
        if (prop.graph_channel_count() > 1) {
            x = prop.graph_channel(0).graph_value(local_time);
            y = prop.graph_channel(1).graph_value(local_time);
            z = prop.graph_channel_count() > 2
                ? prop.graph_channel(2).graph_value(local_time) : 0.0;
            w = prop.graph_channel_count() > 3
                ? prop.graph_channel(3).graph_value(local_time) : 0.0;
            if (property_name == "scale") { x *= 100.0; y *= 100.0; z *= 100.0; w *= 100.0; }
        } else if (prop.vector) {
            const Vec2Value evaluated = prop.vector->evaluate(local_time);
            x = evaluated.x;
            y = evaluated.y;
            if (property_name == "scale") { x *= 100.0; y *= 100.0; }
        } else {
            x = prop.graph_value(local_time);
            if (property_name == "opacity" || property_name == "char_scale_x" ||
                property_name == "char_scale_y") x *= 100.0;
        }
        auto update_spin = [owner_locked](QDoubleSpinBox *spin, double value) {
            if (!spin) return;
            spin->setEnabled(!owner_locked);
            if (!spin->hasFocus()) {
                QSignalBlocker blocker(spin);
                spin->setValue(value);
            }
        };
        if (channel >= 0) {
            double channel_value = prop.graph_value(local_time);
            if (property_name == "scale") channel_value *= 100.0;
            QDoubleSpinBox *spin = channel == 0 ? value_x
                : channel == 1 ? value_y
                : channel == 2 ? value_z : value_w;
            update_spin(spin, channel_value);
        } else {
            update_spin(value_x, x);
            update_spin(value_y, y);
            update_spin(value_z, z);
            update_spin(value_w, w);
        }
    }
}

void LayerStack::refresh_layer_row_backgrounds()
{
    if (!list_)
        return;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        if (!item ||
            item->data(Qt::UserRole + 1).toString() !=
                QStringLiteral("layer"))
            continue;
        if (QWidget *row_widget = list_->itemWidget(item)) {
            if (auto *color_row = dynamic_cast<LayerRowWidget *>(row_widget))
                color_row->refresh_contrast();
            row_widget->update();
        }
    }
}

void LayerStack::sync_order_from_list()
{
    if (!title_) return;

    std::map<std::string, std::vector<std::string>> visual_orders;
    for (int i = 0; i < list_->count(); ++i) {
        auto *item = list_->item(i);
        if (!item || item->data(Qt::UserRole + 1).toString() != "layer")
            continue;
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        const auto layer = title_->find_layer(id);
        if (!layer) continue;
        visual_orders[hierarchy_scope_id(title_, *layer)].push_back(id);
    }

    auto reordered = canonical_group_model_order(title_, visual_orders);
    if (reordered.size() == title_->layers.size() && reordered != title_->layers) {
        title_->layers = std::move(reordered);
        emit layer_order_changed();
    }
}

void LayerStack::populate()
{
    QString prev_id = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toString()
        : QString();

    list_->blockSignals(true);
    list_->clear();
    if (!title_) { list_->blockSignals(false); return; }

    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor field_text = pal.color(QPalette::Text);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor disabled_field_text =
        pal.color(QPalette::Disabled, QPalette::Text);
    const QColor base = pal.color(QPalette::Base);
    const QColor disabled_base =
        pal.color(QPalette::Disabled, QPalette::Base);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text =
        pal.color(QPalette::HighlightedText);
    const QString button_style = QStringLiteral(
        "QToolButton{color:%1;background-color:rgba(0,0,0,0);border:none;}"
        "QToolButton:hover{background-color:rgba(0,0,0,0);color:%2;}"
        "QToolButton:checked{background-color:rgba(0,0,0,0);color:%2;}")
        .arg(button_text.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb));
    const QString combo_style = QStringLiteral(
        "QComboBox{color:%1;background-color:rgba(0,0,0,0);border:1px solid transparent;border-radius:3px;padding-left:4px;}"
        "QComboBox:hover{background-color:rgba(0,0,0,0);border-color:%3;}"
        "QComboBox::drop-down{background-color:rgba(0,0,0,0);border:none;}"
        "QComboBox QAbstractItemView{background:%2;color:%1;selection-background-color:%3;selection-color:%4;}")
        .arg(field_text.name(QColor::HexRgb),
             base.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             pal.color(QPalette::HighlightedText).name(QColor::HexRgb));
    const QString row_button_style = QStringLiteral(
        "QToolButton{color:palette(button-text);background-color:rgba(0,0,0,0);border:none;}"
        "QToolButton:hover{background-color:rgba(127,127,127,36);color:palette(button-text);}"
        "QToolButton:checked{background-color:rgba(127,127,127,48);color:palette(button-text);}");
    auto make_row_combo_style = [&](const QColor &row_color) {
        return QStringLiteral(
            "QComboBox#layerRowSelector{color:%1;background:%2;border:1px solid %3;"
            "border-radius:3px;padding:0 4px;min-height:%8px;max-height:%8px;}"
            "QComboBox#layerRowSelector:hover,QComboBox#layerRowSelector:focus{border-color:%3;}"
            "QComboBox#layerRowSelector:disabled{color:%6;background:%7;border-color:%3;}"
            "QComboBox#layerRowSelector::drop-down{background:%2;border:0;border-left:1px solid %3;}"
            "QComboBox#layerRowSelector::drop-down:disabled{background:%7;}"
            "QComboBox#layerRowSelector QAbstractItemView{color:%1;background:%2;border:1px solid %3;"
            "selection-background-color:%4;selection-color:%5;}"
            "QComboBox#layerRowSelector QAbstractItemView::item{background:%2;}"
            "QComboBox#layerRowSelector QAbstractItemView::item:selected{background:%4;color:%5;}")
            .arg(field_text.name(QColor::HexRgb),
                 base.name(QColor::HexRgb),
                 row_color.name(QColor::HexRgb),
                 highlight.name(QColor::HexRgb),
                 highlighted_text.name(QColor::HexRgb),
                 disabled_field_text.name(QColor::HexRgb),
                 disabled_base.name(QColor::HexRgb),
                 QString::number(kLayerFieldHeight));
    };
    auto make_row_name_style = [&](const QColor &row_color) {
        return QStringLiteral(
            "QLineEdit#layerNameField{color:%1;background:%2;border:1px solid %3;"
            "border-radius:3px;padding:0 4px;min-height:%8px;max-height:%8px;"
            "selection-background-color:%4;selection-color:%5;}"
            "QLineEdit#layerNameField:hover,QLineEdit#layerNameField:focus{border-color:%3;}"
            "QLineEdit#layerNameField:read-only,QLineEdit#layerNameField:disabled{"
            "color:%6;background:%7;border-color:%3;}")
            .arg(field_text.name(QColor::HexRgb),
                 base.name(QColor::HexRgb),
                 row_color.name(QColor::HexRgb),
                 highlight.name(QColor::HexRgb),
                 highlighted_text.name(QColor::HexRgb),
                 disabled_field_text.name(QColor::HexRgb),
                 disabled_base.name(QColor::HexRgb),
                 QString::number(kLayerFieldHeight));
    };

    auto show_layer_color_menu =
        [this](QWidget *anchor, QListWidgetItem *item,
               const std::string &layer_id, bool has_custom_color,
               const QColor &current_color, const QColor &default_color) {
            if (!anchor || layer_id.empty())
                return;
            if (item)
                list_->setCurrentItem(item);

            /* Keep the popup outside the colored type chip's widget subtree.
             * Otherwise the chip's unqualified background stylesheet can
             * cascade into the menu and tint it with the layer color. */
            auto *menu = new QMenu(list_);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            const QPalette menu_palette = qApp->palette();
            menu->setPalette(menu_palette);
            menu->setStyleSheet(QStringLiteral(
                "QMenu{color:%1;background:%2;border:1px solid %3;}"
                "QMenu::item{padding:5px 22px;}"
                "QMenu::item:selected{background:%4;color:%5;}"
                "QMenu::item:disabled{color:%6;}"
                "QWidget#layerColorPaletteWidget{background:%2;color:%1;}")
                .arg(menu_palette.color(QPalette::Text)
                         .name(QColor::HexRgb),
                     menu_palette.color(QPalette::Base)
                         .name(QColor::HexRgb),
                     menu_palette.color(QPalette::Mid)
                         .name(QColor::HexRgb),
                     menu_palette.color(QPalette::Highlight)
                         .name(QColor::HexRgb),
                     menu_palette.color(QPalette::HighlightedText)
                         .name(QColor::HexRgb),
                     menu_palette.color(QPalette::Disabled, QPalette::Text)
                         .name(QColor::HexRgb)));
            menu->addSection(fxm_tr("OBSTitles.LayerColorPalette"));

            auto *palette_action = new QWidgetAction(menu);
            auto *palette_widget = new QWidget(menu);
            palette_widget->setObjectName(
                QStringLiteral("layerColorPaletteWidget"));
            auto *grid = new QGridLayout(palette_widget);
            grid->setContentsMargins(6, 4, 6, 6);
            grid->setHorizontalSpacing(4);
            grid->setVerticalSpacing(4);

            const auto &colors = layer_ui_color_palette();
            for (int index = 0; index < static_cast<int>(colors.size());
                 ++index) {
                const QColor color = colors[static_cast<size_t>(index)];
                const bool selected = has_custom_color &&
                    current_color.rgb() == color.rgb();
                auto *swatch = new QToolButton(palette_widget);
                swatch->setObjectName(QStringLiteral("layerColorSwatch"));
                swatch->setFixedSize(24, 24);
                swatch->setAutoRaise(false);
                swatch->setText(selected ? QStringLiteral("✓") : QString());
                swatch->setToolTip(color.name(QColor::HexRgb).toUpper());
                swatch->setAccessibleName(
                    fxm_tr("OBSTitles.LayerColorSwatchAccessibleFormat")
                        .arg(color.name(QColor::HexRgb).toUpper()));
                swatch->setStyleSheet(
                    layer_ui_color_swatch_style(color, selected));
                connect(swatch, &QToolButton::clicked, menu,
                        [this, menu, layer_id, color]() {
                            /* Close first: the emitted edit rebuilds the layer
                             * list synchronously and destroys this popup. */
                            menu->close();
                            emit layer_ui_color_changed(
                                layer_id, true,
                                static_cast<uint32_t>(color.rgba()));
                        });
                grid->addWidget(swatch, index / 4, index % 4);
            }
            palette_action->setDefaultWidget(palette_widget);
            menu->addAction(palette_action);
            menu->addSeparator();

            QAction *default_action = menu->addAction(
                layer_ui_color_action_icon(default_color),
                fxm_tr("OBSTitles.DefaultLayerColor"));
            default_action->setCheckable(true);
            default_action->setChecked(!has_custom_color);
            connect(default_action, &QAction::triggered, menu,
                    [this, layer_id]() {
                        emit layer_ui_color_changed(layer_id, false, 0u);
                    });

            QAction *custom_action = menu->addAction(
                layer_ui_color_action_icon(current_color),
                fxm_tr("OBSTitles.CustomLayerColor"));
            connect(custom_action, &QAction::triggered, this,
                    [this, layer_id, current_color]() {
                        QColor selected = QColorDialog::getColor(
                            current_color, this,
                            fxm_tr("OBSTitles.CustomLayerColorDialog"),
                            QColorDialog::DontUseNativeDialog);
                        if (!selected.isValid())
                            return;
                        selected.setAlpha(255);
                        emit layer_ui_color_changed(
                            layer_id, true,
                            static_cast<uint32_t>(selected.rgba()));
                    });

            menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
        };

    std::set<std::string> track_matte_source_ids;
    for (const auto &candidate : title_->layers) {
        if (candidate && candidate->mask_mode != MaskMode::None && !candidate->mask_source_id.empty())
            track_matte_source_ids.insert(candidate->mask_source_id);
    }

    // Title-owned camera tracks precede layer tracks.  Both panes consume
    // this exact flattened row model, including expanded X/Y/Z children.
    const auto shared_timeline_rows = timeline_rows(title_);
    for (const auto &timeline_row : shared_timeline_rows) {
        if (!timeline_row.is_camera && !timeline_row.is_camera_switch)
            break;
        const bool is_property = timeline_row.is_property;
        const bool is_channel = timeline_row.is_property_channel;
        const int channel_count = is_property && !is_channel
            ? timeline_row.prop.graph_channel_count() : 0;
        const bool channels_expanded = is_property && !is_channel &&
            timeline_property_channels_expanded(*title_, timeline_row.owner_id,
                                                 timeline_row.prop);

        auto *item = new QListWidgetItem();
        item->setData(Qt::UserRole, QString::fromStdString(timeline_row.owner_id));
        item->setData(Qt::UserRole + 1,
                      is_channel ? QStringLiteral("property_channel")
                                 : is_property ? QStringLiteral("property")
                                               : QStringLiteral("camera"));
        if (is_property) {
            item->setData(Qt::UserRole + 2, timeline_row.owner_label);
            item->setData(Qt::UserRole + 3,
                          QString::fromStdString(timeline_row.prop.name()));
            item->setData(Qt::UserRole + 4,
                          is_channel ? timeline_row.property_channel : 3);
        }
        Qt::ItemFlags item_flags = Qt::ItemIsEnabled;
        if (is_property) item_flags |= Qt::ItemIsSelectable;
        item->setFlags(item_flags);
        item->setSizeHint(QSize(0, 28));
        list_->addItem(item);

        auto *row_widget = new QWidget(list_);
        row_widget->setObjectName(is_channel
            ? QStringLiteral("layerKeyframeChannelRow") : QString());
        row_widget->setStyleSheet(QStringLiteral("background:transparent;color:%1;")
                                      .arg(text.name(QColor::HexRgb)));
        auto *layout = new QHBoxLayout(row_widget);
        layout->setContentsMargins(
            kLayerListMargin + (is_channel ? 94 : is_property ? 34 : 4),
            0, kLayerListMargin, 0);
        layout->setSpacing(kLayerListSpacing);

        if (!is_property) {
            const bool is_light = timeline_light_from_owner(
                *title_, timeline_row.owner_id) != nullptr;
            const bool expanded = timeline_row.is_camera_switch
                ? title_->camera_switches_expanded
                : is_light
                    ? timeline_light_from_owner(*title_, timeline_row.owner_id)
                          ->timeline_expanded
                    : ([&]() {
                        const TitleCamera *camera = timeline_camera_from_owner(
                            *title_, timeline_row.owner_id);
                        return camera && camera->timeline_expanded;
                    })();
            auto *caret = new FxmCaretButton(row_widget);
            caret->setCaretState(expanded ? 2 : 0);
            caret->setFixedSize(20, 20);
            const QString owner_kind = is_light
                ? QStringLiteral("light") : QStringLiteral("camera");
            caret->setToolTip(expanded
                ? QStringLiteral("Hide %1 properties").arg(owner_kind)
                : QStringLiteral("Show %1 properties").arg(owner_kind));
            connect(caret, &QToolButton::clicked, this,
                    [this, caret, owner = timeline_row.owner_id]() {
                const bool next = caret->caretState() == 0;
                caret->setCaretState(next ? 2 : 0);
                emit camera_expand_changed(owner, next);
            });
            layout->addWidget(caret);
            auto *camera_icon = new QLabel(row_widget);
            camera_icon->setFixedSize(34, 20);
            camera_icon->setAlignment(Qt::AlignCenter);
            if (timeline_row.is_camera_switch) {
                camera_icon->setText(QStringLiteral("⇄"));
                camera_icon->setStyleSheet(
                    QStringLiteral("color:%1;font-weight:700;")
                        .arg(highlight.name(QColor::HexRgb)));
            } else {
                const auto role = is_light
                    ? TitlePreferences::TimelineColorRole::LightLayer
                    : TitlePreferences::TimelineColorRole::CameraLayer;
                const QColor owner_color = TitlePreferences::timeline_color(role);
                const char *icon_name = is_light
                    ? "layer-type-light.svg" : "layer-type-camera.svg";
                const QColor owner_foreground =
                    fxm_background_aware_foreground(owner_color, pal);
                camera_icon->setPixmap(
                    fxm_icon(icon_name, owner_foreground).pixmap(
                        QSize(kLayerRowIconExtent, kLayerRowIconExtent)));
                camera_icon->setStyleSheet(QStringLiteral(
                    "background:%1;border:1px solid %2;border-radius:2px;color:%3;")
                    .arg(owner_color.name(QColor::HexRgb),
                         fxm_mix_colors(owner_foreground, owner_color, 0.42)
                             .name(QColor::HexRgb),
                         owner_foreground.name(QColor::HexRgb)));
            }
            layout->addWidget(camera_icon);
            auto *name = new QLabel(timeline_row.owner_label, row_widget);
            name->setStyleSheet(QStringLiteral("font-weight:600;"));
            layout->addWidget(name, 1);
            list_->setItemWidget(item, row_widget);
            continue;
        }

        const double local_time = std::clamp(
            playhead_ - timeline_row.in_time, 0.0,
            std::max(0.0, timeline_row.out_time - timeline_row.in_time));

        if (is_channel) {
            const int channel = std::clamp(timeline_row.property_channel, 0, 3);
            const QString channel_name = timeline_property_channel_label(
                timeline_row.prop.graph_all_channels(), channel);
            auto *axis = new QToolButton(row_widget);
            axis->setText(channel_name);
            axis->setFixedSize(24, 20);
            axis->setAutoRaise(true);
            axis->setToolTip(QStringLiteral("Edit the %1 channel in the Graph Editor")
                                 .arg(channel_name));
            axis->setStyleSheet(button_style);
            connect(axis, &QToolButton::clicked, this,
                    [this, owner = timeline_row.owner_id,
                     property = timeline_row.prop.name(), channel]() {
                emit property_graph_target_requested(
                    owner, property, graph_mode_for_property_channel(channel));
            });
            layout->addWidget(axis);
            auto *channel_label = new QLabel(timeline_row.owner_label, row_widget);
            layout->addWidget(channel_label, 1);
            auto *value = new QDoubleSpinBox(row_widget);
            value->setObjectName(QStringLiteral("keyframeValue%1")
                                     .arg(channel_name));
            value->setRange(-1000000.0, 1000000.0);
            value->setDecimals(4);
            value->setSingleStep(0.1);
            value->setKeyboardTracking(false);
            value->setFixedWidth(118);
            value->setValue(timeline_row.prop.graph_value(local_time));
            layout->addWidget(value);
            connect(value, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this, owner = timeline_row.owner_id,
                           property = timeline_row.prop.name(), channel](double next) {
                emit property_channel_value_changed(owner, property, channel, next);
            });
            list_->setItemWidget(item, row_widget);
            continue;
        }

        if (channel_count > 1) {
            auto *caret = new FxmCaretButton(row_widget);
            caret->setObjectName(QStringLiteral("propertyChannelCaret"));
            caret->setCaretState(channels_expanded ? 2 : 0);
            caret->setFixedSize(18, 20);
            caret->setToolTip(channels_expanded
                ? QStringLiteral("Hide property channels")
                : QStringLiteral("Show property channels"));
            connect(caret, &QToolButton::clicked, this,
                    [this, owner = timeline_row.owner_id,
                     property = timeline_row.prop.name(), channels_expanded]() {
                emit property_channels_expanded_changed(
                    owner, property, !channels_expanded);
            });
            layout->addWidget(caret);
        } else {
            layout->addSpacing(18);
        }

        auto *diamond = new KeyframeDiamondButton(row_widget);
        diamond->setObjectName(QStringLiteral("keyframeDiamond"));
        diamond->setActiveKeyframe(
            keyframe_index_at_time(timeline_row.prop, local_time) >= 0);
        const bool owner_is_light = timeline_light_from_owner(
            *title_, timeline_row.owner_id) != nullptr;
        diamond->setIndicatorColor(TitlePreferences::timeline_color(
            owner_is_light ? TitlePreferences::TimelineColorRole::LightLayer
                           : TitlePreferences::TimelineColorRole::CameraLayer));
        diamond->setFixedSize(22, 22);
        diamond->setAutoRaise(true);
        diamond->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;background:transparent;padding:0;}"));
        layout->addWidget(diamond);
        auto *label = new QLabel(timeline_row.owner_label, row_widget);
        label->setMinimumWidth(150);
        layout->addWidget(label, 1);
        connect(diamond, &QToolButton::clicked, this,
                [this, owner = timeline_row.owner_id,
                 property = timeline_row.prop.name()]() {
            emit property_keyframe_toggled(owner, property);
        });

        if (!timeline_row.prop.is_discrete()) {
            if (channel_count > 1) {
                auto *all_channel = new QToolButton(row_widget);
                all_channel->setText(QStringLiteral("All"));
                all_channel->setFixedSize(34, 20);
                all_channel->setAutoRaise(true);
                all_channel->setToolTip(
                    QStringLiteral("Edit all channels in the Graph Editor"));
                all_channel->setStyleSheet(button_style);
                connect(all_channel, &QToolButton::clicked, this,
                        [this, owner = timeline_row.owner_id,
                         property = timeline_row.prop.name()]() {
                    emit property_graph_target_requested(owner, property, 3);
                });
                layout->addWidget(all_channel);
                auto *summary = new QLabel(
                    property_channel_summary(timeline_row.prop, local_time,
                                             timeline_row.prop.name()),
                    row_widget);
                summary->setObjectName(QStringLiteral("keyframeValueSummary"));
                summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                summary->setStyleSheet(QStringLiteral("color:%1;font-size:10px;")
                    .arg(disabled_text.name(QColor::HexRgb)));
                layout->addWidget(summary);
            } else {
                auto *value = new QDoubleSpinBox(row_widget);
                value->setObjectName(QStringLiteral("keyframeValueX"));
                value->setRange(-1000000.0, 1000000.0);
                value->setDecimals(4);
                value->setSingleStep(0.1);
                value->setKeyboardTracking(false);
                value->setFixedWidth(118);
                value->setValue(timeline_row.prop.graph_value(local_time));
                if (timeline_row.prop.name() == "camera_projection") {
                    value->setRange(0.0, 1.0);
                    value->setDecimals(0);
                    value->setSingleStep(1.0);
                }
                layout->addWidget(value);
                connect(value, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this, [this, owner = timeline_row.owner_id,
                               property = timeline_row.prop.name()](double next) {
                    emit property_value_changed(owner, property, next, 0.0, 0.0);
                });
            }
        }
        list_->setItemWidget(item, row_widget);
    }

    const auto display_layers = visible_layer_hierarchy_rows(title_);

    for (int row = 0; row < static_cast<int>(display_layers.size()); ++row) {
        auto l = display_layers[static_cast<size_t>(row)].layer;
        const int hierarchy_depth = display_layers[static_cast<size_t>(row)].depth;
        auto *item = new QListWidgetItem();
        item->setData(Qt::UserRole, QString::fromStdString(l->id));
        item->setData(Qt::UserRole + 1, "layer");
        Qt::ItemFlags row_flags =
            item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        row_flags &= ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        if (!l->linked_media_stream)
            row_flags |= Qt::ItemIsDragEnabled;
        /* Only an unlocked Group accepts an on-row drop. Every other layer
         * exposes only the standard above/below insertion line. */
        if (l->type == LayerType::Group && !l->locked)
            row_flags |= Qt::ItemIsDropEnabled;
        item->setFlags(row_flags & ~Qt::ItemIsUserCheckable);
        item->setData(kLayerGroupDropTargetRole,
                      l->type == LayerType::Group && !l->locked);
        item->setSizeHint(QSize(0, 28));
        list_->addItem(item);

        auto *row_widget = new LayerRowWidget(
            item, layer_color(*l, row), list_);
        auto *hl = new QHBoxLayout(row_widget);
        hl->setContentsMargins(kLayerListMargin, 0, kLayerListMargin, 0);
        hl->setSpacing(kLayerListSpacing);
        auto *drag_handle = new LayerRowDragHandle(
            static_cast<LayerListWidget *>(list_), item, row_widget);
        drag_handle->setStyleSheet(row_button_style);
        hl->addWidget(drag_handle);

        /* Every optional control lives inside a permanent fixed-width cell.
         * Hiding an inapplicable control therefore never shifts later columns
         * away from their shared header positions. */
        auto add_fixed_control_column = [&](QWidget *control, int width,
                                            bool show_control) {
            auto *cell = new QWidget(row_widget);
            cell->setFixedWidth(width);
            cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            auto *cell_layout = new QHBoxLayout(cell);
            cell_layout->setContentsMargins(0, 0, 0, 0);
            cell_layout->setSpacing(0);
            if (control) {
                cell_layout->addWidget(control, 0, Qt::AlignCenter);
                control->setVisible(show_control);
            }
            hl->addWidget(cell);
            return cell;
        };
        const bool is_mask_object = track_matte_source_ids.find(l->id) != track_matte_source_ids.end();

        auto make_toggle = [&](const char *on_icon, const char *off_icon, bool checked,
                               const QString &tip, int fixed_width = kLayerVisibilityWidth) {
            auto *btn = new LayerRowIconButton(row_widget);
            btn->setCheckable(true);
            btn->setChecked(checked);
            set_layer_row_button_icon(btn, checked ? on_icon : off_icon);
            btn->setToolTip(tip);
            btn->setFixedSize(fixed_width, 20);
            btn->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
            btn->setAutoRaise(true);
            btn->setStyleSheet(row_button_style);
            connect(btn, &QToolButton::toggled, btn, [btn, on_icon, off_icon](bool state) {
                set_layer_row_button_icon(btn, state ? on_icon : off_icon);
            });
            hl->addWidget(btn);
            return btn;
        };
        auto add_empty_switch_column = [&](int fixed_width) {
            auto *btn = new LayerRowIconButton(row_widget);
            btn->setFixedSize(fixed_width, 20);
            btn->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
            btn->setAutoRaise(true);
            btn->setEnabled(false);
            btn->setStyleSheet(row_button_style);
            hl->addWidget(btn);
            return btn;
        };

        const LayerMediaKinds media_kinds = layer_media_kinds(title_, *l);
        const bool is_audio_layer = l->type == LayerType::Audio;
        const bool is_non_raster_layer = is_audio_layer ||
            l->type == LayerType::Light || l->type == LayerType::Empty;
        const bool is_group_layer = layer_type_can_have_children(l->type);

        QToolButton *vis = nullptr;
        if (is_audio_layer) {
            vis = add_empty_switch_column(kLayerVisibilityWidth);
            vis->setToolTip(fxm_tr("OBSTitles.AudioHasNoPictureTooltip"));
        } else if (is_mask_object) {
            vis = new LayerRowIconButton(row_widget);
            vis->setCheckable(false);
            vis->setFixedSize(kLayerVisibilityWidth, 20);
            vis->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
            vis->setAutoRaise(true);
            vis->setStyleSheet(row_button_style);
            auto update_matte_visibility_button = [vis](MatteVisibilityMode mode) {
                switch (mode) {
                case MatteVisibilityMode::HiddenInactive:
                    set_layer_row_button_icon(vis, "no-visibility.svg");
                    vis->setToolTip(fxm_tr("OBSTitles.MatteHiddenInactiveTooltip"));
                    break;
                case MatteVisibilityMode::VisibleAndMatte:
                    set_layer_row_button_icon(vis, "visibility-normal.svg");
                    vis->setToolTip(fxm_tr("OBSTitles.MatteVisibleActiveTooltip"));
                    break;
                case MatteVisibilityMode::MatteOnly:
                default:
                    set_layer_row_button_icon(vis, "visibility-matte.svg");
                    vis->setToolTip(fxm_tr("OBSTitles.MatteOnlyTooltip"));
                    break;
                }
            };
            update_matte_visibility_button(l->matte_visibility_mode);
            connect(vis, &QToolButton::clicked, this,
                    [this, id = l->id, item, vis, mode = l->matte_visibility_mode,
                     update_matte_visibility_button]() mutable {
                list_->setCurrentItem(item);
                const int next = (static_cast<int>(mode) + 1) % 3;
                mode = static_cast<MatteVisibilityMode>(next);
                update_matte_visibility_button(mode);
                emit layer_matte_visibility_changed(id, mode);
            });
            hl->addWidget(vis);
        } else {
            vis = make_toggle("visibility-normal.svg", "no-visibility.svg", l->visible,
                              fxm_tr("OBSTitles.LayerVisibilityTooltip"));
            connect(vis, &QToolButton::toggled, this, [this, id = l->id, item](bool checked) {
                list_->setCurrentItem(item);
                emit layer_visibility_changed(id, checked);
            });
        }

        if (media_kinds.audio) {
            auto *audio_toggle = make_toggle("sound.svg", "sound-mute.svg", !l->audio_muted,
                                             fxm_tr("OBSTitles.AudioMuteTooltip"),
                                             kLayerAudioMuteWidth);
            connect(audio_toggle, &QToolButton::toggled, this,
                    [this, id = l->id, item](bool audible) {
                list_->setCurrentItem(item);
                emit layer_audio_mute_changed(id, !audible);
            });
        } else {
            add_empty_switch_column(kLayerAudioMuteWidth);
        }

        QToolButton *lock = make_toggle("layer-lock.svg", "layer-unlock.svg", l->locked, fxm_tr("OBSTitles.LockLayerTooltip"));
        connect(lock, &QToolButton::toggled, this, [this, id = l->id, item](bool checked) {
            list_->setCurrentItem(item);
            emit layer_lock_changed(id, checked);
        });

        const bool is_group = l->type == LayerType::Group;
        const bool is_video = l->type == LayerType::Video;
        const bool is_expandable_container = is_group || is_video;
        const bool is_asset = l->type == LayerType::Asset;
        const int group_state = !is_expandable_container ? -1
            : (!l->group_collapsed ? 2 : (l->properties_expanded ? 1 : 0));
        const bool expanded = !is_expandable_container && layer_keyframe_sections_expanded(*l);
        QToolButton *expand = nullptr;
        if (is_asset) {
            expand = new LayerRowIconButton(row_widget);
            set_layer_row_button_icon(expand, "duplicate.svg");
            expand->setIconSize(QSize(kLayerAssetIconExtent, kLayerAssetIconExtent));
            expand->setToolTip(fxm_tr("OBSTitles.AssetLayerTooltip"));
            expand->setEnabled(false);
            expand->setFixedSize(kLayerExpandWidth, 20);
            expand->setAutoRaise(true);
            expand->setStyleSheet(row_button_style);
        } else {
            auto *caret = new FxmCaretButton(row_widget);
            caret->setCaretState(is_expandable_container ? group_state : (expanded ? 2 : 0));
            expand = caret;
            if (is_expandable_container) {
                caret->setToolTip(group_state == 0
                    ? fxm_tr("OBSTitles.GroupExpansionClosedTooltip")
                    : group_state == 1
                        ? fxm_tr("OBSTitles.GroupExpansionKeyframesTooltip")
                        : fxm_tr("OBSTitles.GroupExpansionChildrenTooltip"));
                connect(caret, &QToolButton::clicked, this,
                        [this, caret, id = l->id]() {
                    const int next_state = (caret->caretState() + 1) % 3;
                    caret->setCaretState(next_state);
                    emit group_expansion_state_changed(id, next_state);
                });
            } else {
                caret->setToolTip(fxm_tr("OBSTitles.ShowKeyframedPropertiesTooltip"));
                connect(caret, &QToolButton::clicked, this,
                        [this, caret, id = l->id]() {
                    const bool next = caret->caretState() == 0;
                    caret->setCaretState(next ? 2 : 0);
                    emit layer_expand_changed(id, next);
                });
            }
        }
        hl->addWidget(expand);

        QLabel *idx = new QLabel(QString::number(row + 1), row_widget);
        idx->setObjectName(QStringLiteral("layerRowIndex"));
        idx->setProperty(kLayerRowForegroundLabelProperty, true);
        idx->setFixedWidth(kLayerIndexWidth);
        idx->setAlignment(Qt::AlignCenter);
        QFont index_font = idx->font();
        index_font.setBold(true);
        idx->setFont(index_font);
        hl->addWidget(idx);

        auto *type = new LayerColorIcon(row_widget);
        type->setFixedWidth(kLayerTypeWidth);
        type->setAlignment(Qt::AlignCenter);
        type->setAccessibleName(fxm_tr("OBSTitles.LayerColorTooltip"));
        type->setToolTip(fxm_tr("OBSTitles.LayerColorTooltip"));
        const QColor effective_layer_color = layer_color(*l, row);
        const QString layer_combo_style =
            make_row_combo_style(effective_layer_color);
        const QString layer_name_style =
            make_row_name_style(effective_layer_color);
        const QColor type_foreground = fxm_background_aware_foreground(
            effective_layer_color, pal);
        QIcon authored_type_icon = layer_type_icon(l->type, type_foreground);
        if (authored_type_icon.isNull())
            authored_type_icon = layer_type_icon(l->type);
        if (!authored_type_icon.isNull()) {
            type->setText(QString());
            type->setPixmap(authored_type_icon.pixmap(
                QSize(kLayerRowIconExtent, kLayerRowIconExtent)));
        } else {
            type->setText(layer_type_short(l->type));
        }
        type->setStyleSheet(QStringLiteral(
            "background:%1;border:1px solid %2;color:%3;font-weight:bold;")
                .arg(effective_layer_color.name(QColor::HexRgb),
                     fxm_mix_colors(type_foreground, effective_layer_color, 0.42)
                         .name(QColor::HexRgb),
                     type_foreground.name(QColor::HexRgb)));
        type->set_click_handler(
            [show_layer_color_menu, type, item, id = l->id,
             has_custom_color = l->custom_ui_color_enabled,
             current_color = effective_layer_color,
             default_color = layer_type_color(l->type)]() {
                show_layer_color_menu(
                    type, item, id, has_custom_color,
                    current_color, default_color);
            });
        hl->addWidget(type);

        const bool has_effect_stack = !l->effects.empty();
        const bool has_enabled_effect_stack = std::any_of(l->effects.begin(), l->effects.end(),
            [](const LayerEffect &effect) { return effect.enabled; });
        const bool has_external_binding = std::any_of(
            l->external_bindings.begin(), l->external_bindings.end(),
            [](const ExternalPropertyBinding &binding) { return binding.enabled; });
        auto *fx_indicator = new FxIndicatorButton(row_widget);
        fx_indicator->setText(has_effect_stack
            ? (has_external_binding ? QStringLiteral("FX•") : fxm_tr("OBSTitles.FX"))
            : (has_external_binding ? QStringLiteral("D") : QString()));
        fx_indicator->setCheckable(has_effect_stack);
        fx_indicator->setChecked(has_enabled_effect_stack);
        fx_indicator->set_effect_stack_disabled(has_effect_stack && !has_enabled_effect_stack);
        fx_indicator->setFixedSize(kLayerFxWidth, 20);
        QString layer_indicator_tip;
        if (has_effect_stack)
            layer_indicator_tip = has_enabled_effect_stack
                ? fxm_tr("OBSTitles.DisableLayerEffectsTooltip")
                : fxm_tr("OBSTitles.EnableLayerEffectsTooltip");
        if (has_external_binding) {
            if (!layer_indicator_tip.isEmpty()) layer_indicator_tip += QStringLiteral("\n");
            layer_indicator_tip += QStringLiteral("Layer has external data binding");
        }
        if (layer_indicator_tip.isEmpty())
            layer_indicator_tip = fxm_tr("OBSTitles.NoEffectsAdded");
        fx_indicator->setToolTip(layer_indicator_tip);
        fx_indicator->setCursor(has_effect_stack ? Qt::PointingHandCursor : Qt::ArrowCursor);
        fx_indicator->setStyleSheet((has_effect_stack || has_external_binding)
            ? QStringLiteral(
                  "QToolButton{background:transparent;border:1px solid %1;border-radius:2px;color:%2;font-size:10px;font-weight:bold;padding:0;}"
                  "QToolButton:checked{background:%3;color:%4;}"
                  "QToolButton:hover{border-color:%3;}")
                  .arg((has_external_binding ? QColor(QStringLiteral("#f0a000")) : border).name(QColor::HexRgb),
                       QStringLiteral("palette(button-text)"),
                       highlight.name(QColor::HexRgb),
                       fxm_background_aware_foreground(highlight, pal).name(QColor::HexRgb))
            : QStringLiteral(
                  "QToolButton{background:transparent;border:none;color:%1;"
                  "font-size:10px;font-weight:bold;padding:0;}")
                  .arg(fxm_background_aware_muted_foreground(
                      effective_layer_color, pal).name(QColor::HexRgb)));
        if (has_effect_stack) {
            connect(fx_indicator, &QToolButton::toggled, this,
                    [this, id = l->id, item, fx_indicator](bool enabled) {
                        list_->setCurrentItem(item);
                        fx_indicator->set_effect_stack_disabled(!enabled);
                        fx_indicator->setToolTip(enabled
                            ? fxm_tr("OBSTitles.DisableLayerEffectsTooltip")
                            : fxm_tr("OBSTitles.EnableLayerEffectsTooltip"));
                        emit layer_effects_enabled_changed(id, enabled);
                    });
        }
        hl->addWidget(fx_indicator);

        auto add_matte_indicator = [&](const char *icon, const QString &tip, bool active) {
            QToolButton *indicator = new LayerRowIconButton(row_widget);
            indicator->setFixedSize(kLayerMatteIndicatorWidth, 20);
            indicator->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
            indicator->setAutoRaise(true);
            indicator->setEnabled(false);
            indicator->setStyleSheet(row_button_style);
            if (active) {
                set_layer_row_button_icon(indicator, icon);
                indicator->setToolTip(tip);
            }
            hl->addWidget(indicator);
        };
        const bool used_as_track_matte = is_mask_object;
        const bool uses_track_matte = l->mask_mode != MaskMode::None && !l->mask_source_id.empty();
        if (uses_track_matte) {
            add_matte_indicator("matte-destination.svg",
                                used_as_track_matte
                                    ? fxm_tr("OBSTitles.MatteSourceAndDestinationTooltip")
                                    : fxm_tr("OBSTitles.MaskedLayerTooltip"),
                                true);
        } else {
            add_matte_indicator("matte-source.svg",
                                fxm_tr("OBSTitles.TrackMatteTooltip"),
                                used_as_track_matte);
        }

        // Keep every layer-list column aligned with the header. Hierarchy indentation
        // belongs only to the flexible Name column; applying it to the row margins
        // shifts every preceding/following control for child layers.
        QWidget *name_cell = new QWidget(row_widget);
        name_cell->setMinimumWidth(kLayerNameMinimumWidth);
        name_cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *name_layout = new QHBoxLayout(name_cell);
        name_layout->setContentsMargins(hierarchy_depth * 18, 0, 0, 0);
        name_layout->setSpacing(0);

        QLineEdit *name = new CompactLayerLineEdit(
            QString::fromStdString(l->name), name_cell);
        name->setObjectName(QStringLiteral("layerNameField"));
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        name->setFrame(false);
        name->setReadOnly(l->locked);
        name->setToolTip(fxm_tr("OBSTitles.RenameLayerTooltip"));
        name->setStyleSheet(layer_name_style);
        enforce_layer_row_compact_field(name);
        connect(name, &QLineEdit::editingFinished, this,
                [this, id = l->id, name]() {
                    emit layer_name_changed(id, name->text().trimmed().toStdString());
                });
        name_layout->addWidget(name, 1, Qt::AlignVCenter);
        hl->addWidget(name_cell, 1);

        QComboBox *mode = new CompactLayerComboBox(row_widget);
        mode->setObjectName(QStringLiteral("layerRowSelector"));
        mode->setFixedWidth(kLayerModeWidth);
        mode->setStyleSheet(layer_combo_style);
        enforce_layer_row_compact_field(mode);
        mode->setToolTip(fxm_tr("OBSTitles.LayerModesTooltip"));
        mode->addItem(fxm_tr("OBSTitles.BlendModeNormal"), (int)EffectBlendMode::Normal);
        mode->addItem(fxm_tr("OBSTitles.BlendModeMultiply"), (int)EffectBlendMode::Multiply);
        mode->addItem(fxm_tr("OBSTitles.BlendModeAdditive"), (int)EffectBlendMode::Additive);
        mode->addItem(fxm_tr("OBSTitles.BlendModeScreen"), (int)EffectBlendMode::Screen);
        mode->addItem(fxm_tr("OBSTitles.BlendModeOverlay"), (int)EffectBlendMode::Overlay);
        mode->addItem(fxm_tr("OBSTitles.BlendModeColor"), (int)EffectBlendMode::Color);
        int mode_idx = mode->findData((int)l->blend_mode);
        mode->setCurrentIndex(mode_idx >= 0 ? mode_idx : 0);
        mode->setEnabled(!is_non_raster_layer);
        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, id = l->id, mode](int index) {
                    emit layer_blend_mode_changed(id, (EffectBlendMode)mode->itemData(index).toInt());
                });
        add_fixed_control_column(mode, kLayerModeWidth,
                                 !is_non_raster_layer);

        QComboBox *matte = new CompactLayerComboBox(row_widget);
        matte->setObjectName(QStringLiteral("layerRowSelector"));
        matte->setFixedWidth(kLayerMaskWidth);
        matte->setStyleSheet(layer_combo_style);
        enforce_layer_row_compact_field(matte);
        matte->setToolTip(fxm_tr("OBSTitles.TrackMatteTooltip"));
        matte->addItem(fxm_tr("OBSTitles.NoMask"), QString());
        for (int candidate_row = 0; candidate_row < static_cast<int>(title_->layers.size()); ++candidate_row) {
            const auto &candidate = title_->layers[static_cast<size_t>(candidate_row)];
            if (!candidate || candidate->id == l->id) continue;
            if (!layer_type_can_be_scene_mask(candidate->type)) continue;
            /* A child cannot use one of its container groups as a matte: the
             * group result already depends on that child, which would create
             * a recursive compositing graph. Groups remain valid matte sources
             * for every layer outside their own subtree. */
            if (layer_type_is_container(candidate->type) &&
                has_group_ancestor(title_, *l, candidate->id))
                continue;
            const int layer_number = static_cast<int>(title_->layers.size()) - candidate_row;
            const QString label = QStringLiteral("%1. %2")
                                      .arg(layer_number)
                                      .arg(QString::fromStdString(candidate->name));
            matte->addItem(label, QString::fromStdString(candidate->id));
        }

        const int matte_idx = matte->findData(QString::fromStdString(l->mask_source_id));
        matte->setCurrentIndex(matte_idx >= 0 ? matte_idx : 0);
        add_fixed_control_column(matte, kLayerMaskWidth,
                                 !is_non_raster_layer);

        const bool has_matte = !l->mask_source_id.empty() && l->mask_mode != MaskMode::None;
        const bool uses_luma = l->mask_mode == MaskMode::Luma || l->mask_mode == MaskMode::InvertedLuma;
        const bool uses_clipping = l->mask_mode == MaskMode::Clipping ||
                                   l->mask_mode == MaskMode::InvertedClipping;
        const bool is_inverted = l->mask_mode == MaskMode::InvertedAlpha ||
                                 l->mask_mode == MaskMode::InvertedLuma ||
                                 l->mask_mode == MaskMode::InvertedClipping;

        QToolButton *matte_type = new LayerRowIconButton(row_widget);
        matte_type->setCheckable(false);
        matte_type->setProperty("matteType", uses_clipping ? 2 : (uses_luma ? 1 : 0));
        auto update_matte_type_button = [matte_type](bool enabled) {
            if (!enabled) {
                set_layer_row_button_icon(matte_type, nullptr);
                matte_type->setToolTip(QString());
                return;
            }
            const int type = matte_type->property("matteType").toInt();
            if (type == 2) {
                set_layer_row_button_icon(matte_type, "matte-clipping.svg");
                matte_type->setToolTip(fxm_tr("OBSTitles.MatteClipping"));
            } else if (type == 1) {
                set_layer_row_button_icon(matte_type, "matte-luma.svg");
                matte_type->setToolTip(fxm_tr("OBSTitles.MatteLuma"));
            } else {
                set_layer_row_button_icon(matte_type, "matte-alpha.svg");
                matte_type->setToolTip(fxm_tr("OBSTitles.MatteAlpha"));
            }
        };
        update_matte_type_button(has_matte);
        matte_type->setFixedSize(kLayerMatteControlWidth, 20);
        matte_type->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
        matte_type->setAutoRaise(true);
        matte_type->setStyleSheet(row_button_style);
        matte_type->setEnabled(has_matte);
        add_fixed_control_column(matte_type, kLayerMatteControlWidth,
                                 !is_non_raster_layer);

        QToolButton *matte_invert = new LayerRowIconButton(row_widget);
        matte_invert->setCheckable(true);
        matte_invert->setChecked(is_inverted);
        if (has_matte) {
            set_layer_row_button_icon(matte_invert, is_inverted ? "matte-inverted.svg" : "matte-normal.svg");
            matte_invert->setToolTip(is_inverted ? fxm_tr("OBSTitles.MatteInverted") : fxm_tr("OBSTitles.MatteNormal"));
        } else {
            set_layer_row_button_icon(matte_invert, nullptr);
            matte_invert->setToolTip(QString());
        }
        matte_invert->setFixedSize(kLayerMatteControlWidth, 20);
        matte_invert->setIconSize(QSize(kLayerRowIconExtent, kLayerRowIconExtent));
        matte_invert->setAutoRaise(true);
        matte_invert->setStyleSheet(row_button_style);
        matte_invert->setEnabled(has_matte);
        add_fixed_control_column(matte_invert, kLayerMatteControlWidth,
                                 !is_non_raster_layer);

        auto selected_matte_mode = [matte_type, matte_invert]() {
            const bool inverted = matte_invert->isChecked();
            switch (matte_type->property("matteType").toInt()) {
            case 2:
                return inverted ? MaskMode::InvertedClipping : MaskMode::Clipping;
            case 1:
                return inverted ? MaskMode::InvertedLuma : MaskMode::Luma;
            case 0:
            default:
                return inverted ? MaskMode::InvertedAlpha : MaskMode::Alpha;
            }
        };

        connect(matte, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, id = l->id, matte, matte_type, matte_invert,
                 selected_matte_mode, update_matte_type_button](int index) {
                    const std::string source_id = matte->itemData(index).toString().toStdString();
                    const bool enabled = !source_id.empty();
                    matte_type->setEnabled(enabled);
                    matte_invert->setEnabled(enabled);
                    update_matte_type_button(enabled);
                    if (enabled) {
                        const bool inverted = matte_invert->isChecked();
                        set_layer_row_button_icon(matte_invert, inverted ? "matte-inverted.svg" : "matte-normal.svg");
                        matte_invert->setToolTip(inverted ? fxm_tr("OBSTitles.MatteInverted") : fxm_tr("OBSTitles.MatteNormal"));
                    } else {
                        set_layer_row_button_icon(matte_invert, nullptr);
                        matte_invert->setToolTip(QString());
                    }
                    emit layer_mask_changed(id, source_id,
                                            enabled ? selected_matte_mode() : MaskMode::None);
                });
        connect(matte_type, &QToolButton::clicked, this,
                [this, id = l->id, matte, matte_type,
                 selected_matte_mode, update_matte_type_button]() {
                    const int next_type = (matte_type->property("matteType").toInt() + 1) % 3;
                    matte_type->setProperty("matteType", next_type);
                    update_matte_type_button(true);
                    const std::string source_id = matte->currentData().toString().toStdString();
                    if (!source_id.empty())
                        emit layer_mask_changed(id, source_id, selected_matte_mode());
                });
        connect(matte_invert, &QToolButton::toggled, this,
                [this, id = l->id, matte, matte_invert, selected_matte_mode](bool inverted) {
                    set_layer_row_button_icon(matte_invert, inverted ? "matte-inverted.svg" : "matte-normal.svg");
                    matte_invert->setToolTip(inverted ? fxm_tr("OBSTitles.MatteInverted") : fxm_tr("OBSTitles.MatteNormal"));
                    const std::string source_id = matte->currentData().toString().toStdString();
                    if (!source_id.empty()) emit layer_mask_changed(id, source_id, selected_matte_mode());
                });

        QComboBox *parent = new CompactLayerComboBox(row_widget);
        parent->setObjectName(QStringLiteral("layerRowSelector"));
        parent->setFixedWidth(kLayerParentWidth);
        parent->setStyleSheet(layer_combo_style);
        enforce_layer_row_compact_field(parent);
        parent->setToolTip(fxm_tr("OBSTitles.ParentLayerTooltip"));
        parent->addItem(fxm_tr("OBSTitles.None"), "");
        for (int candidate_row = 0;
             candidate_row < static_cast<int>(title_->layers.size());
             ++candidate_row) {
            const auto &candidate =
                title_->layers[static_cast<size_t>(candidate_row)];
            if (!candidate || candidate->id == l->id)
                continue;
            const int layer_number =
                static_cast<int>(title_->layers.size()) - candidate_row;
            const QString label = QStringLiteral("%1. %2")
                                      .arg(layer_number)
                                      .arg(QString::fromStdString(candidate->name));
            parent->addItem(label, QString::fromStdString(candidate->id));
        }
        const int parent_idx = parent->findData(
            QString::fromStdString(l->transform_parent_id));
        parent->setCurrentIndex(parent_idx >= 0 ? parent_idx : 0);
        connect(parent, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, id = l->id, parent](int index) {
                    emit layer_parent_changed(
                        id, parent->itemData(index).toString().toStdString());
                });
        add_fixed_control_column(parent, kLayerParentWidth, !is_audio_layer);

        const bool supports_3d = !layer_type_is_audio(l->type) &&
                                 l->type != LayerType::Adjustment;
        auto *dimension_toggle = new QToolButton(row_widget);
        dimension_toggle->setObjectName(QStringLiteral("layerDimensionToggle"));
        dimension_toggle->setCheckable(true);
        dimension_toggle->setChecked(supports_3d &&
            l->dimension_mode == LayerDimensionMode::ThreeD);
        dimension_toggle->setEnabled(supports_3d && !l->locked);
        dimension_toggle->setFixedSize(24, 24);
        dimension_toggle->setAutoRaise(false);
        dimension_toggle->setCursor(supports_3d ? Qt::PointingHandCursor : Qt::ArrowCursor);
        dimension_toggle->setToolTip(supports_3d
            ? QStringLiteral("Toggle this layer between the legacy 2D transform and the XYZ 3D transform.")
            : QStringLiteral("This layer type does not support 3D transforms."));
        auto update_dimension_toggle = [dimension_toggle, supports_3d](bool is_3d) {
            const bool effective_3d = supports_3d && is_3d;
            dimension_toggle->setText(effective_3d ? QStringLiteral("3D") : QStringLiteral("2D"));
            dimension_toggle->setAccessibleName(effective_3d
                ? QStringLiteral("3D Layer") : QStringLiteral("2D Layer"));
            dimension_toggle->setProperty("threeD", effective_3d);
            dimension_toggle->style()->unpolish(dimension_toggle);
            dimension_toggle->style()->polish(dimension_toggle);
        };
        dimension_toggle->setProperty(kLayerRowDimensionToggleProperty, true);
        refresh_layer_dimension_toggle(
            dimension_toggle, row_widget->effective_background(), pal);
        update_dimension_toggle(dimension_toggle->isChecked());
        connect(dimension_toggle, &QToolButton::toggled, this,
                [this, id = l->id, item, update_dimension_toggle](bool is_3d) {
                    list_->setCurrentItem(item);
                    update_dimension_toggle(is_3d);
                    emit layer_dimension_mode_changed(
                        id, is_3d ? LayerDimensionMode::ThreeD : LayerDimensionMode::TwoD);
                });
        add_fixed_control_column(dimension_toggle, kLayerDimensionWidth, true);

        /* Palette roles such as Base and Button can still be filled by the
         * native OBS/Qt style after a transparent stylesheet declaration.
         * Clear them once the complete row exists so its color remains a
         * single uninterrupted surface behind every embedded control. */
        make_layer_row_children_transparent(row_widget);
        row_widget->refresh_contrast();
        for (QWidget *child : row_widget->findChildren<QWidget *>())
            enforce_layer_row_compact_field(child);

        list_->setItemWidget(item, row_widget);
        if ((prev_id.isEmpty() && list_->currentItem() == nullptr) ||
            prev_id == item->data(Qt::UserRole).toString())
            list_->setCurrentItem(item);

        /* Property rows are not rebuilt independently here.  They are read
         * from the same flattened model as TimelineWidget, which guarantees
         * identical row counts/order for aggregate and X/Y/Z rows. */
        if (!layer_keyframe_sections_expanded(*l)) continue;

        for (const auto &timeline_row : shared_timeline_rows) {
            if ((!timeline_row.is_property && !timeline_row.is_effect_group) ||
                timeline_row.owner_id != l->id ||
                timeline_row.layer.get() != l.get())
                continue;

            if (timeline_row.is_effect_group) {
                auto *effect_item = new QListWidgetItem();
                effect_item->setData(Qt::UserRole, QString::fromStdString(l->id));
                effect_item->setData(Qt::UserRole + 1, QStringLiteral("effect_group"));
                effect_item->setData(Qt::UserRole + 2, timeline_row.owner_label);
                effect_item->setData(Qt::UserRole + 5, timeline_row.effect_index);
                effect_item->setFlags(Qt::ItemIsEnabled);
                effect_item->setSizeHint(QSize(0, 28));
                list_->addItem(effect_item);

                auto *effect_widget = new QWidget(list_);
                effect_widget->setObjectName(QStringLiteral("layerEffectGroupRow"));
                effect_widget->setStyleSheet(QStringLiteral(
                    "QWidget{background:transparent;color:%1;}")
                    .arg(text.name(QColor::HexRgb)));
                auto *effect_layout = new QHBoxLayout(effect_widget);
                effect_layout->setContentsMargins(44, 0, 4, 0);
                effect_layout->setSpacing(6);
                auto *fx = new QLabel(QStringLiteral("FX"), effect_widget);
                fx->setFixedWidth(24);
                fx->setAlignment(Qt::AlignCenter);
                fx->setStyleSheet(QStringLiteral(
                    "font-size:9px;font-weight:700;color:%1;border:1px solid %2;"
                    "border-radius:2px;")
                    .arg(highlight.name(QColor::HexRgb),
                         border.name(QColor::HexRgb)));
                effect_layout->addWidget(fx);
                auto *effect_name = new QLabel(timeline_row.owner_label,
                                               effect_widget);
                effect_name->setStyleSheet(QStringLiteral("font-weight:600;"));
                effect_layout->addWidget(effect_name, 1);
                list_->setItemWidget(effect_item, effect_widget);
                continue;
            }

            const TimelinePropertyRef prop = timeline_row.prop;
            const QString label = timeline_row.owner_label;
            const bool is_channel = timeline_row.is_property_channel;
            const int channel = timeline_row.property_channel;
            const int channel_count = is_channel ? 1 : prop.graph_channel_count();
            const bool channels_expanded = !is_channel &&
                timeline_property_channels_expanded(*title_, l->id, prop);
            const double property_local_time = std::clamp(
                playhead_ - timeline_row.in_time, 0.0,
                std::max(0.0, timeline_row.out_time - timeline_row.in_time));

            auto configure_spin = [&](QDoubleSpinBox *spin,
                                      const char *object_name,
                                      int width = 74) {
                spin->setObjectName(QString::fromLatin1(object_name));
                spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
                spin->setKeyboardTracking(false);
                spin->setAlignment(Qt::AlignRight);
                spin->setDecimals(2);
                spin->setRange(-100000.0, 100000.0);
                spin->setFixedWidth(width);
                spin->setMinimumHeight(22);
                spin->setEnabled(!l->locked);
                spin->setStyleSheet(QStringLiteral(
                    "QDoubleSpinBox{color:%1;background:%2;border:1px solid %3;"
                    "border-radius:3px;padding:2px 5px;selection-background-color:%4;selection-color:%5;}"
                    "QDoubleSpinBox:hover{border-color:%6;}"
                    "QDoubleSpinBox:focus{border-color:%4;}"
                    "QDoubleSpinBox:disabled{color:%7;background:%8;}")
                    .arg(field_text.name(QColor::HexRgb),
                         base.name(QColor::HexRgb),
                         border.name(QColor::HexRgb),
                         highlight.name(QColor::HexRgb),
                         pal.color(QPalette::HighlightedText).name(QColor::HexRgb),
                         text.name(QColor::HexRgb),
                         disabled_text.name(QColor::HexRgb),
                         button.name(QColor::HexRgb)));
            };

            auto *prop_item = new QListWidgetItem();
            prop_item->setData(Qt::UserRole, QString::fromStdString(l->id));
            prop_item->setData(Qt::UserRole + 1,
                               is_channel ? QStringLiteral("property_channel")
                                          : QStringLiteral("property"));
            prop_item->setData(Qt::UserRole + 2, label);
            prop_item->setData(Qt::UserRole + 3,
                               QString::fromStdString(prop.name()));
            prop_item->setData(Qt::UserRole + 4, is_channel ? channel : 3);
            prop_item->setFlags((prop_item->flags() | Qt::ItemIsSelectable |
                                 Qt::ItemIsEnabled) &
                                ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
                                  Qt::ItemIsUserCheckable));
            prop_item->setSizeHint(QSize(0, 28));
            list_->addItem(prop_item);

            QWidget *prop_widget = new QWidget(list_);
            prop_widget->setObjectName(is_channel
                ? QStringLiteral("layerKeyframeChannelRow")
                : QStringLiteral("layerKeyframePropertyRow"));
            prop_widget->setStyleSheet(QStringLiteral(
                "QWidget{background:transparent;color:%1;}")
                .arg(text.name(QColor::HexRgb)));
            auto *ph = new QHBoxLayout(prop_widget);
            const int effect_indent = timeline_row.effect_index >= 0 ? 26 : 0;
            ph->setContentsMargins((is_channel ? 88 : 44) + effect_indent,
                                   0, 4, 0);
            ph->setSpacing(4);

            if (is_channel) {
                static const char *spin_names[] = {
                    "keyframeValueX", "keyframeValueY", "keyframeValueZ",
                    "keyframeValueW"};
                const int safe_channel = std::clamp(channel, 0, 3);
                const QString channel_label = timeline_property_channel_label(
                    timeline_row.prop.graph_all_channels(), safe_channel);
                auto *axis = new QToolButton(prop_widget);
                axis->setText(channel_label);
                axis->setFixedSize(24, 20);
                axis->setAutoRaise(true);
                axis->setToolTip(
                    QStringLiteral("Edit the %1 channel in the Graph Editor")
                        .arg(channel_label));
                axis->setStyleSheet(button_style);
                connect(axis, &QToolButton::clicked, this,
                        [this, id = l->id, name = prop.name(), safe_channel]() {
                    emit property_graph_target_requested(
                        id, name, graph_mode_for_property_channel(safe_channel));
                });
                ph->addWidget(axis);
                auto *channel_name = new QLabel(label, prop_widget);
                ph->addWidget(channel_name, 1);
                auto *value = new QDoubleSpinBox(prop_widget);
                configure_spin(value, spin_names[safe_channel], 118);
                double initial = prop.graph_value(property_local_time);
                if (prop.name() == "scale") initial *= 100.0;
                value->setValue(initial);
                ph->addWidget(value);
                connect(value,
                        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this,
                        [this, id = l->id, name = prop.name(), safe_channel]
                        (double next) {
                    emit property_channel_value_changed(
                        id, name, safe_channel, next);
                });
                list_->setItemWidget(prop_item, prop_widget);
                continue;
            }

            if (channel_count > 1) {
                auto *caret = new FxmCaretButton(prop_widget);
                caret->setObjectName(QStringLiteral("propertyChannelCaret"));
                caret->setCaretState(channels_expanded ? 2 : 0);
                caret->setFixedSize(18, 20);
                caret->setToolTip(channels_expanded
                    ? QStringLiteral("Hide property channels")
                    : QStringLiteral("Show property channels"));
                connect(caret, &QToolButton::clicked, this,
                        [this, owner = l->id, name = prop.name(),
                         channels_expanded]() {
                    emit property_channels_expanded_changed(
                        owner, name, !channels_expanded);
                });
                ph->addWidget(caret);
            } else {
                ph->addSpacing(18);
            }

            auto *diamond_indicator = new KeyframeDiamondButton(prop_widget);
            diamond_indicator->setObjectName(QStringLiteral("keyframeDiamond"));
            diamond_indicator->setProperty("layerId", QString::fromStdString(l->id));
            diamond_indicator->setProperty("propertyName", QString::fromStdString(prop.name()));
            diamond_indicator->setActiveKeyframe(
                keyframe_index_at_time(prop, property_local_time) >= 0);
            diamond_indicator->setIndicatorColor(layer_color(*l, row));
            diamond_indicator->setFixedSize(22, 22);
            diamond_indicator->setEnabled(!l->locked);
            diamond_indicator->setAutoRaise(true);
            diamond_indicator->setCursor(Qt::PointingHandCursor);
            diamond_indicator->setToolTip(fxm_tr("OBSTitles.ToggleKeyframe"));
            diamond_indicator->setStyleSheet(QStringLiteral(
                "QToolButton{border:none;background:transparent;padding:0;}"));
            connect(diamond_indicator, &QToolButton::clicked, this,
                    [this, id = l->id, name = prop.name()]() {
                emit property_keyframe_toggled(id, name);
            });
            ph->addWidget(diamond_indicator);
            QLabel *prop_name = new QLabel(label, prop_widget);
            prop_name->setStyleSheet(QStringLiteral("color:%1;")
                                         .arg(text.name(QColor::HexRgb)));
            ph->addWidget(prop_name, 1);

            /* Extension and discrete tracks keep their authoring surface in
             * the owning panel. They remain selectable as graph targets. */
            if (prop.is_extension() || prop.is_discrete()) {
                list_->setItemWidget(prop_item, prop_widget);
                continue;
            }

            if (channel_count > 1) {
                auto *all_channel = new QToolButton(prop_widget);
                all_channel->setText(QStringLiteral("All"));
                all_channel->setFixedSize(34, 20);
                all_channel->setAutoRaise(true);
                all_channel->setToolTip(
                    QStringLiteral("Edit all channels in the Graph Editor"));
                all_channel->setStyleSheet(button_style);
                connect(all_channel, &QToolButton::clicked, this,
                        [this, id = l->id, name = prop.name()]() {
                    emit property_graph_target_requested(id, name, 3);
                });
                ph->addWidget(all_channel);
                auto *summary = new QLabel(
                    property_channel_summary(prop, property_local_time,
                                             prop.name()),
                    prop_widget);
                summary->setObjectName(QStringLiteral("keyframeValueSummary"));
                summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                summary->setStyleSheet(QStringLiteral("color:%1;font-size:10px;")
                    .arg(disabled_text.name(QColor::HexRgb)));
                ph->addWidget(summary);
                list_->setItemWidget(prop_item, prop_widget);
                continue;
            }

            double value = prop.graph_value(property_local_time);
            if (prop.name() == "opacity" || prop.name() == "char_scale_x" ||
                prop.name() == "char_scale_y") value *= 100.0;
            auto *value_x = new QDoubleSpinBox(prop_widget);
            configure_spin(value_x, "keyframeValueX");
            value_x->setValue(value);
            ph->addWidget(value_x);
            connect(value_x,
                    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this,
                    [this, id = l->id, name = prop.name()](double next) {
                emit property_value_changed(id, name, next, 0.0, 0.0);
            });
            list_->setItemWidget(prop_item, prop_widget);
        }

    }
    /* Hard invariant: the layer-list section and TimelineWidget are two
     * renderers of the same flattened row model. */
    Q_ASSERT(list_->count() == static_cast<int>(shared_timeline_rows.size()));
    list_->blockSignals(false);
    update_property_rows();
    on_selection_changed();
}

void LayerStack::set_selected_layer(const std::string &layer_id)
{
    set_selected_layers(layer_id.empty() ? std::vector<std::string>()
                                         : std::vector<std::string>{layer_id});
}

void LayerStack::set_selected_layers(const std::vector<std::string> &layer_ids)
{
    std::set<QString> desired_ids;
    for (const auto &id : layer_ids)
        if (!id.empty())
            desired_ids.insert(QString::fromStdString(id));

    const QString desired_primary = layer_ids.empty()
        ? QString() : QString::fromStdString(layer_ids.back());
    std::set<QString> current_ids;
    for (auto *item : list_->selectedItems()) {
        if (item->data(Qt::UserRole + 1).toString() == QStringLiteral("layer"))
            current_ids.insert(item->data(Qt::UserRole).toString());
    }
    QListWidgetItem *current_item = list_->currentItem();
    const bool current_is_layer = current_item &&
        current_item->data(Qt::UserRole + 1).toString() == QStringLiteral("layer");
    const QString current_primary = current_is_layer
        ? current_item->data(Qt::UserRole).toString() : QString();
    if (current_ids == desired_ids && current_primary == desired_primary)
        return;

    QSignalBlocker blocker(list_);
    list_->clearSelection();
    if (layer_ids.empty()) {
        list_->setCurrentItem(nullptr);
        refresh_layer_row_backgrounds();
        return;
    }

    QListWidgetItem *current = nullptr;
    for (int i = 0; i < list_->count(); ++i) {
        auto *item = list_->item(i);
        if (item->data(Qt::UserRole + 1).toString() != QStringLiteral("layer"))
            continue;
        const QString id = item->data(Qt::UserRole).toString();
        if (desired_ids.find(id) != desired_ids.end()) {
            item->setSelected(true);
            if (id == desired_primary)
                current = item;
        }
    }
    if (current)
        list_->setCurrentItem(current, QItemSelectionModel::NoUpdate);
    refresh_layer_row_backgrounds();
}

std::string LayerStack::selected_id() const
{
    auto *item = list_->currentItem();
    if (item && item->isSelected())
        return item->data(Qt::UserRole).toString().toStdString();
    auto selected = list_->selectedItems();
    return selected.isEmpty() ? std::string() : selected.back()->data(Qt::UserRole).toString().toStdString();
}

std::vector<std::string> LayerStack::selected_ids() const
{
    std::vector<std::string> ids;
    for (auto *item : list_->selectedItems()) {
        if (item->data(Qt::UserRole + 1).toString() != "layer") continue;
        ids.push_back(item->data(Qt::UserRole).toString().toStdString());
    }
    return ids;
}

void LayerStack::on_selection_changed()
{
    refresh_layer_row_backgrounds();
    if (QListWidgetItem *current = list_ ? list_->currentItem() : nullptr) {
        const QString kind = current->data(Qt::UserRole + 1).toString();
        if (kind == QStringLiteral("property") ||
            kind == QStringLiteral("property_channel")) {
            int graph_mode = current->data(Qt::UserRole + 4).isValid()
                ? current->data(Qt::UserRole + 4).toInt() : 3;
            if (kind == QStringLiteral("property_channel"))
                graph_mode = graph_mode_for_property_channel(graph_mode);
            emit property_graph_target_requested(
                current->data(Qt::UserRole).toString().toStdString(),
                current->data(Qt::UserRole + 3).toString().toStdString(),
                graph_mode);
        }
    }
    std::string id = selected_id();
    const bool has_layer = !id.empty() && title_ && title_->find_layer(id);
    bool has_deletable_layer = false;
    if (title_) {
        for (const auto &selected_id : selected_ids()) {
            const auto layer = title_->find_layer(selected_id);
            if (layer && !layer->linked_media_stream &&
                !stinger_transition_input_layer_is_protected(*layer)) {
                has_deletable_layer = true;
                break;
            }
        }
    }
    if (btn_del_) btn_del_->setEnabled(has_deletable_layer);

    bool can_move_up = false;
    bool can_move_down = false;
    if (has_layer) {
        auto selected = selected_ids();
        if (selected.size() > 1)
            emit layers_selected(selected);
        if (auto layer = title_->find_layer(id)) {
            std::vector<std::shared_ptr<Layer>> siblings;
            const std::string scope = hierarchy_scope_id(title_, *layer);
            for (const auto &candidate : title_->layers) {
                if (candidate && hierarchy_scope_id(title_, *candidate) == scope)
                    siblings.push_back(candidate);
            }
            const auto it = std::find_if(siblings.begin(), siblings.end(),
                [&](const auto &candidate) { return candidate && candidate->id == id; });
            if (it != siblings.end()) {
                const int idx = static_cast<int>(std::distance(siblings.begin(), it));
                can_move_down = idx > 0;
                can_move_up = idx < static_cast<int>(siblings.size()) - 1;
            }
        }
        if (selected.size() <= 1)
            emit layer_selected(id);
    }
    if (!has_layer) {
        emit layer_selected(std::string());
        emit layers_selected({});
    }

    if (btn_move_up_) btn_move_up_->setEnabled(can_move_up);
    if (btn_move_down_) btn_move_down_->setEnabled(can_move_down);
}

void LayerStack::on_add_text() { emit add_layer_requested(LayerType::Text); }
void LayerStack::on_add_clock() { emit add_layer_requested(LayerType::Clock); }
void LayerStack::on_add_ticker() { emit add_layer_requested(LayerType::Ticker); }
void LayerStack::on_add_rect() { emit add_layer_requested(LayerType::Shape); }
void LayerStack::on_add_image() { emit add_layer_requested(LayerType::Image); }
void LayerStack::on_add_video() { emit add_layer_requested(LayerType::Video); }
void LayerStack::on_add_audio() { emit add_layer_requested(LayerType::Audio); }
void LayerStack::on_add_empty() { emit add_layer_requested(LayerType::Empty); }
void LayerStack::on_add_chart() { emit add_layer_requested(LayerType::Chart); }
void LayerStack::on_add_adjustment() { emit add_layer_requested(LayerType::Adjustment); }
void LayerStack::on_add_color_solid() { emit add_layer_requested(LayerType::ColorSolid); }
void LayerStack::on_add_camera() { emit add_camera_requested(); }
void LayerStack::on_add_ambient_light() { emit add_light_requested(TitleLightType::Ambient); }
void LayerStack::on_add_point_light() { emit add_light_requested(TitleLightType::Point); }
void LayerStack::on_add_spot_light() { emit add_light_requested(TitleLightType::Spot); }
void LayerStack::on_add_parallel_light() { emit add_light_requested(TitleLightType::Parallel); }
void LayerStack::on_add_environment_light() { emit add_light_requested(TitleLightType::Environment); }

void LayerStack::on_move_up()
{
    std::string id = selected_id();
    if (!title_ || id.empty()) return;
    if (!move_layer_within_hierarchy(title_, id, +1)) return;
    emit layer_order_changed();
    set_selected_layer(id);
}

void LayerStack::on_move_down()
{
    std::string id = selected_id();
    if (!title_ || id.empty()) return;
    if (!move_layer_within_hierarchy(title_, id, -1)) return;
    emit layer_order_changed();
    set_selected_layer(id);
}

void LayerStack::on_delete()
{
    if (!title_)
        return;
    for (const auto &id : selected_ids()) {
        const auto layer = title_->find_layer(id);
        if (layer && !layer->linked_media_stream &&
            !stinger_transition_input_layer_is_protected(*layer)) {
            /* The editor command consumes the complete selection. Emit any
             * editable anchor so a mixed A/B + artwork selection still deletes
             * only the user-owned layers and never the protected inputs. */
            emit delete_layer_requested(id);
            return;
        }
    }
}

void LayerStack::show_layer_context_menu(const QPoint &pos)
{
    if (!title_) return;

    QListWidgetItem *item = list_->itemAt(pos);
    auto style_menu = [this](QMenu *menu) {
        if (!menu) return;
        const QPalette pal = palette();
        menu->setStyleSheet(QStringLiteral(
            "QMenu{color:%1;background:%2;border:1px solid %3;}"
            "QMenu::item{padding:5px 22px;}"
            "QMenu::item:selected{background:%4;color:%5;}"
            "QMenu::item:disabled{color:%6;}")
            .arg(pal.color(QPalette::Text).name(QColor::HexRgb),
                 pal.color(QPalette::Base).name(QColor::HexRgb),
                 pal.color(QPalette::Mid).name(QColor::HexRgb),
                 pal.color(QPalette::Highlight).name(QColor::HexRgb),
                 pal.color(QPalette::HighlightedText).name(QColor::HexRgb),
                 pal.color(QPalette::Disabled, QPalette::Text).name(QColor::HexRgb)));
    };
    if (!item) {
        if (btn_add_ && btn_add_->menu()) {
            style_menu(btn_add_->menu());
            btn_add_->menu()->exec(list_->viewport()->mapToGlobal(pos));
        }
        return;
    }

    if (item->data(Qt::UserRole + 1).toString() == QStringLiteral("property")) {
        const std::string layer_id = item->data(Qt::UserRole).toString().toStdString();
        const std::string property_name = item->data(Qt::UserRole + 3).toString().toStdString();
        if (timeline_owner_locked(*title_, layer_id)) return;
        TimelinePropertyRef prop = timeline_property_for_owner(*title_, layer_id, property_name);
        if (!prop) return;
        const double owner_in = timeline_owner_in_time(*title_, layer_id);
        const double owner_out = timeline_owner_out_time(*title_, layer_id);
        const double local_time = std::clamp(playhead_ - owner_in, 0.0,
            std::max(0.0, owner_out - owner_in));
        const int key_index = keyframe_index_at_time(prop, local_time);

        QMenu menu(this);
        style_menu(&menu);
        QAction *toggle_key = menu.addAction(key_index >= 0
            ? fxm_tr("OBSTitles.DeleteKeyframe") : fxm_tr("OBSTitles.AddKeyframe"));
        QMenu *temporal = menu.addMenu(fxm_tr("OBSTitles.TemporalInterpolation"));
        style_menu(temporal);
        temporal->setEnabled(key_index >= 0 && !prop.is_hold_only());
        std::map<QAction *, TemporalInterpolationMode> modes;
        auto temporal_label = [](TemporalInterpolationMode mode) {
            switch (mode) {
            case TemporalInterpolationMode::Linear: return fxm_tr("OBSTitles.Linear");
            case TemporalInterpolationMode::Hold: return fxm_tr("OBSTitles.Hold");
            case TemporalInterpolationMode::AutoBezier: return fxm_tr("OBSTitles.TemporalAutoBezier");
            case TemporalInterpolationMode::ContinuousBezier: return fxm_tr("OBSTitles.TemporalContinuousBezier");
            case TemporalInterpolationMode::ManualBezier: return fxm_tr("OBSTitles.TemporalManualBezier");
            }
            return fxm_tr("OBSTitles.Linear");
        };
        auto *mode_group = new QActionGroup(temporal);
        mode_group->setExclusive(true);
        for (TemporalInterpolationMode mode : {TemporalInterpolationMode::Linear,
                 TemporalInterpolationMode::Hold, TemporalInterpolationMode::AutoBezier,
                 TemporalInterpolationMode::ContinuousBezier,
                 TemporalInterpolationMode::ManualBezier}) {
            QAction *action = temporal->addAction(temporal_label(mode));
            action->setCheckable(true);
            action->setActionGroup(mode_group);
            action->setChecked(key_index >= 0 &&
                prop.keyframe_temporal_mode(static_cast<size_t>(key_index)) == mode);
            modes[action] = mode;
        }
        temporal->addSeparator();
        QAction *easy = temporal->addAction(fxm_tr("OBSTitles.EasyEase"));
        QAction *easy_in = temporal->addAction(fxm_tr("OBSTitles.EasyEaseIn"));
        QAction *easy_out = temporal->addAction(fxm_tr("OBSTitles.EasyEaseOut"));
        temporal->addSeparator();
        QAction *velocity = temporal->addAction(fxm_tr("OBSTitles.KeyframeVelocity"));

        QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == toggle_key) {
            emit property_keyframe_toggled(layer_id, property_name);
            return;
        }
        const auto mode = modes.find(chosen);
        if (mode != modes.end()) {
            emit property_temporal_mode_changed(layer_id, property_name,
                                                static_cast<int>(mode->second));
            return;
        }
        if (chosen == easy || chosen == easy_in || chosen == easy_out) {
            emit property_easy_ease_requested(layer_id, property_name,
                chosen == easy || chosen == easy_in,
                chosen == easy || chosen == easy_out);
            return;
        }
        if (chosen == velocity)
            emit property_velocity_requested(layer_id, property_name);
        return;
    }

    std::string id = item ? item->data(Qt::UserRole).toString().toStdString() : selected_id();
    if (id.empty()) return;

    if (item && item->data(Qt::UserRole + 1).toString() == "layer" && !item->isSelected())
        list_->setCurrentItem(item);

    const std::vector<std::string> selection = selected_ids();
    const auto selection_has_copyable_layer = [this, &selection]() {
        return std::any_of(selection.begin(), selection.end(),
            [this](const std::string &layer_id) {
                const auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
                return layer && !layer->linked_media_stream;
            });
    };
    const auto selection_has_deletable_layer = [this, &selection]() {
        return std::any_of(selection.begin(), selection.end(),
            [this](const std::string &layer_id) {
                const auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
                return layer && !layer->linked_media_stream &&
                       !stinger_transition_input_layer_is_protected(*layer);
            });
    };
    auto selected_layer_is_group = [this](const std::string &layer_id) {
        auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
        return layer && layer->type == LayerType::Group;
    };
    auto parent_is_group = [this](const std::string &layer_id) {
        auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
        auto parent = layer && !layer->parent_id.empty() ? title_->find_layer(layer->parent_id) : nullptr;
        return parent && layer_type_is_container(parent->type);
    };
    auto would_cycle = [this](const std::string &child_id, const std::string &group_id) {
        if (child_id.empty() || group_id.empty() || child_id == group_id)
            return true;
        std::string current = group_id;
        int guard = 0;
        while (!current.empty() && guard++ < 64) {
            if (current == child_id)
                return true;
            auto layer = title_->find_layer(current);
            if (!layer)
                break;
            current = layer->parent_id;
        }
        return false;
    };

    QMenu menu(this);
    style_menu(&menu);
    const bool has_copyable_layer = selection_has_copyable_layer();
    const bool has_deletable_layer = selection_has_deletable_layer();
    const int structural_selection_count = static_cast<int>(std::count_if(
        selection.begin(), selection.end(), [this](const std::string &layer_id) {
            const auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
            return layer && !layer->linked_media_stream;
        }));
    QAction *group_layers = menu.addAction(fxm_tr("OBSTitles.GroupLayers"));
    group_layers->setEnabled(structural_selection_count >= 2);
    QAction *ungroup_layers = menu.addAction(fxm_tr("OBSTitles.UngroupLayers"));
    ungroup_layers->setEnabled(std::any_of(selection.begin(), selection.end(), selected_layer_is_group));

    QMenu *add_to_group = menu.addMenu(fxm_tr("OBSTitles.AddToGroup"));
    style_menu(add_to_group);
    std::map<QAction *, std::string> group_targets;
    bool has_available_group = false;
    for (const auto &candidate : title_->layers) {
        if (!candidate || candidate->type != LayerType::Group || candidate->locked)
            continue;
        bool valid = false;
        for (const auto &selected_id : selection) {
            const auto selected_layer = title_->find_layer(selected_id);
            if (!selected_layer || selected_layer->locked ||
                selected_layer->linked_media_stream ||
                selected_id == candidate->id ||
                selected_layer->parent_id == candidate->id ||
                would_cycle(selected_id, candidate->id))
                continue;
            valid = true;
            break;
        }
        if (!valid)
            continue;
        QAction *target = add_to_group->addAction(QString::fromStdString(candidate->name));
        group_targets[target] = candidate->id;
        has_available_group = true;
    }
    if (!has_available_group) {
        QAction *none = add_to_group->addAction(fxm_tr("OBSTitles.NoAvailableGroups"));
        none->setEnabled(false);
    }
    QAction *remove_from_group = menu.addAction(fxm_tr("OBSTitles.RemoveFromGroup"));
    remove_from_group->setEnabled(std::any_of(selection.begin(), selection.end(), parent_is_group));

    menu.addSeparator();
    QAction *clone = menu.addAction(fxm_tr("OBSTitles.CloneLayer"));
    clone->setEnabled(has_copyable_layer);
    QAction *copy = menu.addAction(fxm_tr("OBSTitles.CopyLayer"));
    copy->setEnabled(has_copyable_layer);
    QAction *paste = menu.addAction(fxm_tr("OBSTitles.PasteLayer"));
    paste->setEnabled(layer_clipboard_available_);
    menu.addSeparator();
    QAction *del = menu.addAction(fxm_tr("OBSTitles.DeleteLayer"));
    del->setEnabled(has_deletable_layer);

    QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (chosen == group_layers) emit group_layers_requested();
    else if (chosen == ungroup_layers) emit ungroup_layers_requested();
    else if (chosen == remove_from_group) emit remove_from_group_requested();
    else if (chosen && group_targets.find(chosen) != group_targets.end())
        emit add_to_group_requested(group_targets.at(chosen));
    else if (chosen == clone) emit clone_layer_requested(id);
    else if (chosen == copy) emit copy_layer_requested(id);
    else if (chosen == paste) emit paste_layer_requested(id);
    else if (chosen == del) emit delete_layer_requested(id);
}

void LayerStack::on_item_changed(QListWidgetItem *item)
{
    std::string id = item->data(Qt::UserRole).toString().toStdString();
    bool v = (item->checkState() == Qt::Checked);
    emit layer_visibility_changed(id, v);
}

/* ══════════════════════════════════════════════════════════════════
 *  TimelineWidget
 * ══════════════════════════════════════════════════════════════════ */
