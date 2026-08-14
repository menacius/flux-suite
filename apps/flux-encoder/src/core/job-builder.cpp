#include "core/job-builder.h"
#include <QDir>
#include <QFileInfo>

namespace flux {
namespace {
QString timecodePositionExpression(const QString &position)
{
    const QString x = position.endsWith(QStringLiteral("left"))
        ? QStringLiteral("20")
        : position.endsWith(QStringLiteral("right"))
            ? QStringLiteral("w-text_w-20")
            : QStringLiteral("(w-text_w)/2");
    const QString y = position.startsWith(QStringLiteral("top"))
        ? QStringLiteral("20")
        : position.startsWith(QStringLiteral("bottom"))
            ? QStringLiteral("h-text_h-20")
            : QStringLiteral("(h-text_h)/2");
    return QStringLiteral("x=%1:y=%2").arg(x, y);
}

QString escapedFilterValue(QString value)
{
    value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    value.replace(QStringLiteral(":"), QStringLiteral("\\:"));
    value.replace(QStringLiteral("'"), QStringLiteral("\\'"));
    return value;
}
} // namespace

QString JobBuilder::defaultOutputPath(const QString &source, const RenderProfile &profile)
{
    const QFileInfo info(source);
    return QDir(info.absolutePath()).filePath(info.completeBaseName() + QStringLiteral("_encoded.") + profile.extension);
}

QString JobBuilder::temporaryOutputPath(const QString &output)
{
    const QFileInfo info(output);
    return QDir(info.absolutePath()).filePath(QStringLiteral(".%1.flux-encoder-part.%2")
        .arg(info.completeBaseName(), info.suffix().isEmpty() ? QStringLiteral("tmp") : info.suffix()));
}

bool JobBuilder::validate(const QueueJob &job, QString *error)
{
    if (!QFileInfo::exists(job.sourcePath)) { if (error) *error = QStringLiteral("Source file does not exist"); return false; }
    if (job.outputPath.trimmed().isEmpty()) { if (error) *error = QStringLiteral("Output path is empty"); return false; }
    if (QFileInfo(job.sourcePath).absoluteFilePath() == QFileInfo(job.outputPath).absoluteFilePath()) {
        if (error) *error = QStringLiteral("Output path must differ from source path"); return false;
    }
    QDir outputDir = QFileInfo(job.outputPath).absoluteDir();
    if (!outputDir.exists() && !QDir().mkpath(outputDir.absolutePath())) {
        if (error) *error = QStringLiteral("Output directory cannot be created"); return false;
    }
    return true;
}

QStringList JobBuilder::ffmpegArguments(const QueueJob &job, const QString &temporaryOutput,
                                        QString *effectiveEncoder,
                                        const QStringList &inputOverride,
                                        int audioInputIndex)
{
    const RenderProfile &p = job.profileSnapshot;
    QString encoder = p.videoEncoder;
    if (effectiveEncoder) *effectiveEncoder = encoder;

    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-y"),
                     QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats")};
    if (inputOverride.isEmpty()) {
        args << p.inputArguments;
        if (p.trimStartMs > 0)
            args << QStringLiteral("-ss") << QString::number(p.trimStartMs / 1000.0, 'f', 3);
        args << QStringLiteral("-i") << job.sourcePath;
    } else {
        args << inputOverride;
    }
    if (p.trimEndMs > p.trimStartMs)
        args << QStringLiteral("-t") << QString::number((p.trimEndMs - p.trimStartMs) / 1000.0, 'f', 3);
    if (!p.videoEncoder.isEmpty())
        args << QStringLiteral("-map") << QStringLiteral("0:v:0?");
    else
        args << QStringLiteral("-vn");
    if (!p.audioEncoder.isEmpty())
        args << QStringLiteral("-map")
             << (audioInputIndex >= 0
                     ? QStringLiteral("%1:a:0?").arg(audioInputIndex)
                     : QStringLiteral("0:a?"));
    else
        args << QStringLiteral("-an");
    const bool exportMetadata = p.writeMetadata && p.metadataMode != QStringLiteral("none");
    if (exportMetadata && p.metadataPreservation != QStringLiteral("none"))
        args << QStringLiteral("-map_metadata") << QStringLiteral("0");
    else
        args << QStringLiteral("-map_metadata") << QStringLiteral("-1");
    args << QStringLiteral("-map_chapters")
         << (exportMetadata && p.metadataIncludeMarkers ? QStringLiteral("0") : QStringLiteral("-1"));
    if (exportMetadata) {
        for (auto it = p.metadataFields.constBegin(); it != p.metadataFields.constEnd(); ++it) {
            const QString value = it.value().toString().trimmed();
            if (!it.key().trimmed().isEmpty() && !value.isEmpty())
                args << QStringLiteral("-metadata") << QStringLiteral("%1=%2").arg(it.key(), value);
        }
    }

    QStringList filters;
    if (p.width > 0 && p.height > 0)
        filters << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2").arg(p.width).arg(p.height);
    if (p.videoLimiter)
        filters << QStringLiteral("limiter=min=0:max=1");
    if (p.burnCaptions && job.sourceType == QStringLiteral("media-file")) {
        QString escaped = job.sourcePath;
        escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        escaped.replace(QStringLiteral(":"), QStringLiteral("\\:"));
        escaped.replace(QStringLiteral("'"), QStringLiteral("\\'"));
        filters << QStringLiteral("subtitles='%1'").arg(escaped);
    }
    if (p.burnTimecode) {
        const double rate = p.frameRate > 0.0 ? p.frameRate : 25.0;
        const double foregroundOpacity = qBound(0, p.timecodeOpacityPercent, 100) / 100.0;
        const double backgroundOpacity = qBound(0, p.timecodeBackgroundOpacityPercent, 100) / 100.0;
        const QString start = escapedFilterValue(p.timecodeStart.isEmpty()
                                                     ? QStringLiteral("00:00:00:00")
                                                     : p.timecodeStart);
        const QString position = timecodePositionExpression(p.timecodePosition);
        filters << QStringLiteral("drawtext=font='Sans':timecode='%1':timecode_rate=%2:tc24hmax=1:%3:fontsize=%4:fontcolor=white@%5:box=1:boxcolor=black@%6:boxborderw=10")
                       .arg(start)
                       .arg(rate, 0, 'f', 3)
                       .arg(position)
                       .arg(qBound(12, p.timecodeFontSize, 240))
                       .arg(foregroundOpacity, 0, 'f', 2)
                       .arg(backgroundOpacity, 0, 'f', 2);
    }
    if (p.hardwareBackend == QStringLiteral("vaapi")) {
#ifdef Q_OS_LINUX
        args.prepend(QStringLiteral("/dev/dri/renderD128"));
        args.prepend(QStringLiteral("-vaapi_device"));
        filters << QStringLiteral("format=nv12,hwupload");
#endif
    }
    if (!filters.isEmpty()) args << QStringLiteral("-vf") << filters.join(',');
    if (p.frameRate > 0.0) args << QStringLiteral("-r") << QString::number(p.frameRate, 'f', 3);
    if (p.fieldOrder == QStringLiteral("upper")) args << QStringLiteral("-field_order") << QStringLiteral("tt");
    else if (p.fieldOrder == QStringLiteral("lower")) args << QStringLiteral("-field_order") << QStringLiteral("bb");
    if (!p.aspectRatio.isEmpty() && p.aspectRatio != QStringLiteral("1:1"))
        args << QStringLiteral("-aspect") << p.aspectRatio;
    if (!encoder.isEmpty()) args << QStringLiteral("-c:v") << encoder;
    if (!p.encoderPreset.isEmpty() &&
        p.hardwareBackend != QStringLiteral("amf") &&
        p.hardwareBackend != QStringLiteral("vaapi"))
        args << QStringLiteral("-preset") << p.encoderPreset;
    if (!p.pixelFormat.isEmpty() && p.hardwareBackend != QStringLiteral("vaapi"))
        args << QStringLiteral("-pix_fmt") << p.pixelFormat;
    if (!p.codecProfile.isEmpty()) args << QStringLiteral("-profile:v") << p.codecProfile;
    if (!p.codecLevel.isEmpty()) args << QStringLiteral("-level:v") << p.codecLevel;

    if (p.videoBitrateKbps > 0) {
        args << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(p.videoBitrateKbps);
        if (p.maxVideoBitrateKbps > 0) {
            args << QStringLiteral("-maxrate") << QStringLiteral("%1k").arg(p.maxVideoBitrateKbps);
            args << QStringLiteral("-bufsize") << QStringLiteral("%1k").arg(qMax(p.maxVideoBitrateKbps * 2, p.videoBitrateKbps * 2));
        }
    } else if (p.quality >= 0) {
        if (encoder.contains(QStringLiteral("nvenc"))) args << QStringLiteral("-cq") << QString::number(p.quality);
        else if (encoder.contains(QStringLiteral("qsv"))) args << QStringLiteral("-global_quality") << QString::number(p.quality);
        else if (encoder.contains(QStringLiteral("vaapi"))) args << QStringLiteral("-qp") << QString::number(p.quality);
        else if (encoder.contains(QStringLiteral("amf"))) args << QStringLiteral("-quality") << QStringLiteral("balanced") << QStringLiteral("-qp_i") << QString::number(p.quality) << QStringLiteral("-qp_p") << QString::number(p.quality);
        else args << QStringLiteral("-crf") << QString::number(p.quality);
    }
    args << p.videoArguments;

    if (p.audioEncoder.isEmpty()) args << QStringLiteral("-an");
    else {
        args << QStringLiteral("-c:a") << p.audioEncoder;
        if (p.audioBitrateKbps > 0 && !p.audioEncoder.startsWith(QStringLiteral("pcm_")))
            args << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(p.audioBitrateKbps);
        if (p.sampleRate > 0) args << QStringLiteral("-ar") << QString::number(p.sampleRate);
        if (p.audioChannels > 0) args << QStringLiteral("-ac") << QString::number(p.audioChannels);
        if (p.normalizeLoudness) {
            QString loudnorm = QStringLiteral("loudnorm=I=-16:LRA=11");
            if (p.truePeakLimit) loudnorm += QStringLiteral(":TP=-1.5");
            args << QStringLiteral("-af") << loudnorm;
        }
        args << p.audioArguments;
    }
    if (p.fastStart && (p.container == QStringLiteral("mp4") || p.container == QStringLiteral("mov")))
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    if (!p.preserveTimecode)
        args << QStringLiteral("-metadata") << QStringLiteral("timecode=");
    args << p.muxerArguments << temporaryOutput;
    return args;
}

} // namespace flux
