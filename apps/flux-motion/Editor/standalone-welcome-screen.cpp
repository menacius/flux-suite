#include "standalone-welcome-screen.h"

#include "recent-projects.h"
#include "title-assets.h"

#include <QCloseEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QIcon>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace fxm::editor {
namespace {

constexpr auto kMainWindowGroup = "StandaloneMainWindow";
constexpr auto kMainWindowGeometryKey = "geometry";
constexpr auto kMainWindowScreenKey = "screen";
constexpr auto kMainWindowScreenOffsetKey = "screenOffset";
constexpr auto kMainWindowNormalSizeKey = "normalSize";
constexpr auto kMainWindowMaximizedKey = "maximized";
constexpr auto kMainWindowFullScreenKey = "fullScreen";

QPushButton *make_action_button(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setMinimumHeight(40);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

StandaloneWelcomeScreen::StandaloneWelcomeScreen(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FluxMotionWelcomePopup"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumSize(720, 470);
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(42.0);
    shadow->setOffset(0.0, 12.0);
    shadow->setColor(QColor(0, 0, 0, 185));
    setGraphicsEffect(shadow);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *sidebar = new QFrame(this);
    sidebar->setObjectName(QStringLiteral("welcomeSidebar"));
    sidebar->setFixedWidth(288);
    auto *actions = new QVBoxLayout(sidebar);
    actions->setContentsMargins(38, 42, 38, 38);
    actions->setSpacing(12);
    auto *brand_row = new QHBoxLayout;
    brand_row->setSpacing(12);
    auto *brand_logo = new QLabel(sidebar);
    brand_logo->setObjectName(QStringLiteral("welcomeBrandLogo"));
    brand_logo->setFixedSize(48, 48);
    brand_logo->setAlignment(Qt::AlignCenter);
    brand_logo->setAccessibleName(QStringLiteral("Flux Motion logo"));
    brand_logo->setPixmap(fxm_brand_icon().pixmap(QSize(48, 48)));
    auto *brand = new QLabel(QStringLiteral("FLUX MOTION"), sidebar);
    brand->setObjectName(QStringLiteral("welcomeBrand"));
    brand_row->addWidget(brand_logo);
    brand_row->addWidget(brand);
    brand_row->addStretch(1);
    auto *home_label = new QLabel(QStringLiteral("Home"), sidebar);
    home_label->setObjectName(QStringLiteral("welcomeNavigation"));
    auto *welcome = new QLabel(QStringLiteral("Welcome"), sidebar);
    welcome->setObjectName(QStringLiteral("welcomeTitle"));
    auto *subtitle = new QLabel(
        QStringLiteral("Create a motion graphic or continue where you left off."),
        sidebar);
    subtitle->setWordWrap(true);
    subtitle->setObjectName(QStringLiteral("welcomeSubtitle"));

    auto *create_button = make_action_button(
        QStringLiteral("Create New Project"), sidebar);
    create_button->setObjectName(QStringLiteral("welcomePrimaryButton"));
    open_project_button_ = make_action_button(QStringLiteral("Open Project"),
                                              sidebar);
    open_recent_button_ = make_action_button(QStringLiteral("Open Recent"),
                                             sidebar);
    auto *import_button = make_action_button(
        QStringLiteral("Import Project  (Coming Soon)"), sidebar);
    import_button->setEnabled(false);
    import_button->setToolTip(QStringLiteral(
        "Project import is reserved for a future Flux Motion release."));

    actions->addLayout(brand_row);
    actions->addSpacing(28);
    actions->addWidget(home_label);
    actions->addSpacing(18);
    actions->addWidget(welcome);
    actions->addWidget(subtitle);
    actions->addSpacing(22);
    actions->addWidget(create_button);
    actions->addWidget(open_project_button_);
    actions->addWidget(open_recent_button_);
    actions->addWidget(import_button);
    actions->addStretch(1);

    auto *project_area = new QWidget(this);
    project_area->setObjectName(QStringLiteral("welcomeContent"));
    auto *projects = new QVBoxLayout(project_area);
    projects->setContentsMargins(52, 46, 52, 46);
    projects->setSpacing(12);
    list_heading_ = new QLabel(QStringLiteral("Recent Projects"), project_area);
    list_heading_->setObjectName(QStringLiteral("welcomeSectionTitle"));
    search_edit_ = new QLineEdit(project_area);
    search_edit_->setPlaceholderText(QStringLiteral("Search projects"));
    search_edit_->setClearButtonEnabled(true);
    search_edit_->setMinimumHeight(36);
    project_list_ = new QListWidget(project_area);
    project_list_->setObjectName(QStringLiteral("welcomeProjectList"));
    project_list_->setIconSize(QSize(112, 63));
    project_list_->setAlternatingRowColors(false);
    project_list_->setSpacing(4);
    project_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    empty_label_ = new QLabel(project_area);
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->setWordWrap(true);
    empty_label_->setObjectName(QStringLiteral("welcomeEmptyState"));

    projects->addWidget(list_heading_);
    projects->addWidget(search_edit_);
    projects->addWidget(project_list_, 1);
    projects->addWidget(empty_label_, 1);
    root->addWidget(sidebar);
    root->addWidget(project_area, 1);

    setStyleSheet(QStringLiteral(
        "#FluxMotionWelcomePopup { background: #141316; color: #f4f3f8; "
        "border: 1px solid #3b3840; border-radius: 16px; }"
        "#welcomeSidebar { background: #1a191d; "
        "border-right: 1px solid #2b2930; "
        "border-top-left-radius: 15px; border-bottom-left-radius: 15px; }"
        "#welcomeContent { background: #141316; "
        "border-top-right-radius: 15px; border-bottom-right-radius: 15px; }"
        "#welcomeBrandLogo { background: transparent; }"
        "#welcomeBrand { color: #9d8cff; font-size: 12px; font-weight: 800; "
        "letter-spacing: 2px; }"
        "#welcomeNavigation { color: #f4f3f8; font-size: 14px; font-weight: 700; "
        "padding: 10px 12px; border-left: 3px solid #7838f5; "
        "background: #292633; border-radius: 7px; }"
        "#welcomeTitle { color: #f4f3f8; font-size: 30px; font-weight: 700; }"
        "#welcomeSubtitle { color: #aaa6b3; font-size: 13px; }"
        "#welcomeEmptyState { color: #85818d; "
        "font-size: 13px; }"
        "#welcomeSectionTitle { color: #f4f3f8; font-size: 21px; font-weight: 700; }"
        "QLineEdit { color: #f4f3f8; background: #1d1c20; "
        "border: 1px solid #3b3840; border-radius: 9px; padding: 0 12px; }"
        "QLineEdit:focus { border-color: #7757ff; }"
        "QPushButton { color: #d1cdd7; background: #29272e; "
        "border: 1px solid #403d46; border-radius: 9px; padding: 8px 14px; "
        "font-weight: 650; }"
        "QPushButton:hover { color: white; background: #37333f; "
        "border-color: #554a87; }"
        "QPushButton:disabled { color: #77727f; background: #242228; "
        "border-color: #302e35; }"
        "#welcomePrimaryButton { background: #7255f5; color: white; "
        "border: 0; font-weight: 700; }"
        "#welcomePrimaryButton:hover { background: #8369f8; }"
        "#welcomeProjectList { color: #f4f3f8; background: #1d1c20; "
        "border: 1px solid #302e35; border-radius: 12px; padding: 6px; "
        "outline: 0; }"
        "#welcomeProjectList::item { padding: 11px 10px; "
        "border-radius: 8px; }"
        "#welcomeProjectList::item:hover { background: #29272e; }"
        "#welcomeProjectList::item:selected { background: #373149; "
        "color: white; border: 1px solid #554a87; }"));

    connect(create_button, &QPushButton::clicked, this, [this]() {
        if (create_project_handler_)
            create_project_handler_();
    });
    connect(open_project_button_, &QPushButton::clicked, this, [this]() {
        if (open_project_handler_)
            open_project_handler_(QString());
    });
    connect(open_recent_button_, &QPushButton::clicked, this, [this]() {
        if (project_view_ == ProjectView::Recent)
            openSelectedProject();
        else
            showRecentProjects();
    });
    connect(project_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { openSelectedProject(); });
    connect(project_list_, &QListWidget::itemSelectionChanged, this, [this]() {
        const bool selected = project_list_->currentItem() != nullptr;
        open_recent_button_->setEnabled(
            project_view_ != ProjectView::Recent || selected);
        if (project_view_ == ProjectView::All)
            open_project_button_->setEnabled(selected);
    });
    connect(search_edit_, &QLineEdit::textChanged, this,
            [this]() { rebuildProjectList(); });

    rebuildProjectList();
}

StandaloneApplicationWindow::StandaloneApplicationWindow(QWidget *parent)
    : QMainWindow(parent, Qt::Window), pages_(new QStackedWidget(this))
{
    fxm_apply_brand_icon(this);
    setObjectName(QStringLiteral("FluxMotionStandaloneMainWindow"));
    setWindowTitle(QStringLiteral("Flux Motion"));
    resize(1280, 760);
    setMinimumSize(900, 600);
    pages_->setContentsMargins(0, 0, 0, 0);
    home_backdrop_ = new QWidget(pages_);
    home_backdrop_->setObjectName(QStringLiteral("FluxMotionWelcomeBackdrop"));
    home_backdrop_->setStyleSheet(QStringLiteral(
        "#FluxMotionWelcomeBackdrop { background: #0d0c0f; }"));
    pages_->addWidget(home_backdrop_);
    setCentralWidget(pages_);
    restoreWindowPlacement();
}

void StandaloneApplicationWindow::showHome(QWidget *home)
{
    if (!home)
        return;
    if (home_ && home_ != home)
        home_->hide();
    home_ = home;
    if (home_->parentWidget() != pages_)
        home_->setParent(pages_);
    pages_->setCurrentWidget(home_backdrop_);
    layoutWelcomePopup();
    home_->show();
    home_->raise();
    home_->setFocus(Qt::OtherFocusReason);
    setWindowTitle(QStringLiteral("Flux Motion — Home"));
}

void StandaloneApplicationWindow::showEditor(QWidget *editor,
                                             const QString &project_name)
{
    if (!editor)
        return;
    if (home_)
        home_->hide();
    editor_ = editor;
    if (pages_->indexOf(editor) < 0)
        pages_->addWidget(editor);
    pages_->setCurrentWidget(editor);
    editor->show();
    setWindowTitle(project_name.trimmed().isEmpty()
        ? QStringLiteral("Flux Motion")
        : QStringLiteral("%1 — Flux Motion").arg(project_name));
}

void StandaloneApplicationWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    layoutWelcomePopup();
}

void StandaloneApplicationWindow::layoutWelcomePopup()
{
    if (!home_ || !pages_)
        return;
    const QRect available = pages_->rect().adjusted(32, 28, -32, -28);
    if (!available.isValid() || available.isEmpty())
        return;
    const QSize popup_size(
        std::min(1040, available.width()),
        std::min(660, available.height()));
    home_->setGeometry(QRect(
        available.center() - QPoint(popup_size.width() / 2,
                                    popup_size.height() / 2),
        popup_size));
}

void StandaloneApplicationWindow::closeEvent(QCloseEvent *event)
{
    if (editor_ && editor_->isVisible() && !editor_->close()) {
        event->ignore();
        return;
    }
    saveWindowPlacement();
    QMainWindow::closeEvent(event);
}

void StandaloneApplicationWindow::restoreWindowPlacement()
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kMainWindowGroup));
    const QByteArray saved_geometry =
        settings.value(QString::fromUtf8(kMainWindowGeometryKey)).toByteArray();
    const QString screen_name =
        settings.value(QString::fromUtf8(kMainWindowScreenKey)).toString();
    const QPoint screen_offset =
        settings.value(QString::fromUtf8(kMainWindowScreenOffsetKey)).toPoint();
    const QSize normal_size =
        settings.value(QString::fromUtf8(kMainWindowNormalSizeKey)).toSize();
    const bool maximized =
        settings.value(QString::fromUtf8(kMainWindowMaximizedKey), false).toBool();
    const bool full_screen =
        settings.value(QString::fromUtf8(kMainWindowFullScreenKey), false).toBool();
    settings.endGroup();

    if (!saved_geometry.isEmpty())
        restoreGeometry(saved_geometry);

    QScreen *target_screen = nullptr;
    for (QScreen *candidate : QGuiApplication::screens()) {
        if (candidate && candidate->name() == screen_name) {
            target_screen = candidate;
            break;
        }
    }
    if (!target_screen)
        target_screen = QGuiApplication::primaryScreen();

    if (target_screen && normal_size.isValid() && !normal_size.isEmpty()) {
        const QRect available = target_screen->availableGeometry();
        const QSize restored_size(
            std::clamp(normal_size.width(),
                       std::min(minimumWidth(), available.width()),
                       available.width()),
            std::clamp(normal_size.height(),
                       std::min(minimumHeight(), available.height()),
                       available.height()));
        const int maximum_x = available.right() - restored_size.width() + 1;
        const int maximum_y = available.bottom() - restored_size.height() + 1;
        const QPoint requested = available.topLeft() + screen_offset;
        const QPoint restored_position(
            std::clamp(requested.x(), available.left(), maximum_x),
            std::clamp(requested.y(), available.top(), maximum_y));
        setGeometry(QRect(restored_position, restored_size));
    } else {
        bool on_screen = false;
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen && screen->availableGeometry().intersects(frameGeometry())) {
                on_screen = true;
                break;
            }
        }
        if (!on_screen && target_screen) {
            const QRect available = target_screen->availableGeometry();
            move(available.center() - rect().center());
        }
    }

    if (full_screen)
        setWindowState(windowState() | Qt::WindowFullScreen);
    else if (maximized)
        setWindowState(windowState() | Qt::WindowMaximized);
}

void StandaloneApplicationWindow::saveWindowPlacement() const
{
    QScreen *current_screen = nullptr;
    if (windowHandle())
        current_screen = windowHandle()->screen();
    if (!current_screen)
        current_screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!current_screen)
        current_screen = QGuiApplication::primaryScreen();

    const QRect normal = (isMaximized() || isFullScreen())
        ? normalGeometry() : geometry();
    const QPoint offset = current_screen
        ? normal.topLeft() - current_screen->availableGeometry().topLeft()
        : normal.topLeft();

    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kMainWindowGroup));
    settings.setValue(QString::fromUtf8(kMainWindowGeometryKey), saveGeometry());
    settings.setValue(QString::fromUtf8(kMainWindowScreenKey),
                      current_screen ? current_screen->name() : QString());
    settings.setValue(QString::fromUtf8(kMainWindowScreenOffsetKey), offset);
    settings.setValue(QString::fromUtf8(kMainWindowNormalSizeKey), normal.size());
    settings.setValue(QString::fromUtf8(kMainWindowMaximizedKey), isMaximized());
    settings.setValue(QString::fromUtf8(kMainWindowFullScreenKey), isFullScreen());
    settings.endGroup();
    settings.sync();
}

void StandaloneWelcomeScreen::setProjects(
    std::vector<WelcomeProject> projects)
{
    projects_ = std::move(projects);
    rebuildProjectList();
}

void StandaloneWelcomeScreen::setCreateProjectHandler(
    std::function<void()> handler)
{
    create_project_handler_ = std::move(handler);
}

void StandaloneWelcomeScreen::setOpenProjectHandler(
    std::function<void(const QString &)> handler)
{
    open_project_handler_ = std::move(handler);
}

void StandaloneWelcomeScreen::recordProjectOpened(
    const QString &project_path, const QString &project_name,
    const QByteArray &thumbnail_png_base64)
{
    RecentProjects::recordOpened(project_path, project_name,
                                 thumbnail_png_base64);
}

QStringList StandaloneWelcomeScreen::recentProjectPaths() const
{
    QStringList paths;
    for (const RecentProject &project : RecentProjects::entries())
        paths.push_back(project.path);
    return paths;
}

void StandaloneWelcomeScreen::showAllProjects()
{
    project_view_ = ProjectView::All;
    list_heading_->setText(QStringLiteral("All Projects"));
    open_project_button_->setText(QStringLiteral("Open Selected"));
    open_recent_button_->setText(QStringLiteral("Show Recent"));
    rebuildProjectList();
    search_edit_->setFocus();
}

void StandaloneWelcomeScreen::showRecentProjects()
{
    project_view_ = ProjectView::Recent;
    list_heading_->setText(QStringLiteral("Recent Projects"));
    open_project_button_->setText(QStringLiteral("Open Project"));
    open_recent_button_->setText(QStringLiteral("Open Recent"));
    rebuildProjectList();
}

void StandaloneWelcomeScreen::rebuildProjectList()
{
    const QString selected_id = project_list_->currentItem()
        ? project_list_->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString query = search_edit_->text().trimmed();
    const QStringList recent_paths = recentProjectPaths();
    project_list_->clear();

    std::vector<const WelcomeProject *> visible;
    if (project_view_ == ProjectView::Recent) {
        for (const QString &recent_path : recent_paths) {
            const auto found = std::find_if(
                projects_.begin(), projects_.end(),
                [&recent_path](const WelcomeProject &project) {
                    return project.full_path.compare(
                        recent_path, Qt::CaseInsensitive) == 0;
                });
            if (found != projects_.end())
                visible.push_back(&*found);
        }
    } else {
        for (const WelcomeProject &project : projects_)
            visible.push_back(&project);
    }

    for (const WelcomeProject *project : visible) {
        if (!project || (!query.isEmpty() &&
            !project->name.contains(query, Qt::CaseInsensitive) &&
            !project->detail.contains(query, Qt::CaseInsensitive)))
            continue;
        auto *item = new QListWidgetItem(project->name, project_list_);
        item->setData(Qt::UserRole, project->id);
        item->setData(Qt::UserRole + 1, project->full_path);
        const QString opened = project->last_opened.isValid()
            ? QLocale().toString(project->last_opened, QLocale::ShortFormat)
            : QString();
        QString secondary = opened.isEmpty()
            ? project->full_path
            : QStringLiteral("%1  •  %2").arg(opened, project->full_path);
        if (secondary.isEmpty())
            secondary = project->detail;
        item->setText(QStringLiteral("%1\n%2").arg(project->name, secondary));
        item->setToolTip(project->full_path.isEmpty()
            ? project->detail : project->full_path);
        if (!project->thumbnail_png_base64.isEmpty()) {
            QPixmap thumbnail;
            if (thumbnail.loadFromData(
                    QByteArray::fromBase64(project->thumbnail_png_base64),
                    "PNG"))
                item->setIcon(QIcon(thumbnail));
        }
        item->setSizeHint(QSize(0, 68));
        if (project->id == selected_id)
            project_list_->setCurrentItem(item);
    }

    const bool empty = project_list_->count() == 0;
    project_list_->setVisible(!empty);
    empty_label_->setVisible(empty);
    empty_label_->setText(project_view_ == ProjectView::Recent
        ? QStringLiteral("No recent projects yet.\nCreate a project or choose Open Project to browse all projects.")
        : QStringLiteral("No projects match your search."));
    if (!empty && !project_list_->currentItem())
        project_list_->setCurrentRow(0);

    const bool selected = project_list_->currentItem() != nullptr;
    open_recent_button_->setEnabled(
        project_view_ != ProjectView::Recent || selected);
    open_project_button_->setEnabled(
        project_view_ != ProjectView::All || selected);
}

void StandaloneWelcomeScreen::openSelectedProject()
{
    const auto *item = project_list_->currentItem();
    if (!item || !open_project_handler_)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (!id.isEmpty())
        open_project_handler_(id);
}

} // namespace fxm::editor
