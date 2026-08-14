#pragma once

#include "title-data.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace fxm::editor {

class ProjectLoadingDialog final : public QDialog {
public:
    explicit ProjectLoadingDialog(QWidget *parent = nullptr);

    bool updateProgress(const ProjectLoadProgress &progress);
    void setCancellable(bool cancellable);
    void updateEditorProgress(const QString &step, int percent,
                              const QString &detail = QString());
    bool wasCancelled() const noexcept { return cancelled_; }

private:
    QLabel *step_ = nullptr;
    QLabel *file_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *cancel_ = nullptr;
    bool cancelled_ = false;
};

} // namespace fxm::editor
