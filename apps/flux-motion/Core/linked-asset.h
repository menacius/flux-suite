#pragma once

#include "title-data.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fxm::linked_asset {

using SourceResolver =
    std::function<std::shared_ptr<const Title>(const std::string &)>;
using IdFactory = std::function<std::string()>;

/* Materialize every linked Asset Layer from its current source title. The
 * resulting Title remains self-contained for serialization/rendering, while
 * stable instance IDs and exposed text/image overrides are retained. */
bool refresh_graph(std::vector<std::shared_ptr<Title>> &titles,
                   const SourceResolver &fallback_resolver,
                   const IdFactory &make_id);

} // namespace fxm::linked_asset
