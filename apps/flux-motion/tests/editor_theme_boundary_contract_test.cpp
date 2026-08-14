#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Editor theme boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool reject_token(const std::string &text, const std::string &token,
                  const char *label)
{
    if (text.find(token) == std::string::npos)
        return true;
    std::cerr << "Forbidden Editor theme dependency: " << label
              << " (" << token << ")\n";
    return false;
}

std::string cmake_block(const std::string &cmake, const std::string &name)
{
    const std::string marker = "set(" + name;
    const std::size_t begin = cmake.find(marker);
    if (begin == std::string::npos)
        return {};
    const std::size_t end = cmake.find("\n)", begin);
    return cmake.substr(begin, end == std::string::npos
        ? std::string::npos : end - begin + 2);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 8)
        return 2;

    const std::string theme_header = read_file(argv[1]);
    const std::string theme_source = read_file(argv[2]);
    const std::string editor_main = read_file(argv[3]);
    const std::string preferences = read_file(argv[4]);
    const std::string cmake = read_file(argv[5]);
    const std::string locale = read_file(argv[6]);
    const std::string window_session = read_file(argv[7]);

    bool ok = true;
    ok = require_token(theme_header, "enum class EditorTheme",
                       "theme selection interface") && ok;
    ok = require_token(theme_source, "QStringLiteral(\"flux-motion\")",
                       "Flux Motion default") && ok;
    ok = require_token(theme_source, "QColor(0x78, 0x38, 0xf5)",
                       "Flux Motion accent") && ok;
    ok = require_token(theme_source, "QSettings",
                       "persistent theme selection") && ok;
    ok = require_token(editor_main, "apply_editor_theme(application",
                       "startup theme application") && ok;
    ok = require_token(editor_main, "result = application.exec();",
                       "preferences event loop") && ok;
    ok = require_token(preferences, "OBSTitles.EditorThemeFluxMotion",
                       "Preferences theme selector") && ok;
    ok = require_token(locale, "OBSTitles.EditorThemeFluxMotion",
                       "localized theme label") && ok;
    ok = reject_token(theme_header, "AdobeDark",
                      "removed Adobe Dark theme") && ok;
    ok = reject_token(theme_header, "Light",
                      "removed Light theme") && ok;
    ok = reject_token(theme_header, "System",
                      "removed System theme") && ok;
    ok = reject_token(window_session, "pal.setColor(QPalette::Window",
                      "per-window palette overriding application theme") && ok;
    ok = require_token(cmake, "Editor/editor-theme.cpp",
                       "standalone Editor theme source") && ok;

    const std::string dock_sources = cmake_block(cmake, "OBS_FXM_OBS_DOCK_SOURCES");
    const std::string dock_headers = cmake_block(cmake, "OBS_FXM_OBS_DOCK_HEADERS");
    ok = reject_token(dock_sources, "editor-theme",
                      "theme implementation in OBS dock sources") && ok;
    ok = reject_token(dock_headers, "editor-theme",
                      "theme interface in OBS dock headers") && ok;
    ok = reject_token(theme_header, "obs_", "OBS types in Editor theme API") && ok;
    ok = reject_token(theme_source, "obs_", "OBS types in Editor theme implementation") && ok;
    return ok ? 0 : 1;
}
