#pragma once

#include <QAbstractItemView>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTimer>
#include <functional>

class LiveTextCueField : public QPlainTextEdit {
public:
    explicit LiveTextCueField(const QString &text, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
        setPlainText(text);
        update_tooltip(text);
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setTabChangesFocus(true);
        /* Cue text is an operator-editable control, not a type-to-select
         * target.  Only a direct pointer click may begin an editing session;
         * OBS hotkeys and QTableView keyboard search must leave it untouched. */
        setFocusPolicy(Qt::ClickFocus);
        document()->setDocumentMargin(0.0);
        setFrameShape(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(this, &QPlainTextEdit::textChanged, this, [this]() {
            update_tooltip(toPlainText());
        });
    }

    void update_tooltip(const QString &text)
    {
        setToolTip(text);
        if (viewport())
            viewport()->setToolTip(text);
    }

    std::function<void(const QString &)> editing_finished;
    std::function<void(bool)> focus_changed;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        edit_focus_armed_ = true;
        QPlainTextEdit::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // Qt delivers a separate double-click event, not a second press. Arm
        // editing before focus/word selection so the hotkey guard cannot
        // reject typing or a queued focus check return focus to the table.
        if (event && event->button() == Qt::LeftButton) {
            edit_focus_armed_ = true;
            setFocus(Qt::MouseFocusReason);
        }
        QPlainTextEdit::mouseDoubleClickEvent(event);
    }

    void focusInEvent(QFocusEvent *event) override
    {
        /* A QTableView may try to transfer focus to the widget in its current
         * cell when it receives a printable key.  A live-cue value must only
         * become editable after an explicit pointer click or keyboard focus
         * traversal; otherwise an operator hotkey (notably a numpad cue key)
         * can unexpectedly overwrite the cue text. */
        QPlainTextEdit::focusInEvent(event);
        if (!edit_focus_armed_) {
            const QPointer<LiveTextCueField> guard(this);
            QTimer::singleShot(0, this, [guard]() {
                if (!guard || guard->edit_focus_armed_ || !guard->hasFocus())
                    return;
                QWidget *ancestor = guard->parentWidget();
                while (ancestor &&
                       !qobject_cast<QAbstractItemView *>(ancestor))
                    ancestor = ancestor->parentWidget();
                if (ancestor)
                    ancestor->setFocus(Qt::OtherFocusReason);
                else
                    guard->clearFocus();
            });
        }
        if (focus_changed)
            focus_changed(true);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (!edit_focus_armed_) {
            if (event)
                event->ignore();
            return;
        }
        if (event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (editing_finished)
                editing_finished(toPlainText());
            focusNextChild();
            event->accept();
            return;
        }

        // A literal newline is intentionally available only through Shift+Enter.
        QPlainTextEdit::keyPressEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        edit_focus_armed_ = false;
        QPlainTextEdit::focusOutEvent(event);
        if (editing_finished)
            editing_finished(toPlainText());
        if (focus_changed) {
            const auto callback = focus_changed;
            QTimer::singleShot(0, qApp, [callback]() { callback(false); });
        }
    }

private:
    bool edit_focus_armed_ = false;
};
