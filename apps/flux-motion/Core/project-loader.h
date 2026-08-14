#pragma once

#include <memory>
#include <string>

struct Title;

namespace fxm {

class IProjectLoader {
public:
    virtual ~IProjectLoader() = default;
    virtual std::shared_ptr<Title> load(const std::string &path,
                                       std::string *error) = 0;
};

} // namespace fxm
