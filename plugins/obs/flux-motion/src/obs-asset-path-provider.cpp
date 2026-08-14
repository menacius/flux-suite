#include "obs-asset-path-provider.h"

#include <obs-module.h>
#include <util/bmem.h>

namespace fxm::obs_plugin {

std::string ObsAssetPathProvider::resolve_asset_path(
    std::string_view relative_path) const
{
    const std::string relative(relative_path);
    char *path = obs_module_file(relative.c_str());
    if (!path)
        return {};
    std::string resolved(path);
    bfree(path);
    return resolved;
}

ObsAssetPathProvider &obs_asset_path_provider() noexcept
{
    static ObsAssetPathProvider provider;
    return provider;
}

} // namespace fxm::obs_plugin
