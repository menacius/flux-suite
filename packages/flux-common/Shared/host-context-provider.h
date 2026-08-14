#pragma once

#include <string>
#include <string_view>

namespace fxm {

class IHostContextProvider {
public:
    virtual ~IHostContextProvider() = default;
    virtual std::string config_path(
        std::string_view relative_path) const = 0;
    virtual std::string project_scope_name() const = 0;
};

void set_host_context_provider(IHostContextProvider *provider) noexcept;
IHostContextProvider *host_context_provider() noexcept;

} // namespace fxm
