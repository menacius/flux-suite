#include "project-loading-dialog.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace fxm::editor {

ProjectLoadingDialog::ProjectLoadingDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Opening Flux Motion Project"));
    setWindowModality(Qt::WindowModal);
    setModal(false);
    setMinimumWidth(440);
    setSizeGripEnabled(false);

    auto *layout = new QVBoxLayout(this);
    step_ = new QLabel(QStringLiteral("Reading project"), this);
    QFont font = step_->font();
    font.setBold(true);
    step_->setFont(font);
    file_ = new QLabel(this);
    file_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    file_->setWordWrap(true);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    cancel_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_->setAutoDefault(false);
    connect(cancel_, &QPushButton::clicked, this, [this]() {
        cancelled_ = true;
        cancel_->setEnabled(false);
        cancel_->setText(QStringLiteral("Cancelling…"));
    });
    layout->addWidget(step_);
    layout->addWidget(file_);
    layout->addWidget(progress_);
    layout->addWidget(cancel_, 0, Qt::AlignRight);
}

void ProjectLoadingDialog::setCancellable(bool cancellable)
{
    cancel_->setVisible(cancellable);
    setWindowFlag(Qt::WindowCloseButtonHint, cancellable);
}

void ProjectLoadingDialog::updateEditorProgress(const QString &step,
                                                int percent,
                                                const QString &detail)
{
    step_->setText(step);
    file_->setText(detail);
    file_->setToolTip(detail);
    if (percent >= 0) {
        progress_->setRange(0, 100);
        progress_->setValue(std::clamp(percent, 0, 100));
    } else {
        progress_->setRange(0, 0);
    }
    show();
    raise();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 30);
}

bool ProjectLoadingDialog::updateProgress(
    const ProjectLoadProgress &state)
{
    step_->setText(QString::fromStdString(state.step));
    const QString path = QString::fromStdString(state.file);
    file_->setText(path.isEmpty() ? QString() : QFileInfo(path).fileName());
    file_->setToolTip(path);
    if (state.total > 0) {
        progress_->setRange(0, 100);
        progress_->setValue(static_cast<int>(std::clamp<std::size_t>(
            state.completed * 100 / state.total, 0, 100)));
    } else {
        progress_->setRange(0, 0);
    }
    show();
    raise();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    return !cancelled_;
}

} // namespace fxm::editor
