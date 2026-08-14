#include "obs-translation-provider.h"

#include <obs-module.h>

namespace fxm::obs_plugin {

const char *ObsTranslationProvider::translate(const char *key) const noexcept
{
    return obs_module_text(key ? key : "");
}

ObsTranslationProvider &obs_translation_provider() noexcept
{
    static ObsTranslationProvider provider;
    return provider;
}

} // namespace fxm::obs_plugin
