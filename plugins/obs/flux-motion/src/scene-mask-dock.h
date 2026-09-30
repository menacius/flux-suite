#pragma once

#include <QDockWidget>

#include <memory>
#include <vector>

class QLabel;
class QScrollArea;
class QTimer;
class QVBoxLayout;
struct obs_source;

void open_scene_mask_source_controls(obs_source *source,
                                     const char *layer_id);

class SceneMaskDock final : public QDockWidget {
public:
    struct MaskEntry;
    struct MaskCard;

    explicit SceneMaskDock(QWidget *parent = nullptr);
    ~SceneMaskDock() override;

    void refresh();

private:
    void rebuild_cards(const std::vector<MaskEntry> &entries);
    void update_cards(const std::vector<MaskEntry> &entries);

    QWidget *container_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QVBoxLayout *cards_layout_ = nullptr;
    QLabel *empty_label_ = nullptr;
    QTimer *refresh_timer_ = nullptr;
    std::vector<std::unique_ptr<MaskCard>> cards_;
};
