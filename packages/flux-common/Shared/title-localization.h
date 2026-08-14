#pragma once

#include "translation-provider.h"

#include <QString>

void fxm_set_translation_provider(
    const fxm::ITranslationProvider *provider) noexcept;
const char *fxm_tr_c(const char *key);

static inline QString fxm_tr(const char *key)
{
    return QString::fromUtf8(fxm_tr_c(key));
}
