#pragma once

#include "translation-provider.h"

namespace fxm::obs_plugin {

class ObsTranslationProvider final : public ITranslationProvider {
public:
    ~ObsTranslationProvider() override = default;
    const char *translate(const char *key) const noexcept override;
};

ObsTranslationProvider &obs_translation_provider() noexcept;

} // namespace fxm::obs_plugin
