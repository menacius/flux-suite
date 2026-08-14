#include "system-memory.h"

#include <algorithm>
#include <climits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif

namespace fxm::system_memory {
namespace {
constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr std::uint64_t kFallbackPhysicalBytes = 8ull * 1024ull * kMiB;
}

std::uint64_t total_physical_bytes()
{
#ifdef _WIN32
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) && status.ullTotalPhys > 0)
        return static_cast<std::uint64_t>(status.ullTotalPhys);
#elif defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 && bytes > 0)
        return bytes;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        const auto page_count = static_cast<std::uint64_t>(pages);
        const auto bytes_per_page = static_cast<std::uint64_t>(page_size);
        if (page_count <= UINT64_MAX / bytes_per_page)
            return page_count * bytes_per_page;
    }
#endif
    return kFallbackPhysicalBytes;
}

int maximum_cache_ram_mb()
{
    const std::uint64_t half_mb = total_physical_bytes() / (2ull * kMiB);
    const std::uint64_t bounded = std::clamp<std::uint64_t>(
        half_mb, static_cast<std::uint64_t>(kMinimumCacheRamMb),
        static_cast<std::uint64_t>(INT_MAX));
    return static_cast<int>(bounded);
}

int clamp_cache_ram_mb(int megabytes)
{
    return std::clamp(megabytes, kMinimumCacheRamMb, maximum_cache_ram_mb());
}

} // namespace fxm::system_memory
