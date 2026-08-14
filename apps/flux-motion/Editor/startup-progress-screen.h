#pragma once

#include "startup-progress.h"

#include <QSplashScreen>

class QLabel;
class QProgressBar;

namespace fxm::editor {

class StartupProgressScreen final : public QSplashScreen {
public:
    explicit StartupProgressScreen(const QPixmap &artwork);

    void bind(StartupProgress &progress);

private:
    void updateProgress(const StartupProgressState &state);

    QLabel *status_label_ = nullptr;
    QLabel *percent_label_ = nullptr;
    QProgressBar *progress_bar_ = nullptr;
};

} // namespace fxm::editor
