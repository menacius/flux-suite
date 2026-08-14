#pragma once

#include <vector>

namespace flux::rendering {

struct MotionBlurSamplingRequest final {
    int configuredSamples = 8;
    double travelPixels = 0.0;
    double shutterStartSeconds = 0.0;
    double shutterEndSeconds = 0.0;
    bool imageLike = false;
    bool sourceChangesDuringShutter = false;
    bool realtime = false;
    int targetWidth = 1920;
    int targetHeight = 1080;
};

struct MotionBlurSamplingPlan final {
    std::vector<double> sampleTimes;
    int sampleCap = 2;
    bool renderSharpFrameOnly = false;
};

/* Shared temporal policy used by the GPU and compatibility render paths.
 * Samples are midpoint-distributed over the clamped shutter. The authored
 * sample count is the quality target; adaptive density fills fast-motion gaps
 * without allowing expensive changing-source rerenders to grow unchecked. */
MotionBlurSamplingPlan makeMotionBlurSamplingPlan(
    const MotionBlurSamplingRequest &request);

int motionBlurQualitySampleCount(int configuredSamples,
                                 double travelPixels,
                                 double samplesPerPixel,
                                 int sampleCap);

} // namespace flux::rendering
