#include "timecode-spinbox.h"

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "timecode spinbox failure: " << message << '\n';
    return false;
}

} // namespace

int main()
{
    bool ok = true;
    double seconds = 0.0;
    ok &= expect(
        TimecodeSpinBox::parse_timecode(
            QStringLiteral("00:01:02:15"), 30.0, &seconds) &&
            std::abs(seconds - 62.5) < 0.000001,
        "explicit 30 fps timecode parses unchanged");
    ok &= expect(
        TimecodeSpinBox::parse_timecode(
            QStringLiteral("1800"), 30.0, &seconds) &&
            std::abs(seconds - 18.0) < 0.000001,
        "compact integer timecode parses unchanged");
    ok &= expect(
        !TimecodeSpinBox::parse_timecode(
            QStringLiteral("00:00:00:30"), 30.0, &seconds),
        "out-of-range frame field remains invalid");
    return ok ? 0 : 1;
}
