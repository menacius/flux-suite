#pragma once

#include <QCheckBox>
#include <QIcon>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <vector>

class QAbstractButton;
class QAbstractSpinBox;
class QColor;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QHBoxLayout;
class QPalette;
class QResizeEvent;
class QVBoxLayout;

class NumericDragLabel : public QLabel {
public:
    NumericDragLabel(const QString &text, QWidget *field,
                     QWidget *parent = nullptr,
                     std::function<void()> drag_started = {},
                     std::function<void()> drag_finished = {});
    ~NumericDragLabel() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    static QAbstractSpinBox *find_spin_box(QWidget *field);
    bool can_drag() const;
    double spin_value() const;
    double spin_step() const;
    void set_spin_value(double value);
    void finish_drag();

    QAbstractSpinBox *spin_box_ = nullptr;
    std::function<void()> drag_started_;
    std::function<void()> drag_finished_;
    bool dragging_ = false;
    double drag_start_x_ = 0.0;
    double drag_start_value_ = 0.0;
};

/*
 * Compact, host-theme-aware controls used throughout the editor. They use
 * the active QWidget palette instead of introducing an application-wide
 * QStyle, so the OBS theme remains the single source of truth.
 */
class FxmSwitch final : public QCheckBox {
    Q_OBJECT
public:
    explicit FxmSwitch(QWidget *parent = nullptr);
    explicit FxmSwitch(const QString &text, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool hitButton(const QPoint &position) const override;
};


/* Shared compact caret used by inspector panels and the layer list. State 0
 * points right, state 1 points down with a small keyframe marker, and state 2
 * points down. */
class FxmCaretButton final : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(int caretState READ caretState WRITE setCaretState NOTIFY caretStateChanged)
public:
    explicit FxmCaretButton(QWidget *parent = nullptr);

    int caretState() const { return state_; }
    QSize sizeHint() const override;

public slots:
    void setCaretState(int state);

signals:
    void caretStateChanged(int state);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int state_ = 0;
};

class FxmAngleControl final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit FxmAngleControl(QWidget *parent = nullptr);

    double value() const { return value_; }
    void setRange(double minimum, double maximum);
    void setSingleStep(double step);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setValue(double value);

signals:
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setValueFromPosition(const QPointF &position);
    double nearestEquivalent(double normalized) const;

    double minimum_ = -360.0;
    double maximum_ = 360.0;
    double value_ = 0.0;
    double step_ = 1.0;
    bool dragging_ = false;
};

/*
 * A compact After Effects-like inspector section. The header contains a drag
 * handle on the left and a collapse caret on the right. Sections in the same
 * parent QVBoxLayout can be reordered by dragging the handle.
 */
class FxmCollapsiblePanel final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)
public:
    explicit FxmCollapsiblePanel(const QString &title, QWidget *content,
                                 QWidget *parent = nullptr);

    QString title() const { return title_; }
    void setTitle(const QString &title);
    QWidget *contentWidget() const { return content_; }
    bool isExpanded() const { return expanded_; }
    void addHeaderWidget(QWidget *widget);
    void addHeaderLeadingWidget(QWidget *widget);
    void setPersistenceKey(const QString &group, const QString &key);
    void setOrderPersistenceEnabled(bool enabled);
    QString persistenceKey() const { return persistence_key_; }

public slots:
    void setExpanded(bool expanded);
    void toggleExpanded();

signals:
    void expandedChanged(bool expanded);
    void orderChanged();
    void activated();

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    class Header;
    friend class Header;

    void beginDrag();
    bool canAcceptPanel(const FxmCollapsiblePanel *panel) const;
    void setDropIndicator(int edge);
    void movePanelHere(FxmCollapsiblePanel *source, bool before);
    void applyExpandedState();
    void saveExpandedState() const;

    QString title_;
    QPointer<QWidget> content_;
    Header *header_ = nullptr;
    QWidget *body_ = nullptr;
    QWidget *drop_indicator_widget_ = nullptr;
    QHBoxLayout *header_leading_ = nullptr;
    QHBoxLayout *header_actions_ = nullptr;
    bool expanded_ = true;
    bool persist_order_ = true;
    QString persistence_group_;
    QString persistence_key_;
    int drop_indicator_edge_ = 0; // -1 top, +1 bottom, 0 none
};

/* Shared compact inspector styling derived from the Transform panel. Use these
 * for every editor inspector/effects panel so equivalent controls keep the same
 * 20 px height, border, padding, spin-button width and theme colors. */
QString fxm_transform_panel_control_style(const QPalette &palette);
QString fxm_transform_panel_button_style(const QPalette &palette);
QIcon fxm_panel_defaults_icon(const QPalette &palette);
void fxm_apply_transform_panel_widget_style(QWidget *root);

/* Canonical keyframe control used by every inspector.  The shared wrapper
 * keeps the previous/diamond/next order, the compact 18 px metrics and the
 * enabled state in sync with the authored keyframes for that property. */
using FxmKeyframeTimesProvider = std::function<std::vector<double>()>;
using FxmCurrentTimeProvider = std::function<double()>;
using FxmKeyframeNavigateCallback = std::function<void(double)>;

void fxm_style_keyframe_button(QAbstractButton *button);
QColor fxm_keyframe_color();
QIcon fxm_keyframe_diamond_icon(bool active, bool outlined = false);
QWidget *fxm_make_keyframe_controls(
    QAbstractButton *diamond, QWidget *parent,
    FxmKeyframeTimesProvider times_provider,
    FxmCurrentTimeProvider current_time_provider,
    FxmKeyframeNavigateCallback navigate_callback);
void fxm_refresh_keyframe_navigation(QAbstractButton *diamond);

/* Applies the 3D Camera inspector's field metrics to ordinary editor input
 * fields without flattening multiline document editors or bespoke buttons. */
void fxm_apply_editor_field_style(QWidget *widget);

/* Creates an AE-like angle field: a circular direction widget plus the exact
 * numeric spin box. The spin box remains the authoritative value/control. */
QWidget *fxm_make_angle_field(QDoubleSpinBox *spin_box, QWidget *parent = nullptr,
                              FxmAngleControl **angle_control = nullptr);

/* Wraps a section in the common collapsible/reorderable inspector panel. The
 * title is inferred from fxmPanelTitle or from a QGroupBox title when omitted. */
FxmCollapsiblePanel *fxm_add_panel_section(QVBoxLayout *layout, QWidget *section,
                                           const QString &title = QString(),
                                           QWidget *header_widget = nullptr,
                                           int insert_index = -1);
FxmCollapsiblePanel *fxm_panel_for_content(QWidget *section);
