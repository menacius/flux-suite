#pragma once

#include <QString>

class QApplication;

namespace fxm::editor {

enum class EditorTheme {
    FluxMotion,
};

EditorTheme current_editor_theme();
QString editor_theme_id(EditorTheme theme);
EditorTheme editor_theme_from_id(const QString &id);

void apply_editor_theme(QApplication &application, EditorTheme theme);
void set_current_editor_theme(QApplication &application, EditorTheme theme);

} // namespace fxm::editor
