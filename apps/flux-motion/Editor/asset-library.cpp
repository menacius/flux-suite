#include "asset-library.h"
#include "title-assets.h"
#include "title-localization.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDrag>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace obsfxm {
namespace {

class AssetListWidget final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        if (items.isEmpty())
            return nullptr;
        auto *mime = new QMimeData();
        mime->setData(QString::fromUtf8(kAssetLayerMimeType),
                      items.front()->data(Qt::UserRole).toString().toUtf8());
        mime->setText(items.front()->text());
        return mime;
    }

    QStringList mimeTypes() const override
    {
        return {QString::fromUtf8(kAssetLayerMimeType)};
    }

    Qt::DropActions supportedDropActions() const override
    {
        return Qt::CopyAction;
    }
};

QPixmap assetThumbnail(const Title &title, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    if (!title.preview_screenshot_png_base64.empty()) {
        QPixmap decoded;
        const QByteArray bytes = QByteArray::fromBase64(
            QByteArray::fromStdString(title.preview_screenshot_png_base64));
        if (decoded.loadFromData(bytes, "PNG")) {
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QPixmap scaled = decoded.scaled(size, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
            p.drawPixmap((size.width() - scaled.width()) / 2,
                         (size.height() - scaled.height()) / 2, scaled);
            return pixmap;
        }
    }

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor bg = QApplication::palette().color(QPalette::Base);
    const QColor fg = QApplication::palette().color(QPalette::Text);
    p.fillRect(pixmap.rect(), bg);
    p.setPen(fg);
    p.drawRoundedRect(pixmap.rect().adjusted(1, 1, -2, -2), 5, 5);
    p.drawText(pixmap.rect().adjusted(6, 4, -6, -4),
               Qt::AlignCenter | Qt::TextWordWrap,
               QString::fromStdString(title.name));
    return pixmap;
}

} // namespace

AssetLibraryPanel::AssetLibraryPanel(bool animated_assets, QWidget *parent,
                                     bool filter_by_animation)
    : QWidget(parent), animated_assets_(animated_assets),
      filter_by_animation_(filter_by_animation)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *filters = new QHBoxLayout();
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(fxm_tr("OBSTitles.SearchAssets"));
    category_filter_ = new QComboBox(this);
    filters->addWidget(search_, 1);
    filters->addWidget(category_filter_);
    root->addLayout(filters);

    list_ = new AssetListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(128, 72));
    list_->setGridSize(QSize(154, 112));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setDefaultDropAction(Qt::CopyAction);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(list_, 1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    insert_button_ = new QToolButton(this);
    insert_button_->setText(fxm_tr("OBSTitles.InsertAsset"));
    insert_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    insert_button_->setIcon(fxm_icon("add.svg"));
    delete_button_ = new QToolButton(this);
    delete_button_->setText(fxm_tr("OBSTitles.DeleteAsset"));
    delete_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    delete_button_->setIcon(fxm_icon("delete.svg"));
    buttons->addWidget(insert_button_);
    buttons->addWidget(delete_button_);
    root->addLayout(buttons);

    connect(search_, &QLineEdit::textChanged, this, &AssetLibraryPanel::refreshList);
    connect(category_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AssetLibraryPanel::refreshList);
    connect(insert_button_, &QToolButton::clicked, this, &AssetLibraryPanel::insertSelected);
    connect(delete_button_, &QToolButton::clicked, this, &AssetLibraryPanel::deleteSelected);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { insertSelected(); });
    connect(list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        auto *clicked = list_->itemAt(pos);
        if (!clicked)
            return;
        if (clicked != list_->currentItem())
            list_->setCurrentItem(clicked);
        QMenu menu(list_);
        QAction *edit = menu.addAction(
            fxm_icon("edit.svg"), fxm_tr("OBSTitles.EditAsset"));
        QAction *insert = menu.addAction(
            fxm_icon("add.svg"), fxm_tr("OBSTitles.InsertAsset"));
        menu.addSeparator();
        QAction *remove = menu.addAction(
            fxm_icon("delete.svg"), fxm_tr("OBSTitles.DeleteAsset"));
        QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
        if (chosen == edit)
            editSelected();
        else if (chosen == insert)
            insertSelected();
        else if (chosen == remove)
            deleteSelected();
    });

    change_callback_id_ = TitleDataStore::instance().on_change([this]() {
        QTimer::singleShot(0, this, [this]() { reload(); });
    });
    reload();
}

AssetLibraryPanel::~AssetLibraryPanel()
{
    if (change_callback_id_)
        TitleDataStore::instance().remove_change_callback(change_callback_id_);
}

void AssetLibraryPanel::setInsertCallback(
    std::function<void(const std::string &)> callback)
{
    insert_callback_ = std::move(callback);
}

void AssetLibraryPanel::setEditCallback(
    std::function<void(const std::string &)> callback)
{
    edit_callback_ = std::move(callback);
}

void AssetLibraryPanel::reload()
{
    rebuildCategories();
    refreshList();
}

void AssetLibraryPanel::rebuildCategories()
{
    const QString previous = category_filter_->currentData().toString();
    QStringList categories;
    for (const auto &title : TitleDataStore::instance().title_snapshots()) {
        if (!title || !title->is_asset ||
            (filter_by_animation_ && title->asset_animated != animated_assets_))
            continue;
        const QString category = QString::fromStdString(title->asset_category).trimmed();
        if (!category.isEmpty() && !categories.contains(category, Qt::CaseInsensitive))
            categories.push_back(category);
    }
    categories.sort(Qt::CaseInsensitive);
    category_filter_->blockSignals(true);
    category_filter_->clear();
    category_filter_->addItem(fxm_tr("OBSTitles.AllCategories"), QString());
    for (const QString &category : categories)
        category_filter_->addItem(category, category);
    const int restore = category_filter_->findData(previous);
    if (restore >= 0)
        category_filter_->setCurrentIndex(restore);
    category_filter_->blockSignals(false);
}

void AssetLibraryPanel::refreshList()
{
    const QString previous = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString query = search_->text().trimmed();
    const QString category = category_filter_->currentData().toString();
    list_->clear();

    for (const auto &title : TitleDataStore::instance().title_snapshots()) {
        if (!title || !title->is_asset ||
            (filter_by_animation_ && title->asset_animated != animated_assets_))
            continue;
        const QString name = QString::fromStdString(title->name);
        const QString title_category = QString::fromStdString(title->asset_category);
        QStringList title_tags;
        for (const std::string &tag : title->asset_tags)
            title_tags.push_back(QString::fromStdString(tag));
        if (!query.isEmpty() && !name.contains(query, Qt::CaseInsensitive) &&
            !title_category.contains(query, Qt::CaseInsensitive) &&
            !title_tags.join(QStringLiteral(", ")).contains(
                query, Qt::CaseInsensitive))
            continue;
        if (!category.isEmpty() && title_category.compare(category, Qt::CaseInsensitive) != 0)
            continue;

        auto *item = new QListWidgetItem(QIcon(assetThumbnail(*title, QSize(128, 72))), name);
        item->setData(Qt::UserRole, QString::fromStdString(title->id));
        item->setToolTip(QStringLiteral("%1%2\n%3 · %4 s")
            .arg(title_category,
                 title_tags.isEmpty()
                    ? QString() : QStringLiteral(" · %1").arg(title_tags.join(QStringLiteral(", "))),
                 animated_assets_ ? fxm_tr("OBSTitles.AnimatedAsset")
                                  : fxm_tr("OBSTitles.Asset"))
            .arg(title->duration, 0, 'f', 2));
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        list_->addItem(item);
        if (item->data(Qt::UserRole).toString() == previous)
            list_->setCurrentItem(item);
    }
}

void AssetLibraryPanel::insertSelected()
{
    auto *item = list_->currentItem();
    if (!item || !insert_callback_)
        return;
    insert_callback_(item->data(Qt::UserRole).toString().toStdString());
}

void AssetLibraryPanel::editSelected()
{
    auto *item = list_->currentItem();
    if (!item || !edit_callback_)
        return;
    edit_callback_(item->data(Qt::UserRole).toString().toStdString());
}

void AssetLibraryPanel::deleteSelected()
{
    auto *item = list_->currentItem();
    if (!item)
        return;
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    auto title = TitleDataStore::instance().get_title(id);
    if (!title || !title->is_asset)
        return;
    if (QMessageBox::question(this, fxm_tr("OBSTitles.DeleteAsset"),
            fxm_tr("OBSTitles.DeleteAssetConfirm").arg(QString::fromStdString(title->name)))
        != QMessageBox::Yes)
        return;
    TitleDataStore::instance().delete_title(id);
    TitleDataStore::instance().save_async();
}

} // namespace obsfxm
