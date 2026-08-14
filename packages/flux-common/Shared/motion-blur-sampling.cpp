#include "motion-blur-sampling.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flux::rendering {
namespace {

int realtimeCap(const MotionBlurSamplingRequest &request)
{
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(std::max(1, request.targetWidth)) *
        static_cast<std::uint64_t>(std::max(1, request.targetHeight));
    if (request.sourceChangesDuringShutter)
        return pixels >= 7000000u ? 2 : pixels >= 3000000u ? 3 : 4;
    if (pixels >= 7000000u)
        return request.imageLike ? 32 : 20;
    if (pixels >= 3000000u)
        return request.imageLike ? 48 : 28;
    return request.imageLike ? 64 : 40;
}

} // namespace

int motionBlurQualitySampleCount(int configuredSamples,
                                 double travelPixels,
                                 double samplesPerPixel,
                                 int sampleCap)
{
    sampleCap = std::max(2, sampleCap);
    const int authored = std::clamp(configuredSamples, 2, sampleCap);
    const int adaptive = std::max(2, static_cast<int>(std::ceil(
        std::max(0.0, travelPixels) * std::max(0.0, samplesPerPixel))) + 1);
    return std::clamp(std::max(authored, adaptive), 2, sampleCap);
}

MotionBlurSamplingPlan makeMotionBlurSamplingPlan(
    const MotionBlurSamplingRequest &request)
{
    MotionBlurSamplingPlan plan;
    const double shutterDuration = std::max(
        0.0, request.shutterEndSeconds - request.shutterStartSeconds);
    if (shutterDuration <= 1e-9 || request.travelPixels < 0.01) {
        plan.renderSharpFrameOnly = true;
        return plan;
    }

    int cap = request.imageLike ? 96 : 48;
    if (request.realtime)
        cap = std::min(cap, realtimeCap(request));
    plan.sampleCap = std::max(2, cap);

    /* One midpoint sample per output pixel of travel is sufficient for vector
     * and SDF artwork under bilinear filtering. Image/video needs a denser
     * 1.5 samples/pixel to prevent separated silhouettes. The former 2.25
     * image density generated 50% more full-canvas draws with no resolvable
     * coverage improvement at output resolution. */
    /* Match the resident-GPU transform path: bilinear/SDF vector coverage is
     * continuous at 0.8 samples per pixel, while sharp bitmap silhouettes use
     * the denser 1.5 sample policy. */
    const double density = request.imageLike ? 1.5 : 0.8;
    const int count = motionBlurQualitySampleCount(request.configuredSamples,
        request.travelPixels, density, plan.sampleCap);

    plan.sampleTimes.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const double fraction = (static_cast<double>(index) + 0.5) /
                                static_cast<double>(count);
        plan.sampleTimes.push_back(request.shutterStartSeconds +
            shutterDuration * fraction);
    }
    return plan;
}

} // namespace flux::rendering
