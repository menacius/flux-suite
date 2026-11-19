#include "live-text-cue-field.h"
#include <QTableWidget>
#include <QTextCursor>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

static void double_click_word(LiveTextCueField &field)
{
    QTextCursor cursor = field.textCursor();
    cursor.setPosition(2);
    const QPoint point = field.cursorRect(cursor).center();
    QMouseEvent event(QEvent::MouseButtonDblClick, point,
        field.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(field.viewport(), &event);
    QMouseEvent release(QEvent::MouseButtonRelease, point,
        field.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(field.viewport(), &release);
    QApplication::processEvents();
}

static void type(LiveTextCueField &field, const QString &text)
{
    QKeyEvent event(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, text);
    QApplication::sendEvent(&field, &event);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTableWidget table(1, 1);
    auto *field = new LiveTextCueField(QStringLiteral("Hello world"), &table);
    table.setCellWidget(0, 0, field);
    table.setColumnWidth(0, 260);
    table.setRowHeight(0, 80);
    table.resize(360, 180);
    table.show(); table.activateWindow(); table.setFocus();
    QApplication::processEvents();
    // Qt can deliver a double-click directly without another mousePressEvent.
    // Word selection must grant editing focus just like a normal pointer click.
    double_click_word(*field);
    require(field->textCursor().selectedText() == QStringLiteral("Hello"), "Double click selects the word");
    require(field->hasFocus(), "Double click retains editing focus");
    type(*field, QStringLiteral("Changed"));
    require(field->toPlainText() == QStringLiteral("Changed world"), "Typing replaces the selected word");
    QString committed;
    field->editing_finished = [&](const QString &text) { committed = text; };
    table.setFocus(); QApplication::processEvents();
    require(committed == QStringLiteral("Changed world"), "Focus loss commits the edited cue");
    // Preserve the protection against accidental OBS hotkey/table-search input.
    type(*field, QStringLiteral("9"));
    require(field->toPlainText() == committed, "Typing without pointer edit focus cannot overwrite a cue");
    field->setFocus(Qt::OtherFocusReason); QApplication::processEvents();
    require(!field->hasFocus(), "Programmatic focus does not arm editing");
    field->setReadOnly(true);
    double_click_word(*field);
    const auto read_only_text = field->toPlainText();
    type(*field, QStringLiteral("No"));
    require(field->toPlainText() == read_only_text, "Managed read-only values remain protected");
    field->editing_finished = {};
    std::cout << "Cue word selection, editing, commit and hotkey/read-only guards passed\n";
}
