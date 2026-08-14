#include "providers/render-provider-support.h"

#include "providers/provider-catalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

namespace flux {
namespace {

QStringList commonArguments(const QueueJob &job, const QString &executable,
                            qint64 startMs, qint64 durationMs)
{
    const QJsonObject options =
        job.sourceDescriptor.value(QStringLiteral("renderOptions")).toObject();
    QStringList arguments;
    const QString projectFile =
        options.value(QStringLiteral("projectFilePath")).toString();
    if (!projectFile.isEmpty()) {
        arguments << QStringLiteral("--project-file") << projectFile;
    } else {
        arguments << QStringLiteral("--config-root")
                  << options.value(QStringLiteral("projectStorePath")).toString()
                  << QStringLiteral("--project-scope")
                  << options.value(QStringLiteral("projectScope")).toString()
                  << QStringLiteral("--title-id")
                  << job.sourceDescriptor.value(QStringLiteral("compositionId")).toString();
    }
    arguments << QStringLiteral("--render-start-ms")
              << QString::number(qMax<qint64>(0, startMs))
              << QStringLiteral("--render-duration-ms")
              << QString::number(qMax<qint64>(1, durationMs))
              << QStringLiteral("--obs-bin-root")
              << QFileInfo(executable).absolutePath();
    const QString dataRoot = QDir(QFileInfo(executable).absolutePath())
                                 .filePath(QStringLiteral("data"));
    if (QDir(dataRoot).exists())
        arguments << QStringLiteral("--data-root") << dataRoot;
    return arguments;
}

} // namespace

RenderProviderSourceFormat readRenderProviderSourceFormat(const QueueJob &job)
{
    RenderProviderSourceFormat format;
    const QJsonObject options =
        job.sourceDescriptor.value(QStringLiteral("renderOptions")).toObject();
    format.width = options.value(QStringLiteral("sourceWidth")).toInt();
    format.height = options.value(QStringLiteral("sourceHeight")).toInt();
    format.frameRate = options.value(QStringLiteral("sourceFrameRate")).toDouble();
    format.durationMs = options.value(QStringLiteral("sourceDurationMs")).toInteger();
    if (format.width > 0 && format.height > 0 && format.frameRate > 0.0)
        return format;
    QFile file(job.sourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return format;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray titles = document.isArray()
        ? document.array() : document.object().value(QStringLiteral("titles")).toArray();
    const QString composition =
        job.sourceDescriptor.value(QStringLiteral("compositionId")).toString();
    for (const QJsonValue &value : titles) {
        const QJsonObject title = value.toObject();
        if (title.value(QStringLiteral("id")).toString() != composition)
            continue;
        format.width = title.value(QStringLiteral("width")).toInt();
        format.height = title.value(QStringLiteral("height")).toInt();
        format.frameRate = title.value(QStringLiteral("frame_rate")).toDouble();
        format.durationMs = qRound64(
            title.value(QStringLiteral("duration")).toDouble() * 1000.0);
        break;
    }
    return format;
}

QString locateRenderProviderExecutable(const QString &providerId)
{
    const QString override =
        qEnvironmentVariable("FLUX_MOTION_RENDERER_EXECUTABLE").trimmed();
    if (!override.isEmpty() && QFileInfo(override).isExecutable())
        return QFileInfo(override).absoluteFilePath();
    ProviderCatalog catalog;
    catalog.discover();
    const ProviderManifest *provider = catalog.providerById(providerId);
    return provider && provider->usable ? provider->executablePath : QString();
}

RenderProviderProjectInspection inspectRenderProviderProject(
    const QString &providerId, const QString &projectPath, QString *error)
{
    RenderProviderProjectInspection inspection;
    const QString executable = locateRenderProviderExecutable(providerId);
    if (executable.isEmpty()) {
        if (error)
            *error = QStringLiteral("Flux Motion renderer is not installed");
        return inspection;
    }
    QStringList arguments{QStringLiteral("--inspect-project-file"), projectPath,
                          QStringLiteral("--obs-bin-root"),
                          QFileInfo(executable).absolutePath()};
    const QString dataRoot = QDir(QFileInfo(executable).absolutePath())
                                 .filePath(QStringLiteral("data"));
    if (QDir(dataRoot).exists())
        arguments << QStringLiteral("--data-root") << dataRoot;
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(15000) || !process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        if (error)
            *error = QStringLiteral("Flux Motion project inspection timed out");
        return inspection;
    }
    const QByteArray output = process.readAllStandardOutput().trimmed();
    const QString diagnostics =
        QString::fromUtf8(process.readAllStandardError()).trimmed();
    const QJsonDocument document = QJsonDocument::fromJson(output);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !document.isObject()) {
        if (error)
            *error = diagnostics.isEmpty()
                ? QStringLiteral("Flux Motion could not open this project")
                : diagnostics.right(1000);
        return inspection;
    }
    const QJsonObject object = document.object();
    inspection.compositionId = object.value(QStringLiteral("id")).toString();
    inspection.name = object.value(QStringLiteral("name")).toString();
    inspection.format.width = object.value(QStringLiteral("width")).toInt();
    inspection.format.height = object.value(QStringLiteral("height")).toInt();
    inspection.format.frameRate = object.value(QStringLiteral("frameRate")).toDouble();
    inspection.format.durationMs = object.value(QStringLiteral("durationMs")).toInteger();
    inspection.hasAudio = object.value(QStringLiteral("hasAudio")).toBool();
    inspection.valid = !inspection.compositionId.isEmpty() &&
        inspection.format.width > 0 && inspection.format.height > 0 &&
        inspection.format.frameRate > 0.0 && inspection.format.durationMs > 0;
    if (!inspection.valid && error)
        *error = QStringLiteral("Flux Motion project metadata is incomplete");
    return inspection;
}

QStringList renderProviderVideoArguments(const QueueJob &job,
                                         const QString &executable,
                                         qint64 startMs,
                                         qint64 durationMs,
                                         double frameRate)
{
    QStringList arguments{QStringLiteral("--flux-encoder-render-raw")};
    arguments << commonArguments(job, executable, startMs, durationMs)
              << QStringLiteral("--render-frame-rate")
              << QString::number(frameRate, 'f', 6);
    return arguments;
}

QStringList renderProviderAudioArguments(const QueueJob &job,
                                         const QString &executable,
                                         const QString &outputPath,
                                         qint64 startMs,
                                         qint64 durationMs,
                                         int sampleRate)
{
    QStringList arguments{QStringLiteral("--flux-encoder-render-audio")};
    arguments << commonArguments(job, executable, startMs, durationMs)
              << QStringLiteral("--render-audio-output") << outputPath
              << QStringLiteral("--render-sample-rate")
              << QString::number(qBound(8000, sampleRate, 192000));
    return arguments;
}

} // namespace flux
