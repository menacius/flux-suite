#pragma once

#include "core/media-types.h"

namespace flux {

class FfmpegCapabilityDetector final {
public:
    static FfmpegCapabilities detect(const QString &ffmpegPath = QString(),
                                     const QString &ffprobePath = QString(),
                                     bool runtimeProbe = true);
    static QString locateExecutable(const QString &name);

private:
    static QString run(const QString &program, const QStringList &arguments, int timeoutMs, int *exitCode = nullptr);
    static QStringList parseComponentList(const QString &text, bool encoderList);
    static QStringList parseFilterList(const QString &text);
    static EncoderProbe probeEncoder(const QString &ffmpeg, const QString &encoder);
};

} // namespace flux
