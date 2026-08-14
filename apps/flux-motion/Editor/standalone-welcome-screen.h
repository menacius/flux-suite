#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QStackedWidget;

namespace fxm::editor {

struct WelcomeProject {
    QString id;
    QString name;
    QString detail;
    QString full_path;
    QDateTime last_opened;
    QByteArray thumbnail_png_base64;
};

/* The welcome surface is a child overlay owned by the standalone shell. It is
 * centered above the application workspace like a creative-suite start
 * dialog, but remains inside the main window rather than becoming another
 * native top-level window or a full-size stacked page. */
class StandaloneWelcomeScreen final : public QWidget {
public:
    explicit StandaloneWelcomeScreen(QWidget *parent = nullptr);

    void setProjects(std::vector<WelcomeProject> projects);
    void setCreateProjectHandler(std::function<void()> handler);
    void setOpenProjectHandler(std::function<void(const QString &)> handler);

    static void recordProjectOpened(const QString &project_path,
                                    const QString &project_name = {},
                                    const QByteArray &thumbnail_png_base64 = {});

private:
    enum class ProjectView { Recent, All };

    void rebuildProjectList();
    void showAllProjects();
    void showRecentProjects();
    void openSelectedProject();
    QStringList recentProjectPaths() const;

    std::vector<WelcomeProject> projects_;
    std::function<void()> create_project_handler_;
    std::function<void(const QString &)> open_project_handler_;
    ProjectView project_view_ = ProjectView::Recent;

    QLabel *list_heading_ = nullptr;
    QLabel *empty_label_ = nullptr;
    QLineEdit *search_edit_ = nullptr;
    QListWidget *project_list_ = nullptr;
    QPushButton *open_project_button_ = nullptr;
    QPushButton *open_recent_button_ = nullptr;
};

class StandaloneApplicationWindow final : public QMainWindow {
public:
    explicit StandaloneApplicationWindow(QWidget *parent = nullptr);

    void showHome(QWidget *home);
    void showEditor(QWidget *editor, const QString &project_name);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutWelcomePopup();
    void restoreWindowPlacement();
    void saveWindowPlacement() const;

    QStackedWidget *pages_ = nullptr;
    QWidget *home_backdrop_ = nullptr;
    QWidget *home_ = nullptr;
    QWidget *editor_ = nullptr;
};

} // namespace fxm::editor
