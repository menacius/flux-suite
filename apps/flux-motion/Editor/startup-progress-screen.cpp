#include "startup-progress-screen.h"

#include <QLabel>
#include <QProgressBar>

#include <algorithm>

namespace fxm::editor {

StartupProgressScreen::StartupProgressScreen(const QPixmap &artwork)
    : QSplashScreen(artwork, Qt::WindowStaysOnTopHint)
{
    const int margin = 28;
    const int bar_height = 6;
    const int logical_width = std::max(1, width());
    const int logical_height = std::max(1, height());
    const int status_y = std::max(20, qRound(logical_height * 0.075));

    status_label_ = new QLabel(QStringLiteral("Initializing application..."),
                               this);
    const int progress_x = qRound(logical_width * 0.30);
    status_label_->setGeometry(progress_x, status_y,
                               std::max(1, logical_width - progress_x - 110), 24);
    status_label_->setStyleSheet(QStringLiteral(
        "color: white; font-size: 13px; background: transparent;"));

    percent_label_ = new QLabel(QStringLiteral("0%"), this);
    percent_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    percent_label_->setGeometry(std::max(margin, logical_width - 100),
                                status_y,
                                std::max(1, 72), 24);
    percent_label_->setStyleSheet(QStringLiteral(
        "color: rgba(255,255,255,190); font-size: 12px; "
        "background: transparent;"));

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setTextVisible(false);
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->setGeometry(progress_x, status_y + 27,
                               std::max(1, logical_width - progress_x - margin),
                               bar_height);
    progress_bar_->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 0; border-radius: 3px; "
        "background: rgba(255,255,255,45); }"
        "QProgressBar::chunk { border-radius: 3px; "
        "background: #7838f5; }"));
}

void StartupProgressScreen::bind(StartupProgress &progress)
{
    progress.setUpdateCallback(
        [this](const StartupProgressState &state) { updateProgress(state); });
}

void StartupProgressScreen::updateProgress(const StartupProgressState &state)
{
    status_label_->setText(state.task);
    if (state.determinate) {
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(state.percent);
        percent_label_->setText(QStringLiteral("%1%").arg(state.percent));
    } else {
        progress_bar_->setRange(0, 0);
        percent_label_->clear();
    }
    repaint();
}

} // namespace fxm::editor
