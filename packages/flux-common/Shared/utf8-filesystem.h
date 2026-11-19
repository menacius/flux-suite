#pragma once

#include <filesystem>
#include <string>

namespace fxm {

/* Paths stored by Flux are UTF-8. Constructing std::filesystem::path directly
 * from std::string uses the process narrow encoding on Windows, which cannot
 * reliably represent non-ASCII names. Keep every standard-library filesystem
 * operation on the explicit UTF-8 conversion path. */
inline std::filesystem::path filesystem_path_from_utf8(
    const std::string &path)
{
    return std::filesystem::u8path(path);
}

} // namespace fxm
