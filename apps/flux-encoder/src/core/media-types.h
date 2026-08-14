#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace flux {

enum class JobStatus {
    Pending,
    Validating,
    WaitingForResources,
    Preparing,
    Rendering,
    Encoding,
    Multiplexing,
    Paused,
    Completed,
    CompletedWithWarnings,
    Failed,
    Cancelled,
    Interrupted
};

QString jobStatusName(JobStatus status);
JobStatus jobStatusFromName(const QString &name);

struct ProfileCategory {
    QString uuid;
    QString parentUuid;
    QString name;
    bool builtIn = false;
    int sortOrder = 0;

    QJsonObject toJson() const;
    static ProfileCategory fromJson(const QJsonObject &json);
};

struct RenderProfile {
    QString uuid;
    QString name;
    QString description;
    QString categoryUuid;
    QString parentProfileUuid;
    int revision = 1;
    bool builtIn = false;
    bool favorite = false;
    bool enabled = true;
    QString unavailableReason;

    QString container = QStringLiteral("mp4");
    QString extension = QStringLiteral("mp4");
    QString videoEncoder = QStringLiteral("libx264");
    QString audioEncoder = QStringLiteral("aac");
    QString hardwareBackend = QStringLiteral("software");
    QString fallbackVideoEncoder = QStringLiteral("libx264");
    bool allowSoftwareFallback = true;

    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    int videoBitrateKbps = 0;
    int maxVideoBitrateKbps = 0;
    int audioBitrateKbps = 192;
    int quality = 19;
    QString pixelFormat = QStringLiteral("yuv420p");
    QString encoderPreset = QStringLiteral("medium");
    QString fieldOrder = QStringLiteral("progressive");
    QString aspectRatio = QStringLiteral("1:1");
    QString codecProfile;
    QString codecLevel;
    int sampleRate = 0;
    int audioChannels = 0;
    bool normalizeLoudness = false;
    bool truePeakLimit = false;
    bool writeMetadata = true;
    QString metadataMode = QStringLiteral("embed");
    QString metadataPreservation = QStringLiteral("all");
    bool metadataIncludeMarkers = true;
    QString metadataTemplate = QStringLiteral("all");
    QJsonObject metadataFields;
    bool fastStart = false;
    bool preserveTimecode = true;
    bool burnCaptions = false;
    bool videoLimiter = false;
    bool burnTimecode = false;
    QString timecodeStart = QStringLiteral("00:00:00:00");
    QString timecodePosition = QStringLiteral("bottom-center");
    int timecodeFontSize = 42;
    int timecodeOpacityPercent = 100;
    int timecodeBackgroundOpacityPercent = 55;
    qint64 trimStartMs = 0;
    qint64 trimEndMs = 0;
    QStringList inputArguments;
    QStringList videoArguments;
    QStringList audioArguments;
    QStringList muxerArguments;

    QJsonObject toJson() const;
    static RenderProfile fromJson(const QJsonObject &json);
};

struct QueueJob {
    QString uuid;
    QString sourcePath;
    QString sourceType = QStringLiteral("media-file");
    QString providerId;
    QJsonObject sourceDescriptor;
    QString outputPath;
    QString profileUuid;
    int profileRevision = 1;
    RenderProfile profileSnapshot;
    QJsonObject overrides;
    JobStatus status = JobStatus::Pending;
    double progress = 0.0;
    qint64 durationMs = 0;
    qint64 currentTimeMs = 0;
    qint64 etaSeconds = -1;
    QString speed;
    QString errorMessage;
    int priority = 0;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
    QDateTime updatedAt = QDateTime::currentDateTimeUtc();

    QJsonObject toJson() const;
    static QueueJob fromJson(const QJsonObject &json);
};

struct EncoderProbe {
    bool compiled = false;
    bool runtimeAvailable = false;
    QString reason;
};

struct FfmpegCapabilities {
    QString ffmpegPath;
    QString ffprobePath;
    QString version;
    QStringList hwaccels;
    QStringList encoders;
    QStringList filters;
    QMap<QString, EncoderProbe> encoderProbes;

    bool hasEncoder(const QString &encoder) const;
    bool encoderUsable(const QString &encoder) const;
    QString encoderReason(const QString &encoder) const;
    QJsonObject toJson() const;
};

QString createUuid();
QStringList jsonArrayToStringList(const QJsonArray &array);
QJsonArray stringListToJsonArray(const QStringList &list);

} // namespace flux
