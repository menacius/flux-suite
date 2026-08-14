#include "worker/worker-application.h"

#include "core/ffmpeg-capabilities.h"
#include "core/job-builder.h"
#include "providers/render-provider-support.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>

namespace flux {

WorkerApplication::WorkerApplication(QObject *parent)
    : QObject(parent)
{
}

void WorkerApplication::emitJson(const QJsonObject &object) const
{
    QTextStream out(stdout);
    out << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
}

qint64 WorkerApplication::probeDuration(const QueueJob &job) const
{
    const QString ffprobe = FfmpegCapabilityDetector::locateExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty())
        return 0;

    QProcess process;
    process.start(ffprobe,
                  {QStringLiteral("-v"), QStringLiteral("error"),
                   QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                   QStringLiteral("-of"),
                   QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                   job.sourcePath});
    if (!process.waitForFinished(10000))
        return 0;

    bool ok = false;
    const double seconds = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&ok);
    return ok ? static_cast<qint64>(seconds * 1000.0) : 0;
}

bool WorkerApplication::encoderExists(const QString &encoder) const
{
    const QString ffmpeg = FfmpegCapabilityDetector::locateExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return false;

    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    if (!process.waitForFinished(8000))
        return false;

    const QString output = QString::fromUtf8(process.readAllStandardOutput() +
                                             process.readAllStandardError());
    return output.contains(QRegularExpression(
        QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(encoder))));
}

QStringList WorkerApplication::fallbackArguments(QStringList arguments,
                                                  const QString &fromEncoder,
                                                  const QString &toEncoder) const
{
    for (int i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("-c:v") &&
            arguments.at(i + 1) == fromEncoder) {
            arguments[i + 1] = toEncoder;
            break;
        }
    }

    const QSet<QString> removeWithValue = {
        QStringLiteral("-cq"), QStringLiteral("-global_quality"),
        QStringLiteral("-qp"), QStringLiteral("-rc"),
        QStringLiteral("-qp_i"), QStringLiteral("-qp_p"),
        QStringLiteral("-quality"), QStringLiteral("-vaapi_device"),
        QStringLiteral("-hwaccel"), QStringLiteral("-hwaccel_output_format")
    };

    for (int i = arguments.size() - 2; i >= 0; --i) {
        if (removeWithValue.contains(arguments.at(i))) {
            arguments.removeAt(i + 1);
            arguments.removeAt(i);
        }
    }

    for (int i = arguments.size() - 2; i >= 0; --i) {
        if (arguments.at(i) == QStringLiteral("-vf") &&
            (arguments.at(i + 1).contains(QStringLiteral("hwupload")) ||
             arguments.at(i + 1).contains(QStringLiteral("scale_cuda")) ||
             arguments.at(i + 1).contains(QStringLiteral("scale_vaapi")))) {
            arguments.removeAt(i + 1);
            arguments.removeAt(i);
        }
    }

    for (int i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("-preset"))
            arguments[i + 1] = QStringLiteral("medium");
    }

    for (int i = arguments.size() - 2; i >= 0; --i) {
        if (arguments.at(i) == QStringLiteral("-b:v") && arguments.at(i + 1) == QStringLiteral("0")) {
            arguments.removeAt(i + 1);
            arguments.removeAt(i);
        }
        if (arguments.at(i) == QStringLiteral("-crf")) {
            arguments.removeAt(i + 1);
            arguments.removeAt(i);
        }
    }

    const int outputIndex = arguments.size() - 1;
    arguments.insert(outputIndex, QStringLiteral("-crf"));
    arguments.insert(outputIndex + 1, QStringLiteral("19"));
    return arguments;
}

bool WorkerApplication::runFfmpegAttempt(const QString &ffmpeg,
                                         const QStringList &arguments,
                                         qint64 durationMs,
                                         QString *errorMessage,
                                         const QString &renderExecutable,
                                         const QStringList &renderArguments) const
{
    QProcess process;
    QProcess renderer;
    process.setProgram(ffmpeg);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    if (!renderExecutable.isEmpty()) {
        renderer.setProgram(renderExecutable);
        renderer.setArguments(renderArguments);
        renderer.setWorkingDirectory(QFileInfo(renderExecutable).absolutePath());
        renderer.setProcessChannelMode(QProcess::SeparateChannels);
    }
    process.start();

    if (!process.waitForStarted(5000)) {
        if (errorMessage)
            *errorMessage = process.errorString();
        return false;
    }
    if (!renderExecutable.isEmpty()) {
        renderer.start();
        if (!renderer.waitForStarted(10000)) {
            process.kill();
            process.waitForFinished();
            if (errorMessage)
                *errorMessage = QStringLiteral("Could not start the Flux Motion renderer: %1")
                                    .arg(renderer.errorString());
            return false;
        }
    }

    QByteArray outputBuffer;
    QString lastDiagnostic;
    QString rendererDiagnostic;
    QMap<QString, QString> progress;
    bool rendererFinalized = renderExecutable.isEmpty();
    int rendererExitCode = 0;
    QProcess::ExitStatus rendererExitStatus = QProcess::NormalExit;
    const auto forwardRendererFrames = [&]() {
        QByteArray frames = renderer.readAllStandardOutput();
        while (!frames.isEmpty()) {
            if (process.state() == QProcess::NotRunning)
                return false;
            const qint64 accepted = process.write(frames);
            if (accepted < 0)
                return false;
            frames.remove(0, static_cast<int>(accepted));
            while (process.bytesToWrite() > 16 * 1024 * 1024) {
                if (!process.waitForBytesWritten(1000) &&
                    process.state() == QProcess::NotRunning)
                    return false;
            }
        }
        return true;
    };

    while (process.state() != QProcess::NotRunning) {
        if (!renderExecutable.isEmpty() && !rendererFinalized) {
            renderer.waitForReadyRead(5);
            forwardRendererFrames();
            if (renderer.state() == QProcess::NotRunning) {
                renderer.waitForFinished(0);
                forwardRendererFrames();
                rendererExitCode = renderer.exitCode();
                rendererExitStatus = renderer.exitStatus();
                const QByteArray finalRendererDiagnostic =
                    renderer.readAllStandardError();
                if (!finalRendererDiagnostic.isEmpty())
                    rendererDiagnostic =
                        QString::fromUtf8(finalRendererDiagnostic).trimmed();
                /* We own FFmpeg's stdin in the manual pump. Close it exactly
                 * when the renderer has delivered its final raw frame. */
                process.closeWriteChannel();
                renderer.close();
                rendererFinalized = true;
            }
        }
        process.waitForReadyRead(renderExecutable.isEmpty() ? 150 : 5);
        outputBuffer += process.readAllStandardOutput();

        const QByteArray diagnosticData = process.readAllStandardError();
        if (!diagnosticData.isEmpty()) {
            const QString diagnostic = QString::fromUtf8(diagnosticData).trimmed();
            if (!diagnostic.isEmpty()) {
                lastDiagnostic = diagnostic;
                emitJson({{"type", "log"}, {"message", diagnostic}});
            }
        }
        if (!renderExecutable.isEmpty()) {
            const QByteArray renderDiagnosticData = renderer.readAllStandardError();
            if (!renderDiagnosticData.isEmpty()) {
                const QString diagnostic = QString::fromUtf8(renderDiagnosticData).trimmed();
                if (!diagnostic.isEmpty()) {
                    rendererDiagnostic = diagnostic;
                    emitJson({{"type", "log"}, {"message", diagnostic}});
                }
            }
        }

        int newline = -1;
        while ((newline = outputBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = outputBuffer.left(newline).trimmed();
            outputBuffer.remove(0, newline + 1);
            const int equals = line.indexOf('=');
            if (equals < 0)
                continue;

            const QString key = QString::fromUtf8(line.left(equals));
            progress[key] = QString::fromUtf8(line.mid(equals + 1));
            if (key != QStringLiteral("progress"))
                continue;

            bool timeOk = false;
            qint64 outTimeUs = progress.value(QStringLiteral("out_time_us")).toLongLong(&timeOk);
            if (!timeOk)
                outTimeUs = progress.value(QStringLiteral("out_time_ms")).toLongLong(&timeOk);
            const qint64 outTimeMs = timeOk ? outTimeUs / 1000 : 0;
            const double percent = durationMs > 0
                ? std::clamp(100.0 * double(outTimeMs) / double(durationMs), 0.0, 99.9)
                : 0.0;

            const QString speed = progress.value(QStringLiteral("speed"));
            QString numericSpeed = speed;
            numericSpeed.remove(QLatin1Char('x'));
            bool speedOk = false;
            const double speedValue = numericSpeed.toDouble(&speedOk);
            const qint64 etaSeconds = durationMs > outTimeMs && speedOk && speedValue > 0.001
                ? qRound64((durationMs - outTimeMs) / (1000.0 * speedValue))
                : -1;

            emitJson({{"type", "progress"},
                      {"percent", percent},
                      {"durationMs", double(durationMs)},
                      {"outTimeMs", double(outTimeMs)},
                      {"etaSeconds", double(etaSeconds)},
                      {"speed", speed},
                      {"frame", progress.value(QStringLiteral("frame"))}});
            progress.clear();
        }
        QCoreApplication::processEvents();
    }

    process.waitForFinished();
    if (!renderExecutable.isEmpty() && !rendererFinalized) {
        if (renderer.state() != QProcess::NotRunning) {
            if (!renderer.waitForFinished(5000)) {
                renderer.terminate();
                if (!renderer.waitForFinished(1500)) {
                    renderer.kill();
                    renderer.waitForFinished();
                }
            }
        }
        rendererExitCode = renderer.exitCode();
        rendererExitStatus = renderer.exitStatus();
        const QByteArray finalRendererDiagnostic = renderer.readAllStandardError();
        if (!finalRendererDiagnostic.isEmpty())
            rendererDiagnostic = QString::fromUtf8(finalRendererDiagnostic).trimmed();
        rendererFinalized = true;
    }
    const QByteArray finalDiagnostic = process.readAllStandardError();
    if (!finalDiagnostic.isEmpty())
        lastDiagnostic = QString::fromUtf8(finalDiagnostic).trimmed();

    const bool rendererSucceeded = renderExecutable.isEmpty() ||
        (rendererFinalized && rendererExitStatus == QProcess::NormalExit &&
         rendererExitCode == 0);
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
        rendererSucceeded)
        return true;

    if (errorMessage) {
        if (!rendererSucceeded) {
            const QString rendererError = rendererDiagnostic.isEmpty()
                ? QString::fromUtf8(renderer.readAllStandardError()).trimmed()
                : rendererDiagnostic;
            *errorMessage = rendererError.isEmpty()
                ? QStringLiteral("Flux Motion renderer exited with code %1").arg(rendererExitCode)
                : rendererError;
        } else {
            *errorMessage = lastDiagnostic.isEmpty()
                ? QStringLiteral("FFmpeg exited with code %1").arg(process.exitCode())
                : lastDiagnostic;
        }
    }
    return false;
}

int WorkerApplication::run(const QString &jobFile)
{
    QFile file(jobFile);
    if (!file.open(QIODevice::ReadOnly)) {
        emitJson({{"type", "error"}, {"message", file.errorString()}});
        return 2;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        emitJson({{"type", "error"}, {"message", "Invalid job JSON"}});
        return 3;
    }

    QueueJob job = QueueJob::fromJson(document.object());
    QString validationError;
    if (!JobBuilder::validate(job, &validationError)) {
        emitJson({{"type", "error"}, {"message", validationError}});
        return 4;
    }

    const QString ffmpeg = FfmpegCapabilityDetector::locateExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        emitJson({{"type", "error"}, {"message", "FFmpeg executable was not found"}});
        return 5;
    }

    RenderProviderSourceFormat renderFormat;
    QString renderExecutable;
    QStringList renderArguments;
    QStringList inputOverride;
    if (job.sourceType == QStringLiteral("render-provider")) {
        renderFormat = readRenderProviderSourceFormat(job);
        if (renderFormat.width <= 0 || renderFormat.height <= 0 ||
            renderFormat.frameRate <= 0.0) {
            emitJson({{"type", "error"},
                      {"message", "The Flux Motion project format could not be read"}});
            return 5;
        }
        renderExecutable = locateRenderProviderExecutable(job.providerId);
        if (renderExecutable.isEmpty()) {
            emitJson({{"type", "error"},
                      {"message", "The Flux Motion headless render provider is not installed"}});
            return 5;
        }
        const qint64 startMs = qMax<qint64>(0, job.profileSnapshot.trimStartMs);
        const qint64 sourceDuration = renderFormat.durationMs > 0
            ? renderFormat.durationMs : job.durationMs;
        const qint64 requestedDuration = job.profileSnapshot.trimEndMs > startMs
            ? job.profileSnapshot.trimEndMs - startMs
            : qMax<qint64>(0, sourceDuration - startMs);
        renderArguments = renderProviderVideoArguments(
            job, renderExecutable, startMs, requestedDuration,
            renderFormat.frameRate);
        inputOverride = {
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-pixel_format"), QStringLiteral("bgra"),
            QStringLiteral("-video_size"), QStringLiteral("%1x%2")
                .arg(renderFormat.width).arg(renderFormat.height),
            QStringLiteral("-framerate"), QString::number(renderFormat.frameRate, 'f', 6),
            QStringLiteral("-i"), QStringLiteral("pipe:0")};
    }

    std::unique_ptr<QTemporaryDir> renderedAudioDirectory;
    int audioInputIndex = -1;
    const QJsonObject renderOptions =
        job.sourceDescriptor.value(QStringLiteral("renderOptions")).toObject();
    const bool providerHasAudio = !renderOptions.contains(QStringLiteral("hasAudio")) ||
        renderOptions.value(QStringLiteral("hasAudio")).toBool();
    if (!renderExecutable.isEmpty() && providerHasAudio &&
        !job.profileSnapshot.audioEncoder.isEmpty()) {
        renderedAudioDirectory = std::make_unique<QTemporaryDir>();
        if (!renderedAudioDirectory->isValid()) {
            emitJson({{"type", "error"},
                      {"message", "Could not create temporary audio render storage"}});
            return 5;
        }
        const QString audioPath = renderedAudioDirectory->filePath(
            QStringLiteral("flux-motion-audio.wav"));
        const qint64 startMs = qMax<qint64>(0, job.profileSnapshot.trimStartMs);
        const qint64 sourceDuration = renderFormat.durationMs > 0
            ? renderFormat.durationMs : job.durationMs;
        const qint64 requestedDuration = job.profileSnapshot.trimEndMs > startMs
            ? job.profileSnapshot.trimEndMs - startMs
            : qMax<qint64>(1, sourceDuration - startMs);
        const QStringList audioArguments = renderProviderAudioArguments(
            job, renderExecutable, audioPath, startMs, requestedDuration,
            job.profileSnapshot.sampleRate > 0
                ? job.profileSnapshot.sampleRate : 48000);
        emitJson({{"type", "log"}, {"message", "Rendering Flux Motion audio"}});
        QProcess audioRenderer;
        audioRenderer.setProgram(renderExecutable);
        audioRenderer.setArguments(audioArguments);
        audioRenderer.start();
        if (!audioRenderer.waitForStarted(15000) ||
            !audioRenderer.waitForFinished(300000)) {
            audioRenderer.kill();
            audioRenderer.waitForFinished();
            emitJson({{"type", "error"},
                      {"message", "Flux Motion audio renderer did not finish"}});
            return 5;
        }
        const QString audioDiagnostics = QString::fromUtf8(
            audioRenderer.readAllStandardError()).trimmed();
        if (audioRenderer.exitStatus() != QProcess::NormalExit ||
            audioRenderer.exitCode() != 0 || !QFileInfo::exists(audioPath)) {
            emitJson({{"type", "error"},
                      {"message", audioDiagnostics.isEmpty()
                          ? QStringLiteral("Flux Motion audio rendering failed")
                          : audioDiagnostics.right(1000)}});
            return 5;
        }
        inputOverride << QStringLiteral("-i") << audioPath;
        audioInputIndex = 1;
    }

    const qint64 durationMs = job.durationMs > 0 ? job.durationMs
        : renderFormat.durationMs > 0 ? renderFormat.durationMs : probeDuration(job);
    const QString temporaryOutput = JobBuilder::temporaryOutputPath(job.outputPath);
    QFile::remove(temporaryOutput);

    QString effectiveEncoder;
    QStringList arguments = JobBuilder::ffmpegArguments(
        job, temporaryOutput, &effectiveEncoder, inputOverride, audioInputIndex);
    const QString fallbackEncoder = job.profileSnapshot.fallbackVideoEncoder;
    const bool fallbackAllowed = job.profileSnapshot.allowSoftwareFallback &&
                                 !fallbackEncoder.isEmpty() &&
                                 fallbackEncoder != effectiveEncoder;

    if (!encoderExists(effectiveEncoder)) {
        if (!fallbackAllowed || !encoderExists(fallbackEncoder)) {
            emitJson({{"type", "error"},
                      {"message", QStringLiteral("Encoder %1 is unavailable").arg(effectiveEncoder)}});
            return 6;
        }
        emitJson({{"type", "log"},
                  {"message", QStringLiteral("%1 is unavailable; falling back to %2")
                                  .arg(effectiveEncoder, fallbackEncoder)}});
        arguments = fallbackArguments(arguments, effectiveEncoder, fallbackEncoder);
        effectiveEncoder = fallbackEncoder;
    }

    QString encodeError;
    bool success = runFfmpegAttempt(ffmpeg, arguments, durationMs, &encodeError,
                                    renderExecutable, renderArguments);
    if (!success && fallbackAllowed && effectiveEncoder != fallbackEncoder &&
        encoderExists(fallbackEncoder)) {
        QFile::remove(temporaryOutput);
        emitJson({{"type", "log"},
                  {"message", QStringLiteral("Hardware encode failed; retrying with %1")
                                  .arg(fallbackEncoder)}});
        arguments = fallbackArguments(arguments, effectiveEncoder, fallbackEncoder);
        effectiveEncoder = fallbackEncoder;
        encodeError.clear();
        success = runFfmpegAttempt(ffmpeg, arguments, durationMs, &encodeError,
                                   renderExecutable, renderArguments);
    }

    if (!success) {
        QFile::remove(temporaryOutput);
        emitJson({{"type", "error"},
                  {"message", encodeError.isEmpty()
                                  ? QStringLiteral("FFmpeg encoding failed")
                                  : encodeError}});
        return 8;
    }

    QFile::remove(job.outputPath);
    if (!QFile::rename(temporaryOutput, job.outputPath)) {
        emitJson({{"type", "error"},
                  {"message", "Could not move temporary output to final path"}});
        return 9;
    }

    emitJson({{"type", "complete"},
              {"output", job.outputPath},
              {"encoder", effectiveEncoder}});
    return 0;
}

} // namespace flux
