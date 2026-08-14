#pragma once

#include "host-context-provider.h"

namespace fxm::obs_plugin {

class ObsHostContextProvider final : public IHostContextProvider {
public:
    std::string config_path(
        std::string_view relative_path) const override;
    std::string project_scope_name() const override;
};

ObsHostContextProvider &obs_host_context_provider() noexcept;

} // namespace fxm::obs_plugin
