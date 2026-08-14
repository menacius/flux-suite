#pragma once

#include "cache-manager.h"

#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QWidget;

class PrerenderDock : public QWidget {
    Q_OBJECT
public:
    explicit PrerenderDock(QWidget *parent = nullptr);
    void setTitle(std::shared_ptr<Title> title);
    void setPlayhead(double time);

signals:
    void cacheEntireTimelineRequested();

private:
    void buildUi();
    void applySettings();
    void scheduleStatusUpdate();
    void updateStatus();

    std::shared_ptr<Title> title_;
    double playhead_ = 0.0;
    void setCacheControlsVisible(bool visible);

    QLabel *status_ = nullptr;
    QComboBox *start_mode_ = nullptr;
    QComboBox *playback_mode_ = nullptr;
    QComboBox *cadence_mode_ = nullptr;
    QCheckBox *cached_only_ = nullptr;
    QWidget *cache_section_ = nullptr;
    QVBoxLayout *cache_section_layout_ = nullptr;
    QPushButton *clear_all_cache_ = nullptr;
    QPushButton *cache_timeline_ = nullptr;
    QTimer *status_update_timer_ = nullptr;
};
