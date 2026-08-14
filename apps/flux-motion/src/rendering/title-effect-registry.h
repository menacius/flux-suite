#pragma once

#include "effects/effect-runtime.h"

#include <array>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

struct gs_effect;
typedef struct gs_effect gs_effect_t;

using TitleEffectDefinition = EffectDescriptor;

class TitleEffectRegistry {
public:
    TitleEffectRegistry() = default;
    ~TitleEffectRegistry();

    TitleEffectRegistry(const TitleEffectRegistry &) = delete;
    TitleEffectRegistry &operator=(const TitleEffectRegistry &) = delete;

    gs_effect_t *compile(LayerEffectType type);
    gs_effect_t *compile(const std::string &stable_id);
    gs_effect_t *find(LayerEffectType type) const;
    gs_effect_t *find(const std::string &stable_id) const;
    void reset();
    const char *last_error() const { return last_error_; }

    static const std::vector<TitleEffectDefinition> &definitions();
    static const TitleEffectDefinition *definition(LayerEffectType type);

private:
    static constexpr std::size_t kBuiltInEffectCount =
        static_cast<std::size_t>(LayerEffectType::TrimPaths) + 1;
    std::array<gs_effect_t *, kBuiltInEffectCount> builtins_{};
    std::unordered_map<std::string, gs_effect_t *> extensions_;
    mutable std::recursive_mutex mutex_;
    const char *last_error_ = nullptr;
};
