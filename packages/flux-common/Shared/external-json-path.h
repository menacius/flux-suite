#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fxm::external_data {

struct JsonPathToken {
    bool is_index = false;
    std::string key;
    std::size_t index = 0;
};

inline std::string trim_json_path_component(std::string value)
{
    auto first = std::find_if_not(value.begin(), value.end(),
                                  [](unsigned char ch) {
                                      return std::isspace(ch) != 0;
                                  });
    auto last = std::find_if_not(value.rbegin(), value.rend(),
                                 [](unsigned char ch) {
                                     return std::isspace(ch) != 0;
                                 }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

inline bool parse_json_path(const std::string &path,
                            std::vector<JsonPathToken> &tokens,
                            std::string *error = nullptr)
{
    tokens.clear();
    std::size_t position = 0;
    while (position < path.size()) {
        if (path[position] == '.') {
            ++position;
            continue;
        }
        if (path[position] == '[') {
            const std::size_t close = path.find(']', position + 1);
            if (close == std::string::npos) {
                if (error)
                    *error = "Unclosed array index in field path.";
                return false;
            }
            const std::string index_text = trim_json_path_component(
                path.substr(position + 1, close - position - 1));
            if (index_text.empty() ||
                !std::all_of(index_text.begin(), index_text.end(),
                             [](unsigned char ch) {
                                 return std::isdigit(ch) != 0;
                             })) {
                if (error)
                    *error = "Array indexes must be non-negative integers.";
                return false;
            }
            try {
                tokens.push_back({true, {}, static_cast<std::size_t>(
                    std::stoull(index_text))});
            } catch (...) {
                if (error)
                    *error = "Array index is too large.";
                return false;
            }
            position = close + 1;
            continue;
        }

        const std::size_t begin = position;
        while (position < path.size() && path[position] != '.' &&
               path[position] != '[') {
            ++position;
        }
        std::string key = path.substr(begin, position - begin);
        if (key.empty()) {
            if (error)
                *error = "Empty component in field path.";
            return false;
        }
        tokens.push_back({false, std::move(key), 0});
    }
    return true;
}

template <typename Json>
inline const Json *resolve_json_path(const Json &root, const std::string &path,
                                     std::string *error = nullptr)
{
    if (path.empty())
        return &root;

    std::vector<JsonPathToken> tokens;
    if (!parse_json_path(path, tokens, error))
        return nullptr;

    const Json *current = &root;
    for (const JsonPathToken &token : tokens) {
        if (token.is_index) {
            if (!current->is_array() || token.index >= current->size()) {
                if (error)
                    *error = "Array index is outside the JSON value.";
                return nullptr;
            }
            current = &(*current)[token.index];
            continue;
        }

        if (!current->is_object()) {
            if (error)
                *error = "JSON field path traverses a non-object value.";
            return nullptr;
        }
        if (!current->contains(token.key)) {
            if (error)
                *error = "JSON field path was not found: " + path;
            return nullptr;
        }
        current = &current->at(token.key);
    }
    return current;
}

} // namespace fxm::external_data
