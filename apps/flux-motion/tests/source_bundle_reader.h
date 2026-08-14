#pragma once
#include <cassert>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
namespace fxm::test {
inline std::string read_source_bundle_impl(const std::filesystem::path &path,
                                           std::set<std::filesystem::path> &stack)
{
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
    assert(stack.insert(normalized).second && "cyclic implementation-module include");
    std::ifstream input(normalized, std::ios::binary);
    assert(input.good());
    static const std::regex module_include(R"(^\s*#\s*include\s+\"([^\"]+\.inc)\"\s*$)");
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, module_include))
            output << read_source_bundle_impl(normalized.parent_path() / match[1].str(), stack);
        else {
            output << line;
            if (!input.eof()) output << '\n';
        }
    }
    stack.erase(normalized);
    return output.str();
}
inline std::string read_file(const char *path)
{
    std::set<std::filesystem::path> stack;
    return read_source_bundle_impl(std::filesystem::path(path), stack);
}
} // namespace fxm::test
using fxm::test::read_file;
