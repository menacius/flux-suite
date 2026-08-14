#pragma once

#include <QString>

#include <functional>
#include <vector>

namespace fxm::editor {

struct StartupProgressState {
    QString task;
    int percent = 0;
    bool determinate = true;
    bool ready = false;
};

/*
 * Application-wide startup status channel. Startup modules register named
 * stages and report through this class; presentation code observes state and
 * never needs to know which modules participate in startup.
 */
class StartupProgress final {
public:
    using UpdateCallback = std::function<void(const StartupProgressState &)>;

    void registerStage(QString id, QString message, int weight = 1);
    void beginStage(const QString &id, const QString &message = {});
    void completeStage(const QString &id, const QString &message = {});
    void reportIndeterminate(const QString &message);
    void setReady(const QString &message = QStringLiteral("Ready."));
    void setUpdateCallback(UpdateCallback callback);

    StartupProgressState state() const { return state_; }

private:
    struct Stage {
        QString id;
        QString message;
        int weight = 1;
        bool complete = false;
    };

    Stage *findStage(const QString &id);
    void publish();
    int completedWeight() const;
    int totalWeight() const;

    std::vector<Stage> stages_;
    StartupProgressState state_;
    UpdateCallback update_callback_;
};

} // namespace fxm::editor
