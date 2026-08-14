#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QFileSystemWatcher;
class QTimer;

class EffectsPresetsPanel : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPresetsPanel(QWidget *parent = nullptr);

    void reload();

signals:
    void effect_preset_activated(const QString &file_path);
    void audio_effect_activated(int effect_type);

private:
    bool filter_item(QTreeWidgetItem *item, const QString &query);
    void apply_filter(const QString &query);

    QLineEdit *search_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer *reload_timer_ = nullptr;
};
