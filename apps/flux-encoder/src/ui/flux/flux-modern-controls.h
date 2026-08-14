#pragma once

#include <QCheckBox>
#include <QPointer>
#include <QToolButton>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;

// Reusable controls aligned with the Flux Motion widget language.
// They intentionally use the active QPalette rather than an application-wide QStyle.
class FluxSwitch final : public QCheckBox {
    Q_OBJECT
public:
    explicit FluxSwitch(QWidget *parent = nullptr);
    explicit FluxSwitch(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
    bool hitButton(const QPoint &position) const override;
};

class FluxCaretButton final : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(int caretState READ caretState WRITE setCaretState NOTIFY caretStateChanged)
public:
    explicit FluxCaretButton(QWidget *parent = nullptr);
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

class FluxCollapsiblePanel final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)
public:
    explicit FluxCollapsiblePanel(const QString &title, QWidget *content, QWidget *parent = nullptr);
    QString title() const { return title_; }
    void setTitle(const QString &title);
    QWidget *contentWidget() const { return content_; }
    bool isExpanded() const { return expanded_; }
    void addHeaderWidget(QWidget *widget);
    void setOrderPersistenceEnabled(bool enabled);
    void setPersistenceKey(const QString &group, const QString &key);
public slots:
    void setExpanded(bool expanded);
    void toggleExpanded();
signals:
    void expandedChanged(bool expanded);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    class Header;
    QString title_;
    QPointer<QWidget> content_;
    Header *header_ = nullptr;
    QWidget *body_ = nullptr;
    QHBoxLayout *headerActions_ = nullptr;
    bool expanded_ = true;
    bool orderPersistenceEnabled_ = true;
    QString persistenceGroup_;
    QString persistenceKey_;
};

FluxCollapsiblePanel *flux_add_panel_section(QVBoxLayout *layout, QWidget *section,
                                            const QString &title = QString(),
                                            QWidget *headerWidget = nullptr);
