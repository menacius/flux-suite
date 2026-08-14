#include "motion-blur-sampling.h"

#include <cassert>
#include <cmath>

int main()
{
    using namespace flux::rendering;
    MotionBlurSamplingRequest request;
    request.configuredSamples = 8;
    request.travelPixels = 40.0;
    request.shutterStartSeconds = 1.0;
    request.shutterEndSeconds = 1.0 + 1.0 / 60.0;
    request.imageLike = true;
    request.realtime = true;
    request.targetWidth = 3840;
    request.targetHeight = 2160;
    const MotionBlurSamplingPlan plan = makeMotionBlurSamplingPlan(request);
    assert(!plan.renderSharpFrameOnly);
    assert(plan.sampleTimes.size() == 32); // 4K real-time image cap
    assert(plan.sampleTimes.front() > request.shutterStartSeconds);
    assert(plan.sampleTimes.back() < request.shutterEndSeconds);

    request.travelPixels = 0.0;
    const MotionBlurSamplingPlan stationary = makeMotionBlurSamplingPlan(request);
    assert(stationary.renderSharpFrameOnly);
    assert(stationary.sampleTimes.empty());

    assert(motionBlurQualitySampleCount(8, 20.0, 2.25, 96) == 46);
    assert(motionBlurQualitySampleCount(8, 20.0, 1.5, 96) == 31);
    return 0;
}
