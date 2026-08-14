#pragma once

#include "core/media-types.h"
#include <QSqlDatabase>

namespace flux {

class Database final {
public:
    Database();
    ~Database();

    bool open(const QString &path = QString());
    QString lastError() const { return lastError_; }
    QString databasePath() const { return databasePath_; }

    QList<ProfileCategory> loadCategories() const;
    QList<RenderProfile> loadProfiles() const;
    QList<QueueJob> loadJobs() const;

    bool saveCategory(const ProfileCategory &category);
    bool deleteCategory(const QString &uuid);
    bool saveProfile(const RenderProfile &profile);
    bool deleteProfile(const QString &uuid);
    bool saveJob(const QueueJob &job);
    bool deleteJob(const QString &uuid);
    bool clearCompletedJobs();
    bool markRunningJobsInterrupted();

private:
    bool execute(const QString &sql) const;
    bool createSchema();
    QString connectionName_;
    QString databasePath_;
    mutable QString lastError_;
    QSqlDatabase db_;
};

} // namespace flux
