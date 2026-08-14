#include "obs-host-context-provider.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>

namespace fxm::obs_plugin {

std::string ObsHostContextProvider::config_path(
    std::string_view relative_path) const
{
    const std::string relative(relative_path);
    char *path = obs_module_config_path(relative.c_str());
    if (!path)
        return {};
    std::string result(path);
    bfree(path);
    return result;
}

std::string ObsHostContextProvider::project_scope_name() const
{
    char *name = obs_frontend_get_current_scene_collection();
    if (!name)
        return {};
    std::string result(name);
    bfree(name);
    return result;
}

ObsHostContextProvider &obs_host_context_provider() noexcept
{
    static ObsHostContextProvider provider;
    return provider;
}

} // namespace fxm::obs_plugin
