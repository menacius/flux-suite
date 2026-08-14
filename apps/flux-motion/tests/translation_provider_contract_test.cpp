#include "title-localization.h"

#include <iostream>
#include <string>

namespace {

class TestTranslationProvider final : public fxm::ITranslationProvider {
public:
    const char *translate(const char *key) const noexcept override
    {
        return key && std::string(key) == "known" ? "translated" : "";
    }
};

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "translation provider failure: " << message << '\n';
    return false;
}

} // namespace

int main()
{
    bool ok = true;
    fxm_set_translation_provider(nullptr);
    ok &= expect(std::string(fxm_tr_c("fallback")) == "fallback",
                 "missing provider returns the key");
    ok &= expect(std::string(fxm_tr_c(nullptr)).empty(),
                 "null key returns an empty string");

    TestTranslationProvider provider;
    fxm_set_translation_provider(&provider);
    ok &= expect(std::string(fxm_tr_c("known")) == "translated",
                 "registered provider supplies a translation");
    ok &= expect(std::string(fxm_tr_c("missing")) == "missing",
                 "empty provider result falls back to the key");
    fxm_set_translation_provider(nullptr);
    return ok ? 0 : 1;
}
