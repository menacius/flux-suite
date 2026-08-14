#pragma once

namespace fxm {

class IFrameRateProvider {
public:
    virtual ~IFrameRateProvider() = default;
    virtual double frame_rate() const noexcept = 0;
    /* Standalone editors can use a document-authored preview cadence. Host
     * providers (for example OBS) may keep their output cadence authoritative
     * by retaining this default no-op implementation. */
    virtual void set_document_frame_rate(double) const noexcept {}
};

void set_frame_rate_provider(
    const IFrameRateProvider *provider) noexcept;
void set_document_frame_rate(double frame_rate) noexcept;
double current_frame_rate(double fallback = 30.0) noexcept;

} // namespace fxm
