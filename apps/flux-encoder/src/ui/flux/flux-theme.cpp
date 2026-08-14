#include "ui/flux/flux-theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace flux::ui {

void applyFluxEncoderTheme(QApplication &app)
{
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);

    // Flux Motion palette with Flux Encoder's green brand accent.
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
    palette.setColor(QPalette::Link, QColor(0x42, 0xa2, 0x74));
    palette.setColor(QPalette::Highlight, QColor(0x42, 0xa2, 0x74));
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
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x31, 0x5e, 0x48));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0xa0, 0xa0, 0xa0));
    app.setPalette(palette);

    app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog { background: #232323; }
QMenuBar, QMenu, QToolBar { background: #292929; border-color: #474747; }
QMenuBar::item:selected, QMenu::item:selected { background: #42a274; color: white; }
QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; border: 1px solid #474747; }
QDockWidget::title { background: #2b2b2b; padding: 5px 7px; }
QTabWidget::pane { border: 1px solid #474747; }
QTabBar::tab { background: #2b2b2b; border: 1px solid #474747; padding: 4px 9px; }
QTabBar::tab:selected { background: #3a3a3a; border-top: 2px solid #42a274; }
QPushButton, QToolButton { background: #333333; border: 1px solid #505050; padding: 4px 8px; }
QPushButton:hover, QToolButton:hover { background: #404040; border-color: #686868; }
QPushButton:pressed, QToolButton:pressed { background: #1f1f1f; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #1b1b1b; border: 1px solid #505050; padding: 3px 5px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus { border-color: #42a274; }
QHeaderView::section { background: #303030; border: 0; border-right: 1px solid #474747; padding: 4px; }
QTreeWidget, QTableWidget, QPlainTextEdit { background: #1b1b1b; alternate-background-color: #2b2b2b; }
QTreeWidget::item:selected, QTableWidget::item:selected { background: #42a274; color: white; }
QScrollBar:vertical { background: #242424; border: 0; width: 7px; margin: 0; }
QScrollBar:horizontal { background: #242424; border: 0; height: 7px; margin: 0; }
QScrollBar::handle:vertical { background: #555555; min-height: 24px; border-radius: 3px; }
QScrollBar::handle:horizontal { background: #555555; min-width: 24px; border-radius: 3px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #686868; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolTip { color: #f0f0f0; background: #323232; border: 1px solid #606060; }
QStatusBar { border-top: 1px solid #474747; }
QMainWindow::separator, QDockWidget::separator { background: #181818; width: 3px; height: 3px; }
QMainWindow::separator:hover { background: #42a274; }

QToolBar#FluxEncoderWorkspaceToolbar { background: #292929; border: 0; border-bottom: 1px solid #474747; padding: 0; spacing: 2px; }
QComboBox#FluxEncoderWorkspaceSelector { min-height: 22px; max-height: 22px; background: #292929; border: 0; padding-left: 8px; }
QToolButton#FluxEncoderWorkspaceMenuButton { min-height: 22px; max-height: 22px; min-width: 24px; border: 0; background: transparent; padding: 0; }
QToolButton#FluxEncoderWorkspaceMenuButton:hover { background: #404040; }
QWidget#FluxEncoderDockTitleBar { background: #2b2b2b; border-bottom: 1px solid #474747; }
QLabel#FluxEncoderDockTitle { color: #d6d6d6; font-weight: 600; }
QToolButton#FluxEncoderDockMenuButton { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; border: 0; background: transparent; padding: 0; }
QToolButton#FluxEncoderDockMenuButton:hover { background: #404040; }
QLabel#PanelTitle { background: #2b2b2b; border-bottom: 1px solid #474747; color: #d6d6d6; }
QLabel#EncodingPreview, QLabel#ExportPreview { background: #181818; border: 1px solid #474747; color: #8a8a8a; }
QSplitter::handle { background: #181818; width: 4px; height: 4px; }
QLabel#ExportOutputLink { color: #42a274; text-decoration: underline; }
QLabel#ExportSummary { background: #1b1b1b; border: 1px solid #474747; padding: 8px; color: #d6d6d6; }
QLabel#EstimatedFileSize { font-weight: 600; padding: 6px; }
QLabel#ExportSettingsHint { color: #8a8a8a; }
QDialog#FluxEncoderExportSettings { background: #232323; }
QDialog#FluxEncoderExportSettings QSplitter::handle { background: #181818; width: 3px; }
QWidget#ExportPreviewWorkspace { background: #1b1b1b; }
QWidget#ExportPreviewToolbar, QWidget#ExportSettingsHeader, QWidget#ExportSettingsFooter { background: #292929; }
QWidget#ExportPreviewToolbar { border-bottom: 1px solid #474747; }
QWidget#ExportTimelinePanel { background: #1b1b1b; border-top: 1px solid #474747; }
QWidget#ExportSettingsInspector { background: #292929; border-left: 1px solid #474747; }
QScrollArea#ExportSettingsHeaderScroll { background: #292929; border: 0; }
QLabel#ExportInspectorTitle { font-weight: 700; color: #f0f0f0; padding: 2px 0 5px 0; }
QLabel#ExportCurrentTimecode { color: #42a274; font-weight: 700; }
QLabel#ExportDurationTimecode { color: #d6d6d6; }
QDialog#FluxEncoderExportSettings QTabWidget::pane { border: 0; border-top: 1px solid #474747; }
QDialog#FluxEncoderExportSettings QTabBar::tab { background: transparent; border: 0; border-bottom: 2px solid transparent; padding: 7px 9px; }
QDialog#FluxEncoderExportSettings QTabBar::tab:selected { color: white; border-bottom-color: #42a274; background: #333333; }
QDialog#FluxEncoderExportSettings QTabBar::tab:hover { background: #404040; }
QDialog#FluxEncoderExportSettings QScrollArea, QDialog#FluxEncoderExportSettings QScrollArea > QWidget > QWidget { background: transparent; }
QDialog#FluxEncoderExportSettings QComboBox, QDialog#FluxEncoderExportSettings QLineEdit,
QDialog#FluxEncoderExportSettings QSpinBox, QDialog#FluxEncoderExportSettings QDoubleSpinBox { min-height: 22px; background: #1b1b1b; border-color: #505050; }
QDialog#FluxEncoderExportSettings QPushButton, QDialog#FluxEncoderExportSettings QToolButton { min-height: 22px; }
QDialog#FluxEncoderExportSettings QDialogButtonBox QPushButton { min-width: 62px; }
QDialog#FluxEncoderExportSettings QDialogButtonBox QPushButton:default { background: #42a274; border-color: #65bd92; color: white; }
QWidget#FluxEncoderResolutionControl { background: transparent; }
QWidget#FluxEncoderResolutionControl QSpinBox { min-width: 120px; }
QPushButton#FluxEncoderMatchSourceButton { min-width: 110px; font-weight: 600; }
QToolButton#FluxEncoderDimensionLink { min-width: 28px; max-width: 28px; border: 0; background: transparent; font-size: 17px; }
QToolButton#FluxEncoderDimensionLink:checked { color: #42a274; }
QDialog#MetadataExportDialog { background: #292929; }
QDialog#MetadataExportDialog QGroupBox { border: 1px solid #505050; border-radius: 2px; margin-top: 12px; padding: 12px 10px 10px 10px; font-weight: 600; }
QDialog#MetadataExportDialog QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #f0f0f0; }
QDialog#MetadataExportDialog QTreeWidget { background: #1b1b1b; border: 1px solid #505050; }
QDialog#MetadataExportDialog QTreeWidget::item { min-height: 25px; border-bottom: 1px solid #3d3d3d; }
QDialog#MetadataExportDialog QTreeWidget::item:selected { background: #42a274; color: white; }
)"));
}

} // namespace flux::ui
