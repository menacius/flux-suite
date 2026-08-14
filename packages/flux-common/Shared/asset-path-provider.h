#pragma once

#include <string>
#include <string_view>

namespace fxm {

class IAssetPathProvider {
public:
    virtual ~IAssetPathProvider() = default;
    virtual std::string resolve_asset_path(
        std::string_view relative_path) const = 0;
};

void set_asset_path_provider(
    const IAssetPathProvider *provider) noexcept;
const IAssetPathProvider *asset_path_provider() noexcept;
std::string resolve_asset_path(std::string_view relative_path);

} // namespace fxm
