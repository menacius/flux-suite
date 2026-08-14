#include "editor-theme.h"
#include "title-assets.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

namespace {

constexpr const char *kSettingsOrg = "FluxMotion";
constexpr const char *kSettingsApp = "Dock";
constexpr const char *kAppearanceGroup = "Appearance";
constexpr const char *kEditorThemeKey = "editorTheme";

QPalette flux_motion_palette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x23, 0x23, 0x23));
    palette.setColor(QPalette::WindowText, QColor(0xd6, 0xd6, 0xd6));
    palette.setColor(QPalette::Base, QColor(0x1b, 0x1b, 0x1b));
    palette.setColor(QPalette::AlternateBase, QColor(0x2b, 0x2b, 0x2b));
    palette.setColor(QPalette::ToolTipBase, QColor(0x32, 0x32, 0x32));
    palette.setColor(QPalette::ToolTipText, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::Text, QColor(0xd6, 0xd6, 0xd6));
    palette.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
    palette.setColor(QPalette::ButtonText, QColor(0xd6, 0xd6, 0xd6));
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Link, QColor(0x78, 0x38, 0xf5));
    palette.setColor(QPalette::Highlight, QColor(0x78, 0x38, 0xf5));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Light, QColor(0x4a, 0x4a, 0x4a));
    palette.setColor(QPalette::Midlight, QColor(0x3d, 0x3d, 0x3d));
    palette.setColor(QPalette::Mid, QColor(0x47, 0x47, 0x47));
    palette.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x18));
    palette.setColor(QPalette::Shadow, QColor(0x0f, 0x0f, 0x0f));
    palette.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x78, 0x78, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x78, 0x78, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x78, 0x78, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x4a, 0x32, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0xa0, 0xa0, 0xa0));
    return palette;
}

QString flux_motion_stylesheet()
{
    return QStringLiteral(R"(
QMainWindow, QDialog { background: #232323; }
QMenuBar, QMenu, QToolBar { background: #292929; border-color: #474747; }
QMenuBar::item:selected, QMenu::item:selected { background: #7838f5; color: white; }
QMainWindow::separator { background: #181818; width: 4px; height: 4px; }
QMainWindow::separator:hover { background: #7838f5; }
QDockWidget { border: 1px solid #474747; border-radius: 4px; }
QDockWidget::title {
    background: #2b2b2b; border-bottom: 1px solid #474747;
    padding: 4px 7px; font-weight: 600;
}
QFrame#FluxMotionProjectBrowser {
    background: #23252d; border: 1px solid #474747; border-radius: 4px;
}
QListWidget#FluxMotionProjectItems {
    background: transparent; border: none; outline: none; padding: 3px;
}
QListWidget#FluxMotionProjectItems::item {
    min-height: 26px; padding: 2px 5px; border-radius: 3px;
}
QListWidget#FluxMotionProjectItems::item:hover { background: #34363f; }
QListWidget#FluxMotionProjectItems::item:selected { background: #7838f5; color: white; }
QLabel#FluxMotionProjectEmpty { color: #8d8d8d; border: none; }
QToolBar#FluxMotionDockToolbar {
    background: #292929; border: none; border-top: 1px solid #474747;
    padding: 2px 3px; spacing: 1px;
}
QToolBar#FluxMotionDockToolbar QToolButton {
    background: transparent; border: 1px solid transparent;
    border-radius: 3px; padding: 3px;
}
QToolBar#FluxMotionDockToolbar QToolButton:hover {
    background: #404040; border-color: #565656;
}
QToolBar#FluxMotionDockToolbar QToolButton:pressed { background: #1f1f1f; }
QTabWidget::pane { border: 1px solid #474747; }
QTabBar::tab { background: #2b2b2b; border: 1px solid #474747; padding: 4px 9px; }
QTabBar::tab:selected { background: #3a3a3a; border-top: 2px solid #7838f5; }
QPushButton, QToolButton { background: #333333; border: 1px solid #505050; padding: 4px 8px; }
QPushButton:hover, QToolButton:hover { background: #404040; border-color: #686868; }
QPushButton:pressed, QToolButton:pressed { background: #1f1f1f; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #1b1b1b; border: 1px solid #505050; padding: 3px 5px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus { border-color: #7838f5; }
QHeaderView::section { background: #303030; border: 0; border-right: 1px solid #474747; padding: 4px; }
QScrollBar:vertical { background: #242424; border: 0; width: 7px; margin: 0; }
QScrollBar:horizontal { background: #242424; border: 0; height: 7px; margin: 0; }
QScrollBar::handle:vertical { background: #555555; min-height: 24px; border-radius: 3px; }
QScrollBar::handle:horizontal { background: #555555; min-width: 24px; border-radius: 3px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #686868; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolTip { color: #f0f0f0; background: #323232; border: 1px solid #606060; }
)");
}

} // namespace

namespace fxm::editor {

QString editor_theme_id(EditorTheme theme)
{
    Q_UNUSED(theme);
    return QStringLiteral("flux-motion");
}

EditorTheme editor_theme_from_id(const QString &id)
{
    Q_UNUSED(id);
    // Every legacy profile id migrates to the single supported theme.
    return EditorTheme::FluxMotion;
}

EditorTheme current_editor_theme()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kAppearanceGroup));
    const QString id = settings.value(QString::fromUtf8(kEditorThemeKey),
                                      QStringLiteral("flux-motion")).toString();
    if (id != QStringLiteral("flux-motion"))
        settings.setValue(QString::fromUtf8(kEditorThemeKey),
                          QStringLiteral("flux-motion"));
    settings.endGroup();
    settings.sync();
    return editor_theme_from_id(id);
}

void apply_editor_theme(QApplication &application, EditorTheme theme)
{
    Q_UNUSED(theme);
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        application.setStyle(fusion);
    application.setFont(fxm_satoshi_ui_font());
    application.setPalette(flux_motion_palette());
    application.setStyleSheet(flux_motion_stylesheet());
}

void set_current_editor_theme(QApplication &application, EditorTheme theme)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kAppearanceGroup));
    settings.setValue(QString::fromUtf8(kEditorThemeKey), editor_theme_id(theme));
    settings.endGroup();
    settings.sync();
    apply_editor_theme(application, theme);
}

} // namespace fxm::editor
