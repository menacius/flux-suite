#include "project-panel.h"

#include "title-assets.h"
#include "title-data.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QByteArray>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fxm::editor {

namespace {

QToolButton *make_button(QWidget *parent, const QString &tooltip,
                         const char *icon_name)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setFixedSize(28, 26);
    button->setIconSize(QSize(16, 16));
    button->setProperty("fxmProjectIcon", QString::fromUtf8(icon_name));
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setIcon(fxm_icon(icon_name));
    return button;
}

QString type_label(const Title &title)
{
    switch (title.graphic_type) {
    case TitleGraphicType::Graphic: return QStringLiteral("Graphic");
    case TitleGraphicType::Mask: return QStringLiteral("Mask");
    case TitleGraphicType::Stinger: return QStringLiteral("Stinger");
    default: return QStringLiteral("Title");
    }
}

const char *type_icon(const Title &title)
{
    switch (title.graphic_type) {
    case TitleGraphicType::Graphic: return "graphic.svg";
    case TitleGraphicType::Mask: return "mask.svg";
    case TitleGraphicType::Stinger: return "stinger.svg";
    default: return "text.svg";
    }
}

} // namespace

ProjectPanel::ProjectPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("FluxMotionProjectPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("FluxMotionProjectSearch"));
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(QStringLiteral("Search project"));
    layout->addWidget(search_);

    auto *browser = new QFrame(this);
    browser->setObjectName(QStringLiteral("FluxMotionProjectBrowser"));
    auto *browser_layout = new QVBoxLayout(browser);
    browser_layout->setContentsMargins(0, 0, 0, 0);
    browser_layout->setSpacing(0);

    list_ = new QListWidget(browser);
    list_->setObjectName(QStringLiteral("FluxMotionProjectItems"));
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setDragEnabled(true);
    list_->setAcceptDrops(true);
    list_->setDropIndicatorShown(true);
    list_->setDefaultDropAction(Qt::MoveAction);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setIconSize(QSize(18, 18));
    list_->setAlternatingRowColors(false);
    list_->setUniformItemSizes(true);
    browser_layout->addWidget(list_, 1);

    empty_ = new QLabel(QStringLiteral("No titles or graphics in this project."), browser);
    empty_->setObjectName(QStringLiteral("FluxMotionProjectEmpty"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    browser_layout->addWidget(empty_, 1);
    empty_->hide();

    auto *toolbar = new QToolBar(browser);
    toolbar->setObjectName(QStringLiteral("FluxMotionDockToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(16, 16));
    create_ = make_button(toolbar, QStringLiteral("New title"),
                          "add.svg");
    duplicate_ = make_button(toolbar, QStringLiteral("Duplicate"),
                             "duplicate.svg");
    rename_ = make_button(toolbar, QStringLiteral("Rename"),
                          "rename.svg");
    remove_ = make_button(toolbar, QStringLiteral("Delete"),
                          "delete.svg");
    import_ = make_button(toolbar, QStringLiteral("Import title"),
                          "import.svg");
    export_ = make_button(toolbar, QStringLiteral("Export title"),
                          "export.svg");
    insert_ = make_button(toolbar, QStringLiteral("Insert as title/graphic layer"),
                          "add-to-scene.svg");
    toolbar->addWidget(create_);
    toolbar->addWidget(duplicate_);
    toolbar->addWidget(rename_);
    toolbar->addWidget(remove_);
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    toolbar->addWidget(import_);
    toolbar->addWidget(export_);
    toolbar->addWidget(insert_);
    browser_layout->addWidget(toolbar);
    layout->addWidget(browser, 1);

    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString &text) { applyFilter(text); });
    connect(list_, &QListWidget::itemSelectionChanged, this,
            [this]() { updateActions(); });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (item && open_handler_)
                    open_handler_(item->data(Qt::UserRole).toString().toStdString());
            });
    connect(list_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &position) { showContextMenu(position); });
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
            [this]() {
                if (rebuilding_ || !reorder_handler_)
                    return;
                QTimer::singleShot(0, this, [this]() {
                    if (!rebuilding_ && reorder_handler_)
                        reorder_handler_(itemOrder());
                });
            });
    connect(create_, &QToolButton::clicked, this,
            [this]() { if (create_handler_) create_handler_(); });
    connect(duplicate_, &QToolButton::clicked, this,
            [this]() { if (duplicate_handler_) duplicate_handler_(selectedIds()); });
    connect(rename_, &QToolButton::clicked, this, [this]() {
        const auto ids = selectedIds();
        if (ids.size() == 1 && rename_handler_) rename_handler_(ids.front());
    });
    connect(remove_, &QToolButton::clicked, this,
            [this]() { if (delete_handler_) delete_handler_(selectedIds()); });
    connect(import_, &QToolButton::clicked, this,
            [this]() { if (import_handler_) import_handler_(); });
    connect(export_, &QToolButton::clicked, this,
            [this]() { if (export_handler_) export_handler_(selectedIds()); });
    connect(insert_, &QToolButton::clicked, this, [this]() {
        const auto ids = selectedIds();
        if (ids.size() == 1 && insert_handler_) insert_handler_(ids.front());
    });
    updateActions();
}

void ProjectPanel::setTitles(
    const std::vector<std::shared_ptr<Title>> &titles,
    const std::string &active_title_id)
{
    rebuilding_ = true;
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const auto &title : titles) {
        if (!title)
            continue;
        const QString name = QString::fromStdString(title->name);
        auto *item = new QListWidgetItem(name, list_);
        item->setData(Qt::UserRole, QString::fromStdString(title->id));
        item->setData(Qt::UserRole + 1, name);
        item->setData(Qt::UserRole + 2,
                      QString::fromUtf8(type_icon(*title)));
        item->setIcon(fxm_icon(type_icon(*title)));
        item->setToolTip(QStringLiteral("%1 · %2×%3 · %4 s")
            .arg(type_label(*title)).arg(title->width).arg(title->height)
            .arg(title->duration, 0, 'f', 2));
        if (title->id == active_title_id)
            list_->setCurrentItem(item);
    }
    rebuilding_ = false;
    applyFilter(search_->text());
    empty_->setVisible(list_->count() == 0);
    list_->setVisible(list_->count() != 0);
    updateActions();
}

void ProjectPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event)
        return;
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange)
        refreshIcons();
}

void ProjectPanel::refreshIcons()
{
    for (QToolButton *button : {create_, duplicate_, rename_, remove_,
                                import_, export_, insert_}) {
        if (!button) continue;
        const QByteArray icon_name =
            button->property("fxmProjectIcon").toString().toUtf8();
        if (!icon_name.isEmpty())
            button->setIcon(fxm_icon(icon_name.constData()));
    }
    if (!list_) return;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        if (!item) continue;
        const QByteArray icon_name =
            item->data(Qt::UserRole + 2).toString().toUtf8();
        if (!icon_name.isEmpty())
            item->setIcon(fxm_icon(icon_name.constData()));
    }
}

void ProjectPanel::setActiveTitle(const std::string &title_id)
{
    const QString id = QString::fromStdString(title_id);
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        if (item && item->data(Qt::UserRole).toString() == id) {
            list_->setCurrentItem(item);
            list_->scrollToItem(item);
            return;
        }
    }
}

std::vector<std::string> ProjectPanel::selectedIds() const
{
    std::vector<std::string> ids;
    for (QListWidgetItem *item : list_->selectedItems())
        if (item) ids.push_back(item->data(Qt::UserRole).toString().toStdString());
    return ids;
}

std::vector<std::string> ProjectPanel::itemOrder() const
{
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(list_->count()));
    for (int row = 0; row < list_->count(); ++row) {
        if (const QListWidgetItem *item = list_->item(row))
            ids.push_back(item->data(Qt::UserRole).toString().toStdString());
    }
    return ids;
}

void ProjectPanel::updateActions()
{
    const std::size_t count = selectedIds().size();
    duplicate_->setEnabled(count > 0);
    rename_->setEnabled(count == 1);
    remove_->setEnabled(count > 0 && list_->count() > 1);
    export_->setEnabled(count > 0);
    insert_->setEnabled(count == 1);
}

void ProjectPanel::applyFilter(const QString &query)
{
    const QString needle = query.trimmed();
    int visible = 0;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem *item = list_->item(row);
        if (!item) continue;
        const bool match = needle.isEmpty() ||
            item->data(Qt::UserRole + 1).toString().contains(
                needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        visible += match ? 1 : 0;
    }
    empty_->setText(list_->count() == 0
        ? QStringLiteral("No titles or graphics in this project.")
        : QStringLiteral("No project items match your search."));
    empty_->setVisible(visible == 0);
    list_->setVisible(visible != 0);
}

void ProjectPanel::showContextMenu(const QPoint &position)
{
    QMenu menu(this);
    QAction *open = menu.addAction(fxm_icon("edit.svg"), QStringLiteral("Open"));
    QAction *create = menu.addAction(fxm_icon("add.svg"), QStringLiteral("New Title"));
    QAction *duplicate = menu.addAction(fxm_icon("duplicate.svg"), QStringLiteral("Duplicate"));
    QAction *rename = menu.addAction(fxm_icon("rename.svg"), QStringLiteral("Rename"));
    QAction *remove = menu.addAction(fxm_icon("delete.svg"), QStringLiteral("Delete"));
    menu.addSeparator();
    QAction *import = menu.addAction(fxm_icon("import.svg"), QStringLiteral("Import Title…"));
    QAction *export_action = menu.addAction(fxm_icon("export.svg"), QStringLiteral("Export Title…"));
    QAction *insert_action = menu.addAction(
        fxm_icon("add-to-scene.svg"),
        QStringLiteral("Insert as Title/Graphic Layer"));
    const auto ids = selectedIds();
    open->setEnabled(ids.size() == 1);
    duplicate->setEnabled(!ids.empty());
    rename->setEnabled(ids.size() == 1);
    remove->setEnabled(!ids.empty() && list_->count() > 1);
    export_action->setEnabled(!ids.empty());
    insert_action->setEnabled(ids.size() == 1);
    QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(position));
    if (chosen == open && open_handler_) open_handler_(ids.front());
    else if (chosen == create && create_handler_) create_handler_();
    else if (chosen == duplicate && duplicate_handler_) duplicate_handler_(ids);
    else if (chosen == rename && rename_handler_) rename_handler_(ids.front());
    else if (chosen == remove && delete_handler_) delete_handler_(ids);
    else if (chosen == import && import_handler_) import_handler_();
    else if (chosen == export_action && export_handler_) export_handler_(ids);
    else if (chosen == insert_action && insert_handler_) insert_handler_(ids.front());
}

} // namespace fxm::editor
