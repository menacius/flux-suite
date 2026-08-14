#include "core/media-types.h"

namespace flux {

QString createUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString jobStatusName(JobStatus status)
{
    switch (status) {
    case JobStatus::Pending: return QStringLiteral("Pending");
    case JobStatus::Validating: return QStringLiteral("Validating");
    case JobStatus::WaitingForResources: return QStringLiteral("Waiting for resources");
    case JobStatus::Preparing: return QStringLiteral("Preparing");
    case JobStatus::Rendering: return QStringLiteral("Rendering");
    case JobStatus::Encoding: return QStringLiteral("Encoding");
    case JobStatus::Multiplexing: return QStringLiteral("Multiplexing");
    case JobStatus::Paused: return QStringLiteral("Paused");
    case JobStatus::Completed: return QStringLiteral("Completed");
    case JobStatus::CompletedWithWarnings: return QStringLiteral("Completed with warnings");
    case JobStatus::Failed: return QStringLiteral("Failed");
    case JobStatus::Cancelled: return QStringLiteral("Cancelled");
    case JobStatus::Interrupted: return QStringLiteral("Interrupted");
    }
    return QStringLiteral("Pending");
}

JobStatus jobStatusFromName(const QString &name)
{
    static const QMap<QString, JobStatus> values = {
        {QStringLiteral("Pending"), JobStatus::Pending},
        {QStringLiteral("Validating"), JobStatus::Validating},
        {QStringLiteral("Waiting for resources"), JobStatus::WaitingForResources},
        {QStringLiteral("Preparing"), JobStatus::Preparing},
        {QStringLiteral("Rendering"), JobStatus::Rendering},
        {QStringLiteral("Encoding"), JobStatus::Encoding},
        {QStringLiteral("Multiplexing"), JobStatus::Multiplexing},
        {QStringLiteral("Paused"), JobStatus::Paused},
        {QStringLiteral("Completed"), JobStatus::Completed},
        {QStringLiteral("Completed with warnings"), JobStatus::CompletedWithWarnings},
        {QStringLiteral("Failed"), JobStatus::Failed},
        {QStringLiteral("Cancelled"), JobStatus::Cancelled},
        {QStringLiteral("Interrupted"), JobStatus::Interrupted}
    };
    return values.value(name, JobStatus::Pending);
}

QStringList jsonArrayToStringList(const QJsonArray &array)
{
    QStringList list;
    for (const QJsonValue &value : array)
        list.push_back(value.toString());
    return list;
}

QJsonArray stringListToJsonArray(const QStringList &list)
{
    QJsonArray array;
    for (const QString &value : list)
        array.push_back(value);
    return array;
}

QJsonObject ProfileCategory::toJson() const
{
    return {{"uuid", uuid}, {"parentUuid", parentUuid}, {"name", name},
            {"builtIn", builtIn}, {"sortOrder", sortOrder}};
}

ProfileCategory ProfileCategory::fromJson(const QJsonObject &json)
{
    ProfileCategory value;
    value.uuid = json.value("uuid").toString();
    value.parentUuid = json.value("parentUuid").toString();
    value.name = json.value("name").toString();
    value.builtIn = json.value("builtIn").toBool();
    value.sortOrder = json.value("sortOrder").toInt();
    return value;
}

QJsonObject RenderProfile::toJson() const
{
    return {
        {"schemaVersion", 1}, {"uuid", uuid}, {"name", name},
        {"description", description}, {"categoryUuid", categoryUuid},
        {"parentProfileUuid", parentProfileUuid}, {"revision", revision},
        {"builtIn", builtIn}, {"favorite", favorite}, {"enabled", enabled},
        {"unavailableReason", unavailableReason}, {"container", container},
        {"extension", extension}, {"videoEncoder", videoEncoder},
        {"audioEncoder", audioEncoder}, {"hardwareBackend", hardwareBackend},
        {"fallbackVideoEncoder", fallbackVideoEncoder},
        {"allowSoftwareFallback", allowSoftwareFallback},
        {"width", width}, {"height", height}, {"frameRate", frameRate},
        {"videoBitrateKbps", videoBitrateKbps},
        {"maxVideoBitrateKbps", maxVideoBitrateKbps},
        {"audioBitrateKbps", audioBitrateKbps}, {"quality", quality},
        {"pixelFormat", pixelFormat}, {"encoderPreset", encoderPreset},
        {"fieldOrder", fieldOrder}, {"aspectRatio", aspectRatio},
        {"codecProfile", codecProfile}, {"codecLevel", codecLevel},
        {"sampleRate", sampleRate}, {"audioChannels", audioChannels},
        {"normalizeLoudness", normalizeLoudness}, {"truePeakLimit", truePeakLimit},
        {"writeMetadata", writeMetadata}, {"metadataMode", metadataMode},
        {"metadataPreservation", metadataPreservation}, {"metadataIncludeMarkers", metadataIncludeMarkers},
        {"metadataTemplate", metadataTemplate}, {"metadataFields", metadataFields}, {"fastStart", fastStart},
        {"preserveTimecode", preserveTimecode}, {"burnCaptions", burnCaptions},
        {"videoLimiter", videoLimiter}, {"burnTimecode", burnTimecode},
        {"timecodeStart", timecodeStart}, {"timecodePosition", timecodePosition},
        {"timecodeFontSize", timecodeFontSize}, {"timecodeOpacityPercent", timecodeOpacityPercent},
        {"timecodeBackgroundOpacityPercent", timecodeBackgroundOpacityPercent},
        {"trimStartMs", static_cast<double>(trimStartMs)},
        {"trimEndMs", static_cast<double>(trimEndMs)},
        {"inputArguments", stringListToJsonArray(inputArguments)},
        {"videoArguments", stringListToJsonArray(videoArguments)},
        {"audioArguments", stringListToJsonArray(audioArguments)},
        {"muxerArguments", stringListToJsonArray(muxerArguments)}
    };
}

RenderProfile RenderProfile::fromJson(const QJsonObject &json)
{
    RenderProfile value;
    value.uuid = json.value("uuid").toString();
    value.name = json.value("name").toString();
    value.description = json.value("description").toString();
    value.categoryUuid = json.value("categoryUuid").toString();
    value.parentProfileUuid = json.value("parentProfileUuid").toString();
    value.revision = json.value("revision").toInt(1);
    value.builtIn = json.value("builtIn").toBool();
    value.favorite = json.value("favorite").toBool();
    value.enabled = json.value("enabled").toBool(true);
    value.unavailableReason = json.value("unavailableReason").toString();
    value.container = json.value("container").toString("mp4");
    value.extension = json.value("extension").toString("mp4");
    value.videoEncoder = json.value("videoEncoder").toString("libx264");
    value.audioEncoder = json.value("audioEncoder").toString("aac");
    value.hardwareBackend = json.value("hardwareBackend").toString("software");
    value.fallbackVideoEncoder = json.value("fallbackVideoEncoder").toString("libx264");
    value.allowSoftwareFallback = json.value("allowSoftwareFallback").toBool(true);
    value.width = json.value("width").toInt();
    value.height = json.value("height").toInt();
    value.frameRate = json.value("frameRate").toDouble();
    value.videoBitrateKbps = json.value("videoBitrateKbps").toInt();
    value.maxVideoBitrateKbps = json.value("maxVideoBitrateKbps").toInt();
    value.audioBitrateKbps = json.value("audioBitrateKbps").toInt(192);
    value.quality = json.value("quality").toInt(19);
    value.pixelFormat = json.value("pixelFormat").toString("yuv420p");
    value.encoderPreset = json.value("encoderPreset").toString("medium");
    value.fieldOrder = json.value("fieldOrder").toString("progressive");
    value.aspectRatio = json.value("aspectRatio").toString("1:1");
    value.codecProfile = json.value("codecProfile").toString();
    value.codecLevel = json.value("codecLevel").toString();
    value.sampleRate = json.value("sampleRate").toInt();
    value.audioChannels = json.value("audioChannels").toInt();
    value.normalizeLoudness = json.value("normalizeLoudness").toBool(false);
    value.truePeakLimit = json.value("truePeakLimit").toBool(false);
    value.writeMetadata = json.value("writeMetadata").toBool(true);
    value.metadataMode = json.value("metadataMode").toString(value.writeMetadata ? "embed" : "none");
    value.metadataPreservation = json.value("metadataPreservation").toString("all");
    value.metadataIncludeMarkers = json.value("metadataIncludeMarkers").toBool(true);
    value.metadataTemplate = json.value("metadataTemplate").toString("all");
    value.metadataFields = json.value("metadataFields").toObject();
    value.fastStart = json.value("fastStart").toBool(false);
    value.preserveTimecode = json.value("preserveTimecode").toBool(true);
    value.burnCaptions = json.value("burnCaptions").toBool(false);
    value.videoLimiter = json.value("videoLimiter").toBool(false);
    value.burnTimecode = json.value("burnTimecode").toBool(false);
    value.timecodeStart = json.value("timecodeStart").toString("00:00:00:00");
    value.timecodePosition = json.value("timecodePosition").toString("bottom-center");
    value.timecodeFontSize = json.value("timecodeFontSize").toInt(42);
    value.timecodeOpacityPercent = json.value("timecodeOpacityPercent").toInt(100);
    value.timecodeBackgroundOpacityPercent = json.value("timecodeBackgroundOpacityPercent").toInt(55);
    value.trimStartMs = static_cast<qint64>(json.value("trimStartMs").toDouble());
    value.trimEndMs = static_cast<qint64>(json.value("trimEndMs").toDouble());
    value.inputArguments = jsonArrayToStringList(json.value("inputArguments").toArray());
    value.videoArguments = jsonArrayToStringList(json.value("videoArguments").toArray());
    value.audioArguments = jsonArrayToStringList(json.value("audioArguments").toArray());
    value.muxerArguments = jsonArrayToStringList(json.value("muxerArguments").toArray());
    return value;
}

QJsonObject QueueJob::toJson() const
{
    return {
        {"schemaVersion", 2}, {"uuid", uuid}, {"sourcePath", sourcePath},
        {"sourceType", sourceType}, {"providerId", providerId}, {"sourceDescriptor", sourceDescriptor},
        {"outputPath", outputPath}, {"profileUuid", profileUuid},
        {"profileRevision", profileRevision}, {"profileSnapshot", profileSnapshot.toJson()},
        {"overrides", overrides}, {"status", jobStatusName(status)},
        {"progress", progress}, {"durationMs", static_cast<double>(durationMs)},
        {"currentTimeMs", static_cast<double>(currentTimeMs)}, {"etaSeconds", static_cast<double>(etaSeconds)}, {"speed", speed},
        {"errorMessage", errorMessage}, {"priority", priority},
        {"createdAt", createdAt.toString(Qt::ISODateWithMs)},
        {"updatedAt", updatedAt.toString(Qt::ISODateWithMs)}
    };
}

QueueJob QueueJob::fromJson(const QJsonObject &json)
{
    QueueJob value;
    value.uuid = json.value("uuid").toString();
    value.sourcePath = json.value("sourcePath").toString();
    value.sourceType = json.value("sourceType").toString("media-file");
    value.providerId = json.value("providerId").toString();
    value.sourceDescriptor = json.value("sourceDescriptor").toObject();
    value.outputPath = json.value("outputPath").toString();
    value.profileUuid = json.value("profileUuid").toString();
    value.profileRevision = json.value("profileRevision").toInt(1);
    value.profileSnapshot = RenderProfile::fromJson(json.value("profileSnapshot").toObject());
    value.overrides = json.value("overrides").toObject();
    value.status = jobStatusFromName(json.value("status").toString());
    value.progress = json.value("progress").toDouble();
    value.durationMs = static_cast<qint64>(json.value("durationMs").toDouble());
    value.currentTimeMs = static_cast<qint64>(json.value("currentTimeMs").toDouble());
    value.etaSeconds = static_cast<qint64>(json.value("etaSeconds").toDouble(-1));
    value.speed = json.value("speed").toString();
    value.errorMessage = json.value("errorMessage").toString();
    value.priority = json.value("priority").toInt();
    value.createdAt = QDateTime::fromString(json.value("createdAt").toString(), Qt::ISODateWithMs);
    value.updatedAt = QDateTime::fromString(json.value("updatedAt").toString(), Qt::ISODateWithMs);
    if (!value.createdAt.isValid()) value.createdAt = QDateTime::currentDateTimeUtc();
    if (!value.updatedAt.isValid()) value.updatedAt = value.createdAt;
    return value;
}

bool FfmpegCapabilities::hasEncoder(const QString &encoder) const
{
    return encoders.contains(encoder);
}

bool FfmpegCapabilities::encoderUsable(const QString &encoder) const
{
    const auto it = encoderProbes.constFind(encoder);
    return it != encoderProbes.cend() ? it->runtimeAvailable : hasEncoder(encoder);
}

QString FfmpegCapabilities::encoderReason(const QString &encoder) const
{
    const auto it = encoderProbes.constFind(encoder);
    if (it != encoderProbes.cend()) return it->reason;
    return hasEncoder(encoder) ? QString() : QStringLiteral("Encoder is not included in this FFmpeg build");
}

QJsonObject FfmpegCapabilities::toJson() const
{
    QJsonObject probes;
    for (auto it = encoderProbes.cbegin(); it != encoderProbes.cend(); ++it)
        probes.insert(it.key(), QJsonObject{{"compiled", it->compiled},
                                            {"runtimeAvailable", it->runtimeAvailable},
                                            {"reason", it->reason}});
    return {{"ffmpegPath", ffmpegPath}, {"ffprobePath", ffprobePath},
            {"version", version}, {"hwaccels", stringListToJsonArray(hwaccels)},
            {"encoders", stringListToJsonArray(encoders)},
            {"filters", stringListToJsonArray(filters)}, {"encoderProbes", probes}};
}

} // namespace flux
