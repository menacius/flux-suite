#pragma once

#include <QString>
#include <QVector>

enum class TitleLogLevel {
    Off = 0,
    Error = 1,
    Warning = 2,
    Info = 3,
    Debug = 4,
    Trace = 5
};

struct TitleLogCategory {
    QString key;
    QString display_name;
    QString description;
    QString group;
    bool default_enabled = true;
};

namespace TitleLogger {

void startSession();
void endSession();
QString currentSessionFilePath();
bool relocateCurrentSession(const QString &directory);
QVector<TitleLogCategory> categories();
bool categoryEnabled(const QString &category);
bool wouldLog(TitleLogLevel level, const char *category);

void log(TitleLogLevel level, const char *category, const QString &message);
/* Writes a message after the caller has already checked wouldLog(). This keeps
 * high-frequency macros from performing the settings/category lookup twice. */
void logPrepared(TitleLogLevel level, const char *category, const QString &message);
void refreshConfiguration();
void error(const char *category, const QString &message);
void warning(const char *category, const QString &message);
void info(const char *category, const QString &message);
void debug(const char *category, const QString &message);
void trace(const char *category, const QString &message);
QString levelName(TitleLogLevel level);

}

#define FXM_LOG_ERROR(category, message)                                      \
    do {                                                                      \
        if (::TitleLogger::wouldLog(TitleLogLevel::Error, category))          \
            ::TitleLogger::logPrepared(TitleLogLevel::Error, category, message); \
    } while (false)
#define FXM_LOG_WARNING(category, message)                                    \
    do {                                                                      \
        if (::TitleLogger::wouldLog(TitleLogLevel::Warning, category))        \
            ::TitleLogger::logPrepared(TitleLogLevel::Warning, category, message); \
    } while (false)
#define FXM_LOG_INFO(category, message)                                       \
    do {                                                                      \
        if (::TitleLogger::wouldLog(TitleLogLevel::Info, category))           \
            ::TitleLogger::logPrepared(TitleLogLevel::Info, category, message); \
    } while (false)
#define FXM_LOG_DEBUG(category, message)                                      \
    do {                                                                      \
        if (::TitleLogger::wouldLog(TitleLogLevel::Debug, category))          \
            ::TitleLogger::logPrepared(TitleLogLevel::Debug, category, message); \
    } while (false)
#define FXM_LOG_TRACE(category, message)                                      \
    do {                                                                      \
        if (::TitleLogger::wouldLog(TitleLogLevel::Trace, category))          \
            ::TitleLogger::logPrepared(TitleLogLevel::Trace, category, message); \
    } while (false)
