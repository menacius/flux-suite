#pragma once

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

// OBS exposes button properties as individual form rows. Keep those native
// buttons and their callbacks, but present this pair together in the Qt view.
class SourceSettingsButtonsLayout final : public QObject {
public:
    static constexpr const char *cue_tooltip = "Flux Motion: Cue this source.";
    static constexpr const char *uncue_tooltip = "Flux Motion: Uncue this source.";

    explicit SourceSettingsButtonsLayout(QObject *parent = nullptr) : QObject(parent)
    { if (qApp) qApp->installEventFilter(this); }
    ~SourceSettingsButtonsLayout() override
    { if (qApp) qApp->removeEventFilter(this); }

    static void arrange(QWidget *parent)
    {
        if (!parent) return;
        auto *form = qobject_cast<QFormLayout *>(parent->layout());
        if (!form) return;
        QPushButton *cue = nullptr, *uncue = nullptr;
        for (auto *button : parent->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (button->toolTip() == QString::fromUtf8(cue_tooltip)) cue = button;
            if (button->toolTip() == QString::fromUtf8(uncue_tooltip)) uncue = button;
        }
        if (!cue || !uncue) return;
        int cue_row = -1, uncue_row = -1;
        QFormLayout::ItemRole role;
        form->getWidgetPosition(cue, &cue_row, &role);
        form->getWidgetPosition(uncue, &uncue_row, &role);
        if (cue_row < 0 || uncue_row != cue_row + 1) return;

        const auto second = form->takeRow(uncue);
        const auto first = form->takeRow(cue);
        delete first.labelItem; delete first.fieldItem;
        delete second.labelItem; delete second.fieldItem;
        auto *row = new QWidget(parent);
        row->setObjectName(QStringLiteral("fluxMotionSettingsButtons"));
        auto *buttons = new QHBoxLayout(row);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->addWidget(cue);
        buttons->addWidget(uncue);
        buttons->addStretch();
        auto *label = new QLabel(QStringLiteral("Settings buttons"), parent);
        label->setBuddy(cue);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->insertRow(cue_row, label, row);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event && event->type() == QEvent::Show) {
            auto *button = qobject_cast<QPushButton *>(watched);
            if (button && (button->toolTip() == QString::fromUtf8(cue_tooltip) ||
                           button->toolTip() == QString::fromUtf8(uncue_tooltip))) {
                const QPointer<QWidget> parent = button->parentWidget();
                // Wait until OBS has finished adding both rows. The context
                // and QPointer also make property-view rebuilds safe.
                QTimer::singleShot(0, this, [parent] { arrange(parent); });
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
