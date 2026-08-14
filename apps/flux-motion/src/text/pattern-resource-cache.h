#pragma once

#include "performance-counters.h"

#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>

namespace fxm::text {

class PatternResourceCache {
public:
    static PatternResourceCache &instance()
    {
        static PatternResourceCache cache;
        return cache;
    }

    std::shared_ptr<const std::regex> get(const std::string &pattern,
                                          bool case_sensitive)
    {
        if (pattern.empty())
            return {};
        const std::string key = pattern + (case_sensitive ? "\x1fS" : "\x1fI");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = entries_.find(key);
            if (found != entries_.end()) {
                lru_.splice(lru_.begin(), lru_, found->second.lru);
                fxm::perf::add(fxm::perf::Counter::PatternCacheHits);
                return found->second.expression;
            }
        }

        std::shared_ptr<const std::regex> compiled;
        try {
            const auto flags = std::regex::ECMAScript |
                (case_sensitive ? std::regex::flag_type{} : std::regex::icase);
            compiled = std::make_shared<const std::regex>(pattern, flags);
        } catch (const std::regex_error &) {
            return {};
        } catch (...) {
            return {};
        }
        fxm::perf::add(fxm::perf::Counter::PatternCacheMisses);

        std::lock_guard<std::mutex> lock(mutex_);
        const auto race = entries_.find(key);
        if (race != entries_.end()) {
            lru_.splice(lru_.begin(), lru_, race->second.lru);
            return race->second.expression;
        }
        lru_.push_front(key);
        entries_.emplace(key, Entry{compiled, lru_.begin()});
        while (entries_.size() > capacity_) {
            const std::string evicted = lru_.back();
            lru_.pop_back();
            entries_.erase(evicted);
            fxm::perf::add(fxm::perf::Counter::PatternCacheEvictions);
        }
        return compiled;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        lru_.clear();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

private:
    struct Entry {
        std::shared_ptr<const std::regex> expression;
        std::list<std::string>::iterator lru;
    };

    PatternResourceCache() = default;
    mutable std::mutex mutex_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, Entry> entries_;
    const std::size_t capacity_ = 256;
};

inline std::shared_ptr<const std::regex> cached_pattern(
    const std::string &pattern, bool case_sensitive)
{
    return PatternResourceCache::instance().get(pattern, case_sensitive);
}

inline void clear_pattern_resource_cache()
{
    PatternResourceCache::instance().clear();
}

} // namespace fxm::text
