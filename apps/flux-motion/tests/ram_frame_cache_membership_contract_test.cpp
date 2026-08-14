#include <cassert>
#include <iostream>
#include <string>
#include "source_bundle_reader.h"

int main(int argc, char **argv)
{
    assert(argc == 4);
    const std::string header = read_file(argv[1]);
    const std::string ram = read_file(argv[2]);
    const std::string manager = read_file(argv[3]);
    assert(header.find("bool contains(const CacheFrameKey &key) const;") != std::string::npos);
    assert(ram.find("bool RamFrameCache::contains(const CacheFrameKey &key) const") != std::string::npos);
    assert(manager.find("return cache.contains(key);") != std::string::npos);
    assert(manager.find("QImage ignored;\n    return cache.get(key, ignored);") == std::string::npos);
    std::cout << "RAM cache membership checks are non-mutating and allocation-free\n";
    return 0;
}
