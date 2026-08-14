#include "editor-host-interfaces.h"

#include <atomic>

namespace {
std::atomic<fxm::editor_host::IEditorHost *> g_editor_host{nullptr};
}

namespace fxm::editor_host {

void set_editor_host(IEditorHost *host) noexcept
{
    g_editor_host.store(host, std::memory_order_release);
}

IEditorHost *editor_host() noexcept
{
    return g_editor_host.load(std::memory_order_acquire);
}

} // namespace fxm::editor_host
