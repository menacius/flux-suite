#pragma once

#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;
class QEvent;
struct Title;

namespace fxm::editor {

/* AE-style project navigation for the standalone editor. The panel owns only
 * presentation and intent callbacks; TitleDataStore remains the canonical
 * composition model and project serializer. */
class ProjectPanel final : public QWidget {
public:
    using IdHandler = std::function<void(const std::string &)>;
    using IdsHandler = std::function<void(const std::vector<std::string> &)>;
    using VoidHandler = std::function<void()>;

    explicit ProjectPanel(QWidget *parent = nullptr);

    void setTitles(const std::vector<std::shared_ptr<Title>> &titles,
                   const std::string &active_title_id);
    void setActiveTitle(const std::string &title_id);

    void setOpenHandler(IdHandler handler) { open_handler_ = std::move(handler); }
    void setCreateHandler(VoidHandler handler) { create_handler_ = std::move(handler); }
    void setDuplicateHandler(IdsHandler handler) { duplicate_handler_ = std::move(handler); }
    void setRenameHandler(IdHandler handler) { rename_handler_ = std::move(handler); }
    void setDeleteHandler(IdsHandler handler) { delete_handler_ = std::move(handler); }
    void setImportHandler(VoidHandler handler) { import_handler_ = std::move(handler); }
    void setExportHandler(IdsHandler handler) { export_handler_ = std::move(handler); }
    void setInsertHandler(IdHandler handler) { insert_handler_ = std::move(handler); }
    void setReorderHandler(IdsHandler handler) { reorder_handler_ = std::move(handler); }

protected:
    void changeEvent(QEvent *event) override;

private:
    std::vector<std::string> selectedIds() const;
    std::vector<std::string> itemOrder() const;
    void updateActions();
    void refreshIcons();
    void applyFilter(const QString &query);
    void showContextMenu(const QPoint &position);

    QLineEdit *search_ = nullptr;
    QListWidget *list_ = nullptr;
    QLabel *empty_ = nullptr;
    QToolButton *create_ = nullptr;
    QToolButton *duplicate_ = nullptr;
    QToolButton *rename_ = nullptr;
    QToolButton *remove_ = nullptr;
    QToolButton *import_ = nullptr;
    QToolButton *export_ = nullptr;
    QToolButton *insert_ = nullptr;
    bool rebuilding_ = false;

    IdHandler open_handler_;
    VoidHandler create_handler_;
    IdsHandler duplicate_handler_;
    IdHandler rename_handler_;
    IdsHandler delete_handler_;
    VoidHandler import_handler_;
    IdsHandler export_handler_;
    IdHandler insert_handler_;
    IdsHandler reorder_handler_;
};

} // namespace fxm::editor
