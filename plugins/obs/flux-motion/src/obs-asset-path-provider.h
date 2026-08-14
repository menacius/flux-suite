#pragma once

#include "asset-path-provider.h"

namespace fxm::obs_plugin {

class ObsAssetPathProvider final : public IAssetPathProvider {
public:
    ~ObsAssetPathProvider() override = default;
    std::string resolve_asset_path(
        std::string_view relative_path) const override;
};

ObsAssetPathProvider &obs_asset_path_provider() noexcept;

} // namespace fxm::obs_plugin
