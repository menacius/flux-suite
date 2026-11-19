#pragma once

#include "fxm-caret-paint.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

class SceneMaskCaretButton final : public QToolButton {
public:
    explicit SceneMaskCaretButton(QWidget *parent) : QToolButton(parent)
    { fxm::ui::initialize_caret_button(*this); }
protected:
    void paintEvent(QPaintEvent *) override
    { fxm::ui::paint_caret_button(*this, isChecked() ? 2 : 0); }
};

// Collapsing hides only the controls. Keep widgets and active motion intact.
class SceneMaskPanel final : public QFrame {
public:
    SceneMaskPanel(const QString &title, const QString &state_key,
                   QWidget *parent = nullptr, const QString &settings_file = {})
        : QFrame(parent), title_(title), state_key_(state_key), settings_file_(settings_file)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 5, 6, 6);
        layout->setSpacing(3);
        auto *header = new QHBoxLayout;
        auto *label = new QLabel(title, this);
        label->setWordWrap(true);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        header->addWidget(label, 1);
        toggle_ = new SceneMaskCaretButton(this);
        toggle_->setObjectName(QStringLiteral("sceneMaskCollapseToggle"));
        toggle_->setAutoRaise(true);
        toggle_->setCheckable(true);
        header->addWidget(toggle_, 0, Qt::AlignRight);
        layout->addLayout(header);
        content_ = new QWidget(this);
        content_layout_ = new QVBoxLayout(content_);
        content_layout_->setContentsMargins(0, 0, 0, 0);
        content_layout_->setSpacing(3);
        layout->addWidget(content_);
        auto settings = open_settings();
        settings->beginGroup(QStringLiteral("SceneMaskDock/Collapsed"));
        const bool collapsed = settings->value(state_key_, false).toBool();
        toggle_->setChecked(!collapsed);
        apply_expanded(!collapsed);
        connect(toggle_, &QToolButton::toggled, this, [this](bool expanded) {
            apply_expanded(expanded);
            auto settings = open_settings();
            settings->beginGroup(QStringLiteral("SceneMaskDock/Collapsed"));
            settings->setValue(state_key_, !expanded);
            settings->sync();
        });
    }

    QVBoxLayout *content_layout() const { return content_layout_; }
    QWidget *content_widget() const { return content_; }
    QToolButton *toggle_button() const { return toggle_; }

private:
    std::unique_ptr<QSettings> open_settings() const
    {
        return settings_file_.isEmpty()
            ? std::make_unique<QSettings>(QStringLiteral("FluxMotion"), QStringLiteral("Dock"))
            : std::make_unique<QSettings>(settings_file_, QSettings::IniFormat);
    }

    void apply_expanded(bool expanded)
    {
        content_->setVisible(expanded);
        toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        const QString action = (expanded ? QStringLiteral("Collapse %1")
                                         : QStringLiteral("Expand %1")).arg(title_);
        toggle_->setAccessibleName(action);
        toggle_->setToolTip(action);
    }

    QString title_;
    QString state_key_;
    QString settings_file_;
    QWidget *content_ = nullptr;
    QVBoxLayout *content_layout_ = nullptr;
    QToolButton *toggle_ = nullptr;
};
