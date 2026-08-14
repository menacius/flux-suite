#include "asset-path-provider.h"

#include <atomic>

namespace {
std::atomic<const fxm::IAssetPathProvider *> g_provider{nullptr};
}

namespace fxm {

void set_asset_path_provider(const IAssetPathProvider *provider) noexcept
{
    g_provider.store(provider, std::memory_order_release);
}

const IAssetPathProvider *asset_path_provider() noexcept
{
    return g_provider.load(std::memory_order_acquire);
}

std::string resolve_asset_path(std::string_view relative_path)
{
    const auto *provider = asset_path_provider();
    return provider ? provider->resolve_asset_path(relative_path)
                    : std::string();
}

} // namespace fxm
