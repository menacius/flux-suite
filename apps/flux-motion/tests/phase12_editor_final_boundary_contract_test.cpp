#include "source_bundle_reader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing final Phase 12 boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool reject_token(const std::string &text, const std::string &token,
                  const char *label)
{
    if (text.find(token) == std::string::npos)
        return true;
    std::cerr << "Forbidden final Phase 12 dependency: " << label
              << " (" << token << ")\n";
    return false;
}

bool source_file(const std::filesystem::path &path)
{
    const std::string extension = path.extension().string();
    return extension == ".h" || extension == ".hpp" ||
           extension == ".cpp" || extension == ".cc" ||
           extension == ".inc";
}

std::string read_text(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

bool verify_clean_editor(const std::filesystem::path &root)
{
    static const std::regex obs_include(
        R"(#\s*include\s*[<"][^>"]*(?:obs|graphics/)[^>"]*[>"])");
    static const std::regex obs_symbol(R"(\b(?:obs_|gs_)[A-Za-z0-9_]*\b)");

    bool ok = true;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !source_file(entry.path()))
            continue;
        const std::string text = read_text(entry.path());
        if (std::regex_search(text, obs_include) ||
            std::regex_search(text, obs_symbol)) {
            std::cerr << "OBS dependency in clean Editor file: "
                      << entry.path().string() << '\n';
            ok = false;
        }
    }
    return ok;
}

bool verify_transitional_inventory(const std::filesystem::path &root)
{
    const std::set<std::string> expected = {
        "properties-panel.cpp",
        "style-presets.cpp",
        "title-dock.cpp",
        "title-editor.cpp",
        "title-properties-panel.cpp",
        "tools-sidebar.cpp",
    };
    std::set<std::string> actual;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".cpp") {
            actual.insert(entry.path().filename().string());
        }
    }
    if (actual == expected)
        return true;
    std::cerr << "Unexpected top-level src/editor inventory\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 12)
        return 2;

    const std::string cmake = read_file(argv[3]);
    const std::string dock = read_file(argv[4]);
    const std::string editor = read_file(argv[5]);
    const std::string properties = read_file(argv[6]);
    const std::string styles = read_file(argv[7]);
    const std::string tools = read_file(argv[8]);
    const std::string title_properties = read_file(argv[9]);
    const std::string title_source = read_file(argv[10]);
    const std::string editor_internal = read_file(argv[11]);

    bool ok = verify_clean_editor(argv[1]);
    ok = verify_transitional_inventory(argv[2]) && ok;

    ok = require_token(cmake, "OBS_FXM_EDITOR_MODULE_SOURCES",
                       "clean Editor source ownership") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_OBS_COUPLED_EDITOR_SOURCES",
                       "Phase 13 integration inventory") &&
         ok;
    ok = cmake.find("set(OBS_FXM_EDITOR_SOURCES") == std::string::npos &&
         ok;

    ok = require_token(dock, "#include <obs-frontend-api.h>",
                       "OBS dock integration classification") &&
         ok;
    ok = require_token(editor, "#include <obs-frontend-api.h>",
                       "OBS preview integration classification") &&
         ok;
    ok = require_token(properties, "\"title-video-runtime.h\"",
                       "OBS video runtime dependency classification") &&
         ok;
    ok = require_token(styles, "\"properties-panel.h\"",
                       "mixed preset UI/library classification") &&
         ok;
    ok = require_token(title_source, "#include \"style-preset-runtime.h\"",
                       "OBS renderer Core preset-runtime consumer") &&
         ok;
    ok = require_token(tools, "\"editor-tool-icons.h\"",
                       "tools icon integration classification") &&
         ok;
    ok = require_token(title_properties, "\"title-animation-edit-utils.h\"",
                       "properties animation-edit integration classification") &&
         ok;
    ok = require_token(editor_internal, "\"title-preview-renderer.h\"",
                       "Shared preview-render boundary") &&
         ok;
    ok = reject_token(editor_internal, "\"title-source.h\"",
                      "legacy OBS preview-render dependency") &&
         ok;
    return ok ? 0 : 1;
}
