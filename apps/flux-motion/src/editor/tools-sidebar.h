#pragma once

#include "title-data.h"
#include "title-rich-text.h"

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QListWidget>
#include <QToolButton>
#include <QActionGroup>
#include <QButtonGroup>
#include <QMenu>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QTextEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QColor>
#include <QPixmap>
#include <QLinearGradient>
#include <QElapsedTimer>
#include <memory>
#include <string>
#include <vector>
#include <set>

class QEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QResizeEvent;
class QPaintEvent;
class QPainter;
class QScrollBar;

class ForegroundBackgroundSwatch : public QWidget {
    Q_OBJECT
public:
    explicit ForegroundBackgroundSwatch(QWidget *parent = nullptr);
    void set_foreground_color(const QColor &color);
    void set_background_color(const QColor &color);
    void set_foreground_gradient(const QColor &start, const QColor &end, int gradient_type = 0);
    void set_background_gradient(const QColor &start, const QColor &end, int gradient_type = 0);
    void set_foreground_mixed(bool mixed);
    void set_background_mixed(bool mixed);
    QColor foreground_color() const { return foreground_color_; }
    QColor background_color() const { return background_color_; }

signals:
    void foreground_requested();
    void background_requested();
    void swap_requested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect foreground_rect() const;
    QRect background_rect() const;
    QRect swap_rect() const;
    struct SwatchFill {
        int type = 0; // 0=solid, 1=gradient
        QColor color = QColor(34, 34, 34);
        QColor start = QColor(34, 34, 34);
        QColor end = QColor(255, 255, 255);
        int gradient_type = 0;
    };
    SwatchFill foreground_fill_;
    SwatchFill background_fill_;
    QColor foreground_color_ = QColor(34, 34, 34);
    QColor background_color_ = QColor(255, 255, 255);
    bool foreground_mixed_ = false;
    bool background_mixed_ = false;
};
/* ══════════════════════════════════════════════════════════════════
 *  ToolsSidebar – Photoshop-style icon-only tool palette
 * ══════════════════════════════════════════════════════════════════ */
class ToolsSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ToolsSidebar(QWidget *parent = nullptr);
    void set_selected_shape(ShapeType shape_type);
    ShapeType selected_shape() const { return selected_shape_; }
    void set_selected_text_layer_type(LayerType type);
    LayerType selected_text_layer_type() const { return selected_text_layer_type_; }
    void set_foreground_color(const QColor &color);
    void set_background_color(const QColor &color);
    void set_foreground_gradient(const QColor &start, const QColor &end, int gradient_type = 0);
    void set_background_gradient(const QColor &start, const QColor &end, int gradient_type = 0);
    void set_foreground_mixed(bool mixed);
    void set_background_mixed(bool mixed);

    void activate_selection_tool();
    void activate_direct_selection_tool();
    void activate_free_transform_tool(int mode = 0);
    void activate_shape_tool(ShapeType shape_type);
    void activate_pen_tool();
    void activate_text_tool(LayerType type = LayerType::Text);
    void activate_image_tool();
    void activate_color_picker_tool();
    void activate_gradient_tool();

signals:
    void selection_tool_requested();
    void direct_selection_tool_requested();
    void free_transform_tool_requested(int mode);
    void shape_tool_requested(ShapeType shape_type);
    void pen_tool_requested();
    void text_tool_requested(LayerType type);
    void image_tool_requested();
    void color_picker_tool_requested();
    void gradient_tool_requested();
    void foreground_color_requested();
    void background_color_requested();
    void foreground_background_swap_requested();

private:
    void rebuild_shape_menu();
    void rebuild_text_menu();
    QToolButton *selection_button_ = nullptr;
    QToolButton *direct_selection_button_ = nullptr;
    QToolButton *free_transform_button_ = nullptr;
    QToolButton *shape_button_ = nullptr;
    QToolButton *pen_button_ = nullptr;
    QToolButton *text_button_ = nullptr;
    QToolButton *image_button_ = nullptr;
    QToolButton *color_picker_button_ = nullptr;
    QToolButton *gradient_button_ = nullptr;
    QActionGroup *tool_group_ = nullptr;
    QMenu *shape_menu_ = nullptr;
    QMenu *free_transform_menu_ = nullptr;
    int free_transform_mode_ = 0;
    QMenu *text_menu_ = nullptr;
    ForegroundBackgroundSwatch *foreground_background_swatch_ = nullptr;
    ShapeType selected_shape_ = ShapeType::Rectangle;
    LayerType selected_text_layer_type_ = LayerType::Text;
};


