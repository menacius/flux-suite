#include "system-memory.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    using namespace fxm::system_memory;
    constexpr std::uint64_t mib = 1024ull * 1024ull;
    const std::uint64_t physical = total_physical_bytes();
    const int maximum = maximum_cache_ram_mb();
    assert(physical >= static_cast<std::uint64_t>(kMinimumCacheRamMb) * 2ull * mib);
    assert(maximum >= kMinimumCacheRamMb);
    assert(static_cast<std::uint64_t>(maximum) <= physical / (2ull * mib));
    assert(clamp_cache_ram_mb(0) == kMinimumCacheRamMb);
    assert(clamp_cache_ram_mb(maximum + (maximum < 2147483647 ? 1 : 0)) == maximum);
    assert(clamp_cache_ram_mb(kMinimumCacheRamMb) == kMinimumCacheRamMb);
    std::cout << "physical_bytes=" << physical
              << " maximum_cache_mb=" << maximum << '\n';
    return 0;
}
