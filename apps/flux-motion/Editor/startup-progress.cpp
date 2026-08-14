#include "startup-progress.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <algorithm>
#include <utility>

namespace fxm::editor {

void StartupProgress::registerStage(QString id, QString message, int weight)
{
    if (id.trimmed().isEmpty() || findStage(id))
        return;
    stages_.push_back(Stage{std::move(id), std::move(message),
                            std::max(1, weight), false});
}

void StartupProgress::beginStage(const QString &id, const QString &message)
{
    Stage *stage = findStage(id);
    if (!stage) {
        registerStage(id, message.isEmpty() ? id : message);
        stage = findStage(id);
    }
    if (!stage)
        return;
    state_.task = message.isEmpty() ? stage->message : message;
    state_.percent = totalWeight() > 0
        ? (completedWeight() * 100) / totalWeight() : 0;
    state_.determinate = totalWeight() > 0;
    state_.ready = false;
    publish();
}

void StartupProgress::completeStage(const QString &id, const QString &message)
{
    Stage *stage = findStage(id);
    if (!stage) {
        registerStage(id, message.isEmpty() ? id : message);
        stage = findStage(id);
    }
    if (!stage)
        return;
    stage->complete = true;
    if (!message.isEmpty())
        state_.task = message;
    state_.percent = totalWeight() > 0
        ? (completedWeight() * 100) / totalWeight() : 100;
    state_.determinate = totalWeight() > 0;
    publish();
}

void StartupProgress::reportIndeterminate(const QString &message)
{
    state_.task = message;
    state_.determinate = false;
    state_.ready = false;
    publish();
}

void StartupProgress::setReady(const QString &message)
{
    for (Stage &stage : stages_)
        stage.complete = true;
    state_.task = message;
    state_.percent = 100;
    state_.determinate = true;
    state_.ready = true;
    publish();
}

void StartupProgress::setUpdateCallback(UpdateCallback callback)
{
    update_callback_ = std::move(callback);
    publish();
}

StartupProgress::Stage *StartupProgress::findStage(const QString &id)
{
    const auto found = std::find_if(stages_.begin(), stages_.end(),
        [&id](const Stage &stage) { return stage.id == id; });
    return found == stages_.end() ? nullptr : &*found;
}

int StartupProgress::completedWeight() const
{
    int result = 0;
    for (const Stage &stage : stages_)
        if (stage.complete)
            result += stage.weight;
    return result;
}

int StartupProgress::totalWeight() const
{
    int result = 0;
    for (const Stage &stage : stages_)
        result += stage.weight;
    return result;
}

void StartupProgress::publish()
{
    if (update_callback_)
        update_callback_(state_);

    /* Startup currently includes libraries that require the GUI thread. Give
     * Qt a bounded repaint pass between stages so the progress window remains
     * live without allowing input to re-enter partially initialized code. */
    if (QCoreApplication::instance())
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
}

} // namespace fxm::editor
