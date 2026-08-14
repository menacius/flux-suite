#include "frame-rate-provider.h"

#include <cmath>
#include <iostream>

namespace {

class TestFrameRateProvider final : public fxm::IFrameRateProvider {
public:
    explicit TestFrameRateProvider(double value) : value_(value) {}

    double frame_rate() const noexcept override
    {
        return value_;
    }
    void set_document_frame_rate(double value) const noexcept override
    {
        value_ = value;
    }

private:
    mutable double value_ = 30.0;
};

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "frame-rate provider failure: " << message << '\n';
    return false;
}

} // namespace

int main()
{
    bool ok = true;
    fxm::set_frame_rate_provider(nullptr);
    ok &= expect(std::abs(fxm::current_frame_rate() - 30.0) < 0.000001,
                 "missing provider keeps the 30 fps fallback");

    TestFrameRateProvider provider(59.94);
    fxm::set_frame_rate_provider(&provider);
    ok &= expect(std::abs(fxm::current_frame_rate() - 59.94) < 0.000001,
                 "registered provider supplies the current frame rate");
    fxm::set_document_frame_rate(23.976);
    ok &= expect(std::abs(fxm::current_frame_rate() - 23.976) < 0.000001,
                 "document frame rate is forwarded to mutable providers");

    TestFrameRateProvider invalid_provider(0.0);
    fxm::set_frame_rate_provider(&invalid_provider);
    ok &= expect(std::abs(fxm::current_frame_rate(25.0) - 25.0) < 0.000001,
                 "invalid provider result uses the requested fallback");
    fxm::set_frame_rate_provider(nullptr);
    return ok ? 0 : 1;
}
