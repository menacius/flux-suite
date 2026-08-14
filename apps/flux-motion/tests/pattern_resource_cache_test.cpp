#include <cassert>
#include <iostream>
#include <string>

#include "pattern-resource-cache.h"

int main()
{
    using namespace fxm::text;

    clear_pattern_resource_cache();
    const auto first = cached_pattern("^[A-Z]+$", true);
    const auto second = cached_pattern("^[A-Z]+$", true);
    assert(first);
    assert(second);
    assert(first.get() == second.get());
    assert(PatternResourceCache::instance().size() == 1);

    for (int i = 0; i < 400; ++i)
        (void)cached_pattern("^item-" + std::to_string(i) + "$", true);

    assert(PatternResourceCache::instance().size() <= 256);
    clear_pattern_resource_cache();
    assert(PatternResourceCache::instance().size() == 0);

    std::cout << "pattern resource cache: PASS\n";
    return 0;
}
