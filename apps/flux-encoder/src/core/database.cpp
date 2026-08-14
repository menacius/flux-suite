#include "core/database.h"

#include <QDir>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace flux {

Database::Database() : connectionName_(QStringLiteral("flux-encoder-%1").arg(createUuid())) {}
Database::~Database()
{
    if (db_.isValid()) db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool Database::open(const QString &path)
{
    databasePath_ = path;
    if (databasePath_.isEmpty()) {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(directory);
        databasePath_ = QDir(directory).filePath(QStringLiteral("flux-encoder.sqlite"));
    }
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(databasePath_);
    if (!db_.open()) {
        lastError_ = db_.lastError().text();
        return false;
    }
    execute(QStringLiteral("PRAGMA journal_mode=WAL"));
    execute(QStringLiteral("PRAGMA foreign_keys=ON"));
    execute(QStringLiteral("PRAGMA busy_timeout=5000"));
    return createSchema();
}

bool Database::execute(const QString &sql) const
{
    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::createSchema()
{
    return execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS profile_categories("
        "uuid TEXT PRIMARY KEY,parent_uuid TEXT,name TEXT NOT NULL,built_in INTEGER NOT NULL,sort_order INTEGER NOT NULL,json TEXT NOT NULL)")) &&
        execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS render_profiles("
        "uuid TEXT PRIMARY KEY,category_uuid TEXT,name TEXT NOT NULL,built_in INTEGER NOT NULL,revision INTEGER NOT NULL,json TEXT NOT NULL,updated_at TEXT NOT NULL)")) &&
        execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS queue_jobs("
        "uuid TEXT PRIMARY KEY,status TEXT NOT NULL,priority INTEGER NOT NULL,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,json TEXT NOT NULL)"));
}

QList<ProfileCategory> Database::loadCategories() const
{
    QList<ProfileCategory> result;
    QSqlQuery query(db_);
    query.exec(QStringLiteral("SELECT json FROM profile_categories ORDER BY sort_order,name"));
    while (query.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toByteArray());
        if (doc.isObject()) result.push_back(ProfileCategory::fromJson(doc.object()));
    }
    return result;
}

QList<RenderProfile> Database::loadProfiles() const
{
    QList<RenderProfile> result;
    QSqlQuery query(db_);
    query.exec(QStringLiteral("SELECT json FROM render_profiles ORDER BY name"));
    while (query.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toByteArray());
        if (doc.isObject()) result.push_back(RenderProfile::fromJson(doc.object()));
    }
    return result;
}

QList<QueueJob> Database::loadJobs() const
{
    QList<QueueJob> result;
    QSqlQuery query(db_);
    query.exec(QStringLiteral("SELECT json FROM queue_jobs ORDER BY priority DESC,created_at"));
    while (query.next()) {
        const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toByteArray());
        if (doc.isObject()) result.push_back(QueueJob::fromJson(doc.object()));
    }
    return result;
}

bool Database::saveCategory(const ProfileCategory &category)
{
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO profile_categories(uuid,parent_uuid,name,built_in,sort_order,json) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(category.uuid); query.addBindValue(category.parentUuid); query.addBindValue(category.name);
    query.addBindValue(category.builtIn ? 1 : 0); query.addBindValue(category.sortOrder);
    query.addBindValue(QString::fromUtf8(QJsonDocument(category.toJson()).toJson(QJsonDocument::Compact)));
    if (!query.exec()) { lastError_ = query.lastError().text(); return false; }
    return true;
}

bool Database::saveProfile(const RenderProfile &profile)
{
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO render_profiles(uuid,category_uuid,name,built_in,revision,json,updated_at) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(profile.uuid); query.addBindValue(profile.categoryUuid); query.addBindValue(profile.name);
    query.addBindValue(profile.builtIn ? 1 : 0); query.addBindValue(profile.revision);
    query.addBindValue(QString::fromUtf8(QJsonDocument(profile.toJson()).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) { lastError_ = query.lastError().text(); return false; }
    return true;
}

bool Database::deleteProfile(const QString &uuid)
{
    QSqlQuery query(db_); query.prepare(QStringLiteral("DELETE FROM render_profiles WHERE uuid=? AND built_in=0"));
    query.addBindValue(uuid); if (!query.exec()) { lastError_ = query.lastError().text(); return false; } return true;
}

bool Database::deleteCategory(const QString &uuid)
{
    QSqlQuery profiles(db_); profiles.prepare(QStringLiteral("SELECT COUNT(*) FROM render_profiles WHERE category_uuid=?"));
    profiles.addBindValue(uuid); if (!profiles.exec() || !profiles.next() || profiles.value(0).toInt() > 0) {
        lastError_ = QStringLiteral("Category is not empty"); return false;
    }
    QSqlQuery children(db_); children.prepare(QStringLiteral("SELECT COUNT(*) FROM profile_categories WHERE parent_uuid=?"));
    children.addBindValue(uuid); if (!children.exec() || !children.next() || children.value(0).toInt() > 0) {
        lastError_ = QStringLiteral("Category contains subcategories"); return false;
    }
    QSqlQuery query(db_); query.prepare(QStringLiteral("DELETE FROM profile_categories WHERE uuid=? AND built_in=0"));
    query.addBindValue(uuid); if (!query.exec()) { lastError_ = query.lastError().text(); return false; } return true;
}

bool Database::saveJob(const QueueJob &job)
{
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO queue_jobs(uuid,status,priority,created_at,updated_at,json) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(job.uuid); query.addBindValue(jobStatusName(job.status)); query.addBindValue(job.priority);
    query.addBindValue(job.createdAt.toString(Qt::ISODateWithMs)); query.addBindValue(job.updatedAt.toString(Qt::ISODateWithMs));
    query.addBindValue(QString::fromUtf8(QJsonDocument(job.toJson()).toJson(QJsonDocument::Compact)));
    if (!query.exec()) { lastError_ = query.lastError().text(); return false; } return true;
}

bool Database::deleteJob(const QString &uuid)
{
    QSqlQuery query(db_); query.prepare(QStringLiteral("DELETE FROM queue_jobs WHERE uuid=?")); query.addBindValue(uuid);
    if (!query.exec()) { lastError_ = query.lastError().text(); return false; } return true;
}

bool Database::clearCompletedJobs()
{
    return execute(QStringLiteral("DELETE FROM queue_jobs WHERE status IN ('Completed','Completed with warnings','Cancelled')"));
}

bool Database::markRunningJobsInterrupted()
{
    const QList<QueueJob> jobs = loadJobs();
    bool ok = true;
    for (QueueJob job : jobs) {
        if (job.status == JobStatus::Validating || job.status == JobStatus::Preparing ||
            job.status == JobStatus::Rendering || job.status == JobStatus::Encoding ||
            job.status == JobStatus::Multiplexing) {
            job.status = JobStatus::Interrupted;
            job.errorMessage = QStringLiteral("The previous application session ended while this job was active.");
            job.updatedAt = QDateTime::currentDateTimeUtc();
            ok = saveJob(job) && ok;
        }
    }
    return ok;
}

} // namespace flux
