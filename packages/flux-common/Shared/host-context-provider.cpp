#include "host-context-provider.h"

#include <atomic>

namespace {
std::atomic<fxm::IHostContextProvider *> g_provider{nullptr};
}

namespace fxm {

void set_host_context_provider(IHostContextProvider *provider) noexcept
{
    g_provider.store(provider, std::memory_order_release);
}

IHostContextProvider *host_context_provider() noexcept
{
    return g_provider.load(std::memory_order_acquire);
}

} // namespace fxm
