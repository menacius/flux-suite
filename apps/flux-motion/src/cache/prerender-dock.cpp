#include "prerender-dock.h"
#include "fxm-modern-controls.h"
#include "title-localization.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr const char *kPrerenderStartModeKey = "Prerender/StartMode";
constexpr const char *kPrerenderPlaybackModeKey = "Prerender/PlaybackMode";
constexpr const char *kPrerenderPlayAfterRenderingKey = "Prerender/PlayAfterRendering";
constexpr const char *kPrerenderCadenceModeKey = "Prerender/CadenceMode";
}

PrerenderDock::PrerenderDock(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    status_update_timer_ = new QTimer(this);
    status_update_timer_->setSingleShot(true);
    status_update_timer_->setInterval(100);
    connect(status_update_timer_, &QTimer::timeout, this, &PrerenderDock::updateStatus);
    connect(&CacheManager::instance(), &CacheManager::queueChanged, this, &PrerenderDock::scheduleStatusUpdate);
    connect(&CacheManager::instance(), &CacheManager::cacheStatesChanged, this, [this]() { scheduleStatusUpdate(); });
    connect(&CacheManager::instance(), &CacheManager::cacheEnabledChanged, this, [this](bool) { scheduleStatusUpdate(); });
    connect(&CacheManager::instance(), &CacheManager::diagnosticsChanged, this, &PrerenderDock::scheduleStatusUpdate);
}

void PrerenderDock::setTitle(std::shared_ptr<Title> title)
{
    title_ = std::move(title);
    updateStatus();
}

void PrerenderDock::setPlayhead(double time)
{
    playhead_ = time;
    Q_UNUSED(playhead_);
}

void PrerenderDock::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);

    start_mode_ = new QComboBox(this);
    start_mode_->addItems({fxm_tr("OBSTitles.FromCurrentTime"), fxm_tr("OBSTitles.FromBeginning")});
    form->addRow(fxm_tr("OBSTitles.Start"), start_mode_);

    playback_mode_ = new QComboBox(this);
    playback_mode_->addItems({fxm_tr("OBSTitles.Loop"), fxm_tr("OBSTitles.PingPongLoop"), fxm_tr("OBSTitles.PlayOnce"), fxm_tr("OBSTitles.PlaybackMode")});
    form->addRow(fxm_tr("OBSTitles.Mode"), playback_mode_);

    cadence_mode_ = new QComboBox(this);
    cadence_mode_->addItem(fxm_tr("OBSTitles.SkipFrames"), 0);
    cadence_mode_->addItem(fxm_tr("OBSTitles.PlayEveryFrame"), 1);
    cadence_mode_->setToolTip(fxm_tr("OBSTitles.EditorPlaybackCadenceTooltip"));
    form->addRow(fxm_tr("OBSTitles.EditorPlaybackCadence"), cadence_mode_);

    QSettings prerender_settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));


    start_mode_->setCurrentIndex(std::clamp(prerender_settings.value(QString::fromUtf8(kPrerenderStartModeKey), 0).toInt(), 0, start_mode_->count() - 1));
    playback_mode_->setCurrentIndex(std::clamp(prerender_settings.value(QString::fromUtf8(kPrerenderPlaybackModeKey), 0).toInt(), 0, playback_mode_->count() - 1));
    cadence_mode_->setCurrentIndex(std::clamp(prerender_settings.value(QString::fromUtf8(kPrerenderCadenceModeKey), 0).toInt(), 0, cadence_mode_->count() - 1));

    root->addLayout(form);

    cache_section_ = new QWidget(this);
    cache_section_layout_ = new QVBoxLayout(cache_section_);
    cache_section_layout_->setContentsMargins(0, 0, 0, 0);
    cache_section_layout_->setSpacing(8);

    cached_only_ = new FxmSwitch(fxm_tr("OBSTitles.PlayAfterRendering"), cache_section_);
    cached_only_->setChecked(prerender_settings.value(QString::fromUtf8(kPrerenderPlayAfterRenderingKey), false).toBool());
    cache_section_layout_->addWidget(cached_only_);

    auto *grid = new QGridLayout();
    auto add_button = [&](const QString &text, int row, int col, auto slot) {
        auto *button = new QPushButton(text, cache_section_);
        connect(button, &QPushButton::clicked, this, slot);
        grid->addWidget(button, row, col);
        return button;
    };
    clear_all_cache_ = add_button(
        fxm_tr("OBSTitles.ClearAllCache"), 0, 0,
        []() { CacheManager::instance().clearAll(); });
    cache_timeline_ = add_button(fxm_tr("OBSTitles.CacheEntireTimeline"), 0, 1, [this]() {
        applySettings();
        if (title_) CacheManager::instance().queueWholeTimeline(title_);
        emit cacheEntireTimelineRequested();
    });
    cache_section_layout_->addLayout(grid);

    status_ = new QLabel(cache_section_);
    status_->setWordWrap(true);
    cache_section_layout_->addWidget(status_);
    root->addWidget(cache_section_);

    for (auto *combo : {start_mode_, playback_mode_, cadence_mode_})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PrerenderDock::applySettings);
    connect(cached_only_, &QCheckBox::toggled, this, &PrerenderDock::applySettings);
    applySettings();
}

void PrerenderDock::applySettings()
{
    CachePlaybackSettings settings;
    settings.from_beginning = start_mode_ && start_mode_->currentIndex() == 1;
    const int playback_index = playback_mode_ ? playback_mode_->currentIndex() : 0;
    settings.mode = playback_index == 1 ? CachePlaybackMode::PingPong
                  : playback_index == 2 ? CachePlaybackMode::PlayOnce
                                        : CachePlaybackMode::Loop;
    settings.follow_title_playback_mode = playback_index == 3;
    settings.skip_frames = 0;
    settings.speed_percent = 100.0;
    settings.cached_frames_only = cached_only_ && cached_only_->isChecked();
    settings.play_every_frame = cadence_mode_ && cadence_mode_->currentData().toInt() == 1;
    CacheManager::instance().setPlaybackSettings(settings);

    QSettings prerender_settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    if (start_mode_) prerender_settings.setValue(QString::fromUtf8(kPrerenderStartModeKey), start_mode_->currentIndex());
    if (playback_mode_) prerender_settings.setValue(QString::fromUtf8(kPrerenderPlaybackModeKey), playback_mode_->currentIndex());
    if (cadence_mode_) prerender_settings.setValue(QString::fromUtf8(kPrerenderCadenceModeKey), cadence_mode_->currentIndex());
    if (cached_only_) prerender_settings.setValue(QString::fromUtf8(kPrerenderPlayAfterRenderingKey), cached_only_->isChecked());
}

void PrerenderDock::setCacheControlsVisible(bool visible)
{
    if (cache_section_)
        cache_section_->setVisible(visible);
    if (cached_only_)
        cached_only_->setVisible(visible);
    if (clear_all_cache_)
        clear_all_cache_->setVisible(visible);
    if (cache_timeline_)
        cache_timeline_->setVisible(visible);
    if (status_)
        status_->setVisible(visible);
}

void PrerenderDock::scheduleStatusUpdate()
{
    if (status_update_timer_ && !status_update_timer_->isActive())
        status_update_timer_->start();
}

void PrerenderDock::updateStatus()
{
    if (!status_) return;
    const bool enabled = CacheManager::instance().cacheEnabled();
    setCacheControlsVisible(enabled);
    if (!title_) {
        status_->setText(fxm_tr("OBSTitles.NoTitleLoaded"));
        return;
    }
    const TitleCacheability cacheability = CacheManager::instance().titleCacheability(title_);
    const bool frame_prerender_available = enabled && cacheability != TitleCacheability::NonCacheable;
    if (cache_timeline_) cache_timeline_->setEnabled(frame_prerender_available);
    if (clear_all_cache_) clear_all_cache_->setEnabled(enabled);

    const QString message = CacheManager::instance().titleCacheabilityMessage(title_);
    if (!message.isEmpty()) {
        status_->setText(message);
    } else {
        status_->setText(fxm_tr("OBSTitles.PrerenderQueueStatus"));
    }
}
