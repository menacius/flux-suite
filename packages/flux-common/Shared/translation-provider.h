#pragma once

namespace fxm {

class ITranslationProvider {
public:
    virtual ~ITranslationProvider() = default;
    virtual const char *translate(const char *key) const noexcept = 0;
};

} // namespace fxm
