#include "frame-rate-provider.h"

#include <atomic>
#include <cmath>

namespace fxm {
namespace {

std::atomic<const IFrameRateProvider *> g_frame_rate_provider{nullptr};

} // namespace

void set_frame_rate_provider(
    const IFrameRateProvider *provider) noexcept
{
    g_frame_rate_provider.store(provider, std::memory_order_release);
}

void set_document_frame_rate(double frame_rate) noexcept
{
    const IFrameRateProvider *provider =
        g_frame_rate_provider.load(std::memory_order_acquire);
    if (provider)
        provider->set_document_frame_rate(frame_rate);
}

double current_frame_rate(double fallback) noexcept
{
    const IFrameRateProvider *provider =
        g_frame_rate_provider.load(std::memory_order_acquire);
    if (provider) {
        const double configured = provider->frame_rate();
        if (std::isfinite(configured) && configured > 0.0)
            return configured;
    }
    return std::isfinite(fallback) && fallback > 0.0 ? fallback : 30.0;
}

} // namespace fxm
