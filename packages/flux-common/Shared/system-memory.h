#pragma once

#include <cstdint>

namespace fxm::system_memory {

constexpr int kMinimumCacheRamMb = 16;

/* Total installed physical memory, not currently available memory. Returns a
 * conservative fallback when the platform query is unavailable. */
std::uint64_t total_physical_bytes();

/* User-configurable upper bound: half of installed physical RAM. */
int maximum_cache_ram_mb();
int clamp_cache_ram_mb(int megabytes);

} // namespace fxm::system_memory
