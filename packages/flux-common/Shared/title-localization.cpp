#include "title-localization.h"

namespace {

const fxm::ITranslationProvider *g_translation_provider = nullptr;

} // namespace

void fxm_set_translation_provider(
    const fxm::ITranslationProvider *provider) noexcept
{
    g_translation_provider = provider;
}

const char *fxm_tr_c(const char *key)
{
    const char *fallback = key ? key : "";
    if (!g_translation_provider)
        return fallback;
    const char *translated = g_translation_provider->translate(fallback);
    return translated && *translated ? translated : fallback;
}
