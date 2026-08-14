#include "tools-sidebar.h"

#include "editor-tool-icons.h"
#include "hold-menu-tool-button.h"
#include "title-assets.h"
#include "title-localization.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

using fxm::editor::tool_icons::cursor_tool_icon;
using fxm::editor::tool_icons::direct_selection_tool_icon;
using fxm::editor::tool_icons::gradient_tool_icon;
using fxm::editor::tool_icons::pen_tool_icon;
using fxm::editor::tool_icons::shape_display_name;
using fxm::editor::tool_icons::shape_tool_icon;
using fxm::editor::tool_icons::text_tool_display_name;
using fxm::editor::tool_icons::text_tool_icon;


ForegroundBackgroundSwatch::ForegroundBackgroundSwatch(QWidget *parent) : QWidget(parent)
{
    setFixedSize(42, 42);
    setCursor(Qt::PointingHandCursor);
    setToolTip(fxm_tr("OBSTitles.ForegroundBackgroundTooltip"));
    foreground_fill_.type = 0;
    foreground_fill_.color = foreground_color_;
    foreground_fill_.start = foreground_color_;
    foreground_fill_.end = foreground_color_;
    background_fill_.type = 0;
    background_fill_.color = background_color_;
    background_fill_.start = background_color_;
    background_fill_.end = background_color_;
}

void ForegroundBackgroundSwatch::set_foreground_color(const QColor &color)
{
    if (!color.isValid()) return;
    if (!foreground_mixed_ && foreground_fill_.type == 0 &&
        foreground_color_ == color && foreground_fill_.color == color)
        return;
    foreground_mixed_ = false;
    foreground_color_ = color;
    foreground_fill_.type = 0;
    foreground_fill_.color = color;
    foreground_fill_.start = color;
    foreground_fill_.end = color;
    update();
}

void ForegroundBackgroundSwatch::set_background_color(const QColor &color)
{
    if (!color.isValid()) return;
    if (!background_mixed_ && background_fill_.type == 0 &&
        background_color_ == color && background_fill_.color == color)
        return;
    background_mixed_ = false;
    background_color_ = color;
    background_fill_.type = 0;
    background_fill_.color = color;
    background_fill_.start = color;
    background_fill_.end = color;
    update();
}

void ForegroundBackgroundSwatch::set_foreground_gradient(const QColor &start, const QColor &end, int gradient_type)
{
    if (!start.isValid() || !end.isValid()) return;
    if (!foreground_mixed_ && foreground_fill_.type == 1 &&
        foreground_fill_.start == start && foreground_fill_.end == end &&
        foreground_fill_.gradient_type == gradient_type)
        return;
    foreground_mixed_ = false;
    foreground_color_ = start;
    foreground_fill_.type = 1;
    foreground_fill_.start = start;
    foreground_fill_.end = end;
    foreground_fill_.gradient_type = gradient_type;
    update();
}

void ForegroundBackgroundSwatch::set_background_gradient(const QColor &start, const QColor &end, int gradient_type)
{
    if (!start.isValid() || !end.isValid()) return;
    if (!background_mixed_ && background_fill_.type == 1 &&
        background_fill_.start == start && background_fill_.end == end &&
        background_fill_.gradient_type == gradient_type)
        return;
    background_mixed_ = false;
    background_color_ = start;
    background_fill_.type = 1;
    background_fill_.start = start;
    background_fill_.end = end;
    background_fill_.gradient_type = gradient_type;
    update();
}

void ForegroundBackgroundSwatch::set_foreground_mixed(bool mixed)
{
    if (foreground_mixed_ == mixed) return;
    foreground_mixed_ = mixed;
    update();
}

void ForegroundBackgroundSwatch::set_background_mixed(bool mixed)
{
    if (background_mixed_ == mixed) return;
    background_mixed_ = mixed;
    update();
}

QRect ForegroundBackgroundSwatch::foreground_rect() const
{
    return QRect(4, 14, 24, 24);
}

QRect ForegroundBackgroundSwatch::background_rect() const
{
    return QRect(14, 4, 24, 24);
}

QRect ForegroundBackgroundSwatch::swap_rect() const
{
    return QRect(width() - 17, 0, 17, 17);
}

void ForegroundBackgroundSwatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = qApp->palette();
    const QColor border = pal.color(QPalette::Mid);
    const QColor text = pal.color(QPalette::WindowText);

    auto draw_swatch = [&](const QRect &r, const SwatchFill &fill, bool mixed) {
        const QRect inner = r.adjusted(1, 1, -1, -1);
        if (fill.type == 1) {
            QBrush brush;
            switch (fill.gradient_type) {
            case 1: { // radial
                QRadialGradient grad(inner.center(), std::max(inner.width(), inner.height()) / 2.0);
                grad.setColorAt(0.0, fill.start);
                grad.setColorAt(1.0, fill.end);
                brush = QBrush(grad);
                break;
            }
            case 2: { // conical
                QConicalGradient grad(inner.center(), 0.0);
                grad.setColorAt(0.0, fill.start);
                grad.setColorAt(0.5, fill.end);
                grad.setColorAt(1.0, fill.end);
                brush = QBrush(grad);
                break;
            }
            default: {
                QLinearGradient grad(inner.topLeft(), inner.bottomRight());
                grad.setColorAt(0.0, fill.start);
                grad.setColorAt(1.0, fill.end);
                brush = QBrush(grad);
                break;
            }
            }
            p.fillRect(inner, brush);
        } else {
            p.fillRect(inner, fill.color);
        }
        // Two-tone outline keeps white, black and transparent swatches legible
        // on both light and dark OBS themes.
        const QColor outer_border(24, 24, 24, 230);
        const QColor inner_border(245, 245, 245, 230);
        p.setPen(QPen(outer_border, 1));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(QPen(inner_border, 1));
        p.drawRect(r.adjusted(1, 1, -2, -2));
        const QColor marker = (fill.type == 1) ? fill.end : fill.color;
        if (marker.alpha() < 255) {
            p.setPen(QPen(text, 1));
            p.drawLine(r.topLeft() + QPoint(2, 2), r.bottomRight() - QPoint(2, 2));
        }
        if (mixed) {
            p.fillRect(inner, QColor(24, 24, 24, 215));
            QFont mixed_font = p.font();
            mixed_font.setPixelSize(14);
            mixed_font.setBold(true);
            p.setFont(mixed_font);
            p.setPen(QColor(QStringLiteral("#f2c94c")));
            p.drawText(inner, Qt::AlignCenter, fxm_tr("OBSTitles.M"));
        }
    };

    draw_swatch(background_rect(), background_fill_, background_mixed_);
    draw_swatch(foreground_rect(), foreground_fill_, foreground_mixed_);

    p.setPen(QPen(text, 1));
    QFont f = p.font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    p.drawText(swap_rect(), Qt::AlignCenter, QStringLiteral("⇄"));
}

void ForegroundBackgroundSwatch::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->pos();
    if (swap_rect().contains(pos)) {
        emit swap_requested();
    } else if (foreground_rect().contains(pos)) {
        emit foreground_requested();
    } else if (background_rect().contains(pos)) {
        emit background_requested();
    }
    QWidget::mousePressEvent(event);
}

ToolsSidebar::ToolsSidebar(QWidget *parent) : QWidget(parent)
{
    constexpr int kSidebarIconSize = 22;
    constexpr int kSidebarButtonSize = 36;
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(108);

    setObjectName(QStringLiteral("FluxMotionToolsSidebarPanel"));
    setMinimumWidth(kSidebarButtonSize + 8);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral(
        "QWidget#FluxMotionToolsSidebarPanel{background:%1;color:%2;}"
        "QToolButton{color:%3;background:%4;border:1px solid transparent;border-radius:3px;padding:0;"
        "min-width:%5px;min-height:%5px;max-width:%5px;max-height:%5px;}"
        "QToolButton:hover{background:%6;border-color:%7;}"
        "QToolButton:checked{background:%8;color:%9;border-color:%8;}"
        "QToolButton::menu-indicator{image:none;width:0px;}"
        "QMenu{color:%10;background:%11;border:1px solid %7;}"
        "QMenu::item{padding:5px 22px;}"
        "QMenu::item:selected{background:%8;color:%9;}")
        .arg(window.name(QColor::HexRgb),
             text.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             button.name(QColor::HexRgb),
             QString::number(kSidebarButtonSize),
             hover.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb),
             pal.color(QPalette::Text).name(QColor::HexRgb),
             base.name(QColor::HexRgb)));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(4);
    auto *tools_section = new QWidget(this);
    auto *tools_layout = new QVBoxLayout(tools_section);
    tools_layout->setContentsMargins(0, 0, 0, 0);
    tools_layout->setSpacing(4);
    auto *swatch_section = new QWidget(this);
    auto *swatch_layout = new QVBoxLayout(swatch_section);
    swatch_layout->setContentsMargins(0, 0, 0, 0);
    swatch_layout->setSpacing(0);
    layout->addWidget(tools_section, 0, Qt::AlignHCenter);
    layout->addWidget(swatch_section, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    tool_group_ = new QActionGroup(this);
    tool_group_->setExclusive(true);

    auto make_tool_button = [this, tools_layout](const QString &text, const QIcon &icon, const QString &tip) {
        auto *button = new HoldMenuToolButton(this);
        button->setText(text);
        button->setAccessibleName(text);
        button->setToolTip(tip);
        button->setIcon(icon);
        button->setIconSize(QSize(kSidebarIconSize, kSidebarIconSize));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setFixedSize(kSidebarButtonSize, kSidebarButtonSize);
        tools_layout->addWidget(button, 0, Qt::AlignHCenter);
        return button;
    };

    selection_button_ = make_tool_button(fxm_tr("OBSTitles.SelectionTool"), cursor_tool_icon(),
                                         fxm_tr("OBSTitles.SelectionToolTooltip") + QStringLiteral(" (V)"));
    direct_selection_button_ = make_tool_button(fxm_tr("OBSTitles.DirectSelectionTool"), direct_selection_tool_icon(),
                                                fxm_tr("OBSTitles.DirectSelectionToolTooltip") + QStringLiteral(" (A)"));
    free_transform_button_ = make_tool_button(fxm_tr("OBSTitles.FreeTransformTool"), cursor_tool_icon(),
                                              fxm_tr("OBSTitles.FreeTransformToolTooltip") + QStringLiteral(" (E)"));
    shape_button_ = make_tool_button(fxm_tr("OBSTitles.ShapeTool"), shape_tool_icon(selected_shape_),
                                     fxm_tr("OBSTitles.ShapeToolTooltip") + QStringLiteral(" (M)"));
    pen_button_ = make_tool_button(fxm_tr("OBSTitles.PenTool"), pen_tool_icon(),
                                   fxm_tr("OBSTitles.PenToolTooltip") + QStringLiteral(" (P)"));
    text_button_ = make_tool_button(fxm_tr("OBSTitles.TextTool"), text_tool_icon(selected_text_layer_type_),
                                    fxm_tr("OBSTitles.TextToolTooltip") + QStringLiteral(" (T)"));
    image_button_ = make_tool_button(fxm_tr("OBSTitles.ImageTool"), fxm_icon("image.svg"),
                                     fxm_tr("OBSTitles.ImageToolTooltip"));
    color_picker_button_ = make_tool_button(fxm_tr("OBSTitles.ColorPickerTool"), fxm_icon("eyedropper.svg"),
                                            fxm_tr("OBSTitles.ColorPickerToolTooltip") + QStringLiteral(" (I)"));
    gradient_button_ = make_tool_button(fxm_tr("OBSTitles.GradientTool"), gradient_tool_icon(),
                                        fxm_tr("OBSTitles.GradientToolTooltip") + QStringLiteral(" (G)"));

    auto *selection_action = new QAction(cursor_tool_icon(), fxm_tr("OBSTitles.SelectionTool"), this);
    selection_action->setCheckable(true);
    selection_action->setChecked(true);
    auto *direct_selection_action = new QAction(direct_selection_tool_icon(), fxm_tr("OBSTitles.DirectSelectionTool"), this);
    direct_selection_action->setCheckable(true);
    auto *free_transform_action = new QAction(cursor_tool_icon(), fxm_tr("OBSTitles.FreeTransformTool"), this);
    free_transform_action->setCheckable(true);
    auto *shape_action = new QAction(shape_tool_icon(selected_shape_), fxm_tr("OBSTitles.ShapeTool"), this);
    shape_action->setCheckable(true);
    auto *pen_action = new QAction(pen_tool_icon(), fxm_tr("OBSTitles.PenTool"), this);
    pen_action->setCheckable(true);
    auto *text_action = new QAction(text_tool_icon(selected_text_layer_type_), fxm_tr("OBSTitles.TextTool"), this);
    text_action->setCheckable(true);
    auto *image_action = new QAction(fxm_icon("image.svg"), fxm_tr("OBSTitles.ImageTool"), this);
    image_action->setCheckable(true);
    auto *color_picker_action = new QAction(fxm_icon("eyedropper.svg"), fxm_tr("OBSTitles.ColorPickerTool"), this);
    color_picker_action->setCheckable(true);
    auto *gradient_action = new QAction(gradient_tool_icon(), fxm_tr("OBSTitles.GradientTool"), this);
    gradient_action->setCheckable(true);
    tool_group_->addAction(selection_action);
    tool_group_->addAction(direct_selection_action);
    tool_group_->addAction(free_transform_action);
    tool_group_->addAction(shape_action);
    tool_group_->addAction(pen_action);
    tool_group_->addAction(text_action);
    tool_group_->addAction(image_action);
    tool_group_->addAction(color_picker_action);
    tool_group_->addAction(gradient_action);
    selection_button_->setDefaultAction(selection_action);
    direct_selection_button_->setDefaultAction(direct_selection_action);
    free_transform_button_->setDefaultAction(free_transform_action);
    shape_button_->setDefaultAction(shape_action);
    pen_button_->setDefaultAction(pen_action);
    text_button_->setDefaultAction(text_action);
    image_button_->setDefaultAction(image_action);
    color_picker_button_->setDefaultAction(color_picker_action);
    gradient_button_->setDefaultAction(gradient_action);

    free_transform_menu_ = new QMenu(free_transform_button_);
    free_transform_button_->setMenu(free_transform_menu_);
    const QStringList transform_modes = {
        fxm_tr("OBSTitles.FreeTransformMode"),
        fxm_tr("OBSTitles.PerspectiveDistortMode"),
        fxm_tr("OBSTitles.FreeDistortMode")
    };
    for (int mode = 0; mode < transform_modes.size(); ++mode) {
        QAction *mode_action = free_transform_menu_->addAction(transform_modes[mode]);
        connect(mode_action, &QAction::triggered, this, [this, mode]() {
            free_transform_mode_ = mode;
            if (free_transform_button_ && free_transform_button_->defaultAction())
                free_transform_button_->defaultAction()->setChecked(true);
            emit free_transform_tool_requested(mode);
        });
    }
    shape_menu_ = new QMenu(shape_button_);
    shape_button_->setMenu(shape_menu_);
    rebuild_shape_menu();
    text_menu_ = new QMenu(text_button_);
    text_button_->setMenu(text_menu_);
    rebuild_text_menu();

    connect(selection_action, &QAction::triggered, this, [this]() {
        emit selection_tool_requested();
    });
    connect(direct_selection_action, &QAction::triggered, this, [this]() {
        emit direct_selection_tool_requested();
    });
    connect(free_transform_action, &QAction::triggered, this, [this]() {
        emit free_transform_tool_requested(free_transform_mode_);
    });
    connect(shape_action, &QAction::triggered, this, [this]() {
        emit shape_tool_requested(selected_shape_);
    });
    connect(pen_action, &QAction::triggered, this, [this]() {
        emit pen_tool_requested();
    });
    connect(text_action, &QAction::triggered, this, [this]() {
        emit text_tool_requested(selected_text_layer_type_);
    });
    connect(image_action, &QAction::triggered, this, [this]() {
        emit image_tool_requested();
    });
    connect(color_picker_action, &QAction::triggered, this, [this]() {
        emit color_picker_tool_requested();
    });
    connect(gradient_action, &QAction::triggered, this, [this]() {
        emit gradient_tool_requested();
    });

    foreground_background_swatch_ = new ForegroundBackgroundSwatch(this);
    swatch_layout->addWidget(foreground_background_swatch_, 0, Qt::AlignHCenter);
    connect(foreground_background_swatch_, &ForegroundBackgroundSwatch::foreground_requested,
            this, &ToolsSidebar::foreground_color_requested);
    connect(foreground_background_swatch_, &ForegroundBackgroundSwatch::background_requested,
            this, &ToolsSidebar::background_color_requested);
    connect(foreground_background_swatch_, &ForegroundBackgroundSwatch::swap_requested,
            this, &ToolsSidebar::foreground_background_swap_requested);
}

void ToolsSidebar::set_selected_shape(ShapeType shape_type)
{
    /* The old line-shaped primitive is intentionally not a selectable shape.
     * Keep a defensive normalization for stale shortcuts/settings until the
     * dedicated line tool is introduced. */
    if (shape_type == ShapeType::Line || shape_type == ShapeType::Path)
        shape_type = ShapeType::Rectangle;
    selected_shape_ = shape_type;
    const QIcon icon = shape_tool_icon(shape_type);
    if (shape_button_) {
        shape_button_->setIcon(icon);
        if (auto *action = shape_button_->defaultAction()) {
            action->setIcon(icon);
            action->setText(shape_display_name(shape_type));
            action->setChecked(true);
        }
        shape_button_->setToolTip(fxm_tr("OBSTitles.ShapeToolSelectedTooltip").arg(shape_display_name(shape_type)) +
                                  (shape_type == ShapeType::Ellipse ? QStringLiteral(" (L)") : QStringLiteral(" (M)")));
        shape_button_->setChecked(true);
    }
}

void ToolsSidebar::rebuild_shape_menu()
{
    if (!shape_menu_) return;
    shape_menu_->clear();
    const std::vector<ShapeType> shapes = {
        ShapeType::Rectangle,
        ShapeType::RoundedRectangle,
        ShapeType::Ellipse,
        ShapeType::Triangle,
        ShapeType::Star,
        ShapeType::Polygon,
        ShapeType::Diamond,
    };
    for (ShapeType shape : shapes) {
        QAction *action = shape_menu_->addAction(shape_tool_icon(shape), shape_display_name(shape));
        connect(action, &QAction::triggered, this, [this, shape]() {
            set_selected_shape(shape);
            emit shape_tool_requested(shape);
        });
    }
}


void ToolsSidebar::set_selected_text_layer_type(LayerType type)
{
    if (type != LayerType::Text && type != LayerType::Clock && type != LayerType::Ticker)
        type = LayerType::Text;
    selected_text_layer_type_ = type;
    const QIcon icon = text_tool_icon(type);
    const QString name = text_tool_display_name(type);
    if (text_button_) {
        text_button_->setIcon(icon);
        if (auto *action = text_button_->defaultAction()) {
            action->setIcon(icon);
            action->setText(name);
            action->setChecked(true);
        }
        text_button_->setToolTip(fxm_tr("OBSTitles.TextToolSelectedTooltip").arg(name) + QStringLiteral(" (T)"));
        text_button_->setChecked(true);
    }
}

void ToolsSidebar::rebuild_text_menu()
{
    if (!text_menu_) return;
    text_menu_->clear();
    const std::vector<LayerType> types = {LayerType::Text, LayerType::Clock, LayerType::Ticker};
    for (LayerType type : types) {
        QAction *action = text_menu_->addAction(text_tool_icon(type), text_tool_display_name(type));
        connect(action, &QAction::triggered, this, [this, type]() {
            set_selected_text_layer_type(type);
            emit text_tool_requested(type);
        });
    }
}



void ToolsSidebar::activate_selection_tool()
{
    if (selection_button_ && selection_button_->defaultAction())
        selection_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_direct_selection_tool()
{
    if (direct_selection_button_ && direct_selection_button_->defaultAction())
        direct_selection_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_free_transform_tool(int mode)
{
    free_transform_mode_ = std::clamp(mode, 0, 2);
    if (free_transform_button_ && free_transform_button_->defaultAction())
        free_transform_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_shape_tool(ShapeType shape_type)
{
    set_selected_shape(shape_type);
    if (shape_button_ && shape_button_->defaultAction())
        shape_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_pen_tool()
{
    if (pen_button_ && pen_button_->defaultAction())
        pen_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_text_tool(LayerType type)
{
    set_selected_text_layer_type(type);
    if (text_button_ && text_button_->defaultAction())
        text_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_image_tool()
{
    if (image_button_ && image_button_->defaultAction())
        image_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_color_picker_tool()
{
    if (color_picker_button_ && color_picker_button_->defaultAction())
        color_picker_button_->defaultAction()->trigger();
}

void ToolsSidebar::activate_gradient_tool()
{
    if (gradient_button_ && gradient_button_->defaultAction())
        gradient_button_->defaultAction()->trigger();
}


void ToolsSidebar::set_foreground_color(const QColor &color)
{
    if (foreground_background_swatch_) foreground_background_swatch_->set_foreground_color(color);
}

void ToolsSidebar::set_background_color(const QColor &color)
{
    if (foreground_background_swatch_) foreground_background_swatch_->set_background_color(color);
}


void ToolsSidebar::set_foreground_gradient(const QColor &start, const QColor &end, int gradient_type)
{
    if (foreground_background_swatch_) foreground_background_swatch_->set_foreground_gradient(start, end, gradient_type);
}

void ToolsSidebar::set_background_gradient(const QColor &start, const QColor &end, int gradient_type)
{
    if (foreground_background_swatch_) foreground_background_swatch_->set_background_gradient(start, end, gradient_type);
}

void ToolsSidebar::set_foreground_mixed(bool mixed)
{
    if (foreground_background_swatch_) foreground_background_swatch_->set_foreground_mixed(mixed);
}

void ToolsSidebar::set_background_mixed(bool mixed)
{
    if (foreground_background_swatch_) foreground_background_swatch_->set_background_mixed(mixed);
}
