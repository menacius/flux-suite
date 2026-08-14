#include "external-data-log.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "External data logging failure: " << message << '\n';
    return false;
}

struct Entry {
    ExternalDataLogLevel level;
    std::string component;
    std::string message;
};

} // namespace

int main()
{
    bool ok = true;
    std::vector<Entry> entries;
    ExternalDataLog::set_sink(
        [&entries](ExternalDataLogLevel level, const std::string &component,
                   const std::string &message) {
            entries.push_back({level, component, message});
        },
        [](ExternalDataLogLevel level) {
            return static_cast<int>(level) <=
                   static_cast<int>(ExternalDataLogLevel::Debug);
        });

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "Provider\nHTTP", []() {
            return std::string("refresh\ncompleted");
        });
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Trace, "Provider", []() {
            return std::string("must not be constructed");
        });

    ok &= expect(entries.size() == 1, "level probe suppresses trace entries");
    ok &= expect(entries.front().component == "Provider HTTP",
                 "component control characters are normalized");
    ok &= expect(entries.front().message == "refresh completed",
                 "message control characters are normalized");

    const std::string url = ExternalDataLog::safe_location(
        "https://user:password@example.com/api/live?token=secret#fragment");
    ok &= expect(url == "https://example.com/api/live",
                 "URL user info, query and fragment are redacted");

    const std::string secret = "super-secret-live-value";
    const std::string summary = ExternalDataLog::value_summary(
        ExternalDataValue::string(secret));
    ok &= expect(summary.find(secret) == std::string::npos,
                 "raw external value is not present in summary");
    ok &= expect(summary.find("fingerprint=") != std::string::npos,
                 "value summary includes diagnostic fingerprint");
    ok &= expect(summary.find("bytes=23") != std::string::npos,
                 "value summary includes payload size");

    ExternalDataLog::clear_sink();
    ok &= expect(!ExternalDataLog::enabled(ExternalDataLogLevel::Error),
                 "cleared sink disables logging");
    return ok ? 0 : 1;
}
