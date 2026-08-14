#pragma once

#include "title-data.h"

#include <QWidget>
#include <QString>
#include <functional>

class QLineEdit;
class QComboBox;
class QListWidget;
class QToolButton;

namespace obsfxm {

inline constexpr const char *kAssetLayerMimeType = "application/x-flux-motion-asset";

class AssetLibraryPanel : public QWidget {
    Q_OBJECT
public:
    explicit AssetLibraryPanel(bool animated_assets, QWidget *parent = nullptr,
                               bool filter_by_animation = true);
    ~AssetLibraryPanel() override;

    void setInsertCallback(std::function<void(const std::string &)> callback);
    void setEditCallback(std::function<void(const std::string &)> callback);
    void reload();

private slots:
    void refreshList();
    void insertSelected();
    void editSelected();
    void deleteSelected();

private:
    void rebuildCategories();
    bool animated_assets_ = false;
    bool filter_by_animation_ = true;
    uint64_t change_callback_id_ = 0;
    QLineEdit *search_ = nullptr;
    QComboBox *category_filter_ = nullptr;
    QListWidget *list_ = nullptr;
    QToolButton *insert_button_ = nullptr;
    QToolButton *delete_button_ = nullptr;
    std::function<void(const std::string &)> insert_callback_;
    std::function<void(const std::string &)> edit_callback_;
};

} // namespace obsfxm
