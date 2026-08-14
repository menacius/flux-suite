#include "core/ffmpeg-capabilities.h"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace flux {

QString FfmpegCapabilityDetector::locateExecutable(const QString &name)
{
    const QString executableName =
#ifdef Q_OS_WIN
        name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            ? name : name + QStringLiteral(".exe");
#else
        name;
#endif
    const QFileInfo bundled(QDir(QCoreApplication::applicationDirPath())
                                .filePath(executableName));
    if (bundled.exists() && bundled.isFile())
        return bundled.absoluteFilePath();

    QString found = QStandardPaths::findExecutable(name);
#ifdef Q_OS_WIN
    if (found.isEmpty()) found = QStandardPaths::findExecutable(name + QStringLiteral(".exe"));
#endif
    return found;
}

QString FfmpegCapabilityDetector::run(const QString &program, const QStringList &arguments, int timeoutMs, int *exitCode)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        if (exitCode) *exitCode = -1;
        return process.errorString();
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill(); process.waitForFinished(1000);
        if (exitCode) *exitCode = -2;
        return QStringLiteral("Probe timed out");
    }
    if (exitCode) *exitCode = process.exitCode();
    return QString::fromUtf8(process.readAll());
}

QStringList FfmpegCapabilityDetector::parseComponentList(const QString &text, bool encoderList)
{
    QStringList result;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    QRegularExpression encoderPattern(QStringLiteral("^\\s*[A-Z.]{6}\\s+([A-Za-z0-9_]+)\\s"));
    QRegularExpression simplePattern(QStringLiteral("^\\s*([A-Za-z0-9_]+)\\s*$"));
    for (const QString &line : lines) {
        const auto match = (encoderList ? encoderPattern : simplePattern).match(line);
        if (match.hasMatch()) result.push_back(match.captured(1));
    }
    result.removeDuplicates(); result.sort();
    return result;
}


QStringList FfmpegCapabilityDetector::parseFilterList(const QString &text)
{
    QStringList result;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    const QRegularExpression pattern(
        QStringLiteral("^\\s*[TSC.]{3}\\s+([A-Za-z0-9_]+)\\s"));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = pattern.match(line);
        if (match.hasMatch())
            result.push_back(match.captured(1));
    }
    result.removeDuplicates();
    result.sort();
    return result;
}

EncoderProbe FfmpegCapabilityDetector::probeEncoder(const QString &ffmpeg, const QString &encoder)
{
    EncoderProbe probe; probe.compiled = true;
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error")};
    if (encoder.endsWith(QStringLiteral("_vaapi"))) {
#ifdef Q_OS_LINUX
        const QString device = QFileInfo::exists(QStringLiteral("/dev/dri/renderD128"))
                                   ? QStringLiteral("/dev/dri/renderD128") : QString();
        if (device.isEmpty()) { probe.reason = QStringLiteral("No VAAPI render device was found"); return probe; }
        args << QStringLiteral("-vaapi_device") << device;
#else
        probe.reason = QStringLiteral("VAAPI profiles are supported on Linux"); return probe;
#endif
    }
    args << QStringLiteral("-f") << QStringLiteral("lavfi")
         << QStringLiteral("-i") << QStringLiteral("color=size=64x64:rate=1:duration=0.05");
    if (encoder.endsWith(QStringLiteral("_vaapi")))
        args << QStringLiteral("-vf") << QStringLiteral("format=nv12,hwupload");
    args << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-c:v") << encoder << QStringLiteral("-f")
         << QStringLiteral("null") << QStringLiteral("-");
    int exitCode = -1;
    const QString output = run(ffmpeg, args, 12000, &exitCode);
    probe.runtimeAvailable = exitCode == 0;
    probe.reason = probe.runtimeAvailable ? QString() : output.trimmed();
    if (probe.reason.size() > 320) probe.reason = probe.reason.left(317) + QStringLiteral("...");
    return probe;
}

FfmpegCapabilities FfmpegCapabilityDetector::detect(const QString &requestedFfmpeg,
                                                     const QString &requestedFfprobe,
                                                     bool runtimeProbe)
{
    FfmpegCapabilities caps;
    caps.ffmpegPath = requestedFfmpeg.isEmpty() ? locateExecutable(QStringLiteral("ffmpeg")) : requestedFfmpeg;
    caps.ffprobePath = requestedFfprobe.isEmpty() ? locateExecutable(QStringLiteral("ffprobe")) : requestedFfprobe;
    if (caps.ffmpegPath.isEmpty()) return caps;

    const QString versionText = run(caps.ffmpegPath, {QStringLiteral("-version")}, 5000);
    caps.version = versionText.section('\n', 0, 0).trimmed();
    caps.encoders = parseComponentList(run(caps.ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")}, 8000), true);
    caps.hwaccels = parseComponentList(run(caps.ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-hwaccels")}, 8000), false);
    caps.filters = parseFilterList(run(caps.ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-filters")}, 8000));

    const QStringList hardwareEncoders = {
        QStringLiteral("h264_nvenc"), QStringLiteral("hevc_nvenc"), QStringLiteral("av1_nvenc"),
        QStringLiteral("h264_qsv"), QStringLiteral("hevc_qsv"), QStringLiteral("av1_qsv"),
        QStringLiteral("h264_amf"), QStringLiteral("hevc_amf"), QStringLiteral("av1_amf"),
        QStringLiteral("h264_vaapi"), QStringLiteral("hevc_vaapi"), QStringLiteral("av1_vaapi")
    };
    for (const QString &encoder : hardwareEncoders) {
        EncoderProbe probe;
        probe.compiled = caps.hasEncoder(encoder);
        if (!probe.compiled) probe.reason = QStringLiteral("Encoder is not included in this FFmpeg build");
        else if (runtimeProbe) probe = probeEncoder(caps.ffmpegPath, encoder);
        else probe.runtimeAvailable = true;
        caps.encoderProbes.insert(encoder, probe);
    }
    return caps;
}

} // namespace flux
