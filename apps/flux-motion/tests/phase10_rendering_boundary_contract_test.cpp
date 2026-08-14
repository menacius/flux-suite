#include "source_bundle_reader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Phase 10 rendering boundary: " << label
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

bool verify_obs_free_module(const std::filesystem::path &root)
{
    static const std::regex obs_include(
        R"(#\s*include\s*[<"][^>"]*(?:obs|graphics/)[^>"]*[>"])");
    static const std::regex obs_symbol(R"(\b(?:obs_|gs_)[A-Za-z0-9_]*\b)");
    static const std::regex widget_include(
        R"(#\s*include\s*[<"]Q(?:Widget|MainWindow|DockWidget|Dialog|Menu|ToolBar|Application)[>"])");

    bool ok = true;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !source_file(entry.path()))
            continue;
        /* Development Version 409: the standalone executable intentionally
         * hosts libobs so its canvas executes the production OBS compositor.
         * Keep this bridge narrow; all reusable Editor/Core/Shared code remains
         * backend-neutral and is still checked below. */
        if (root.filename() == "Editor") {
            const std::string filename = entry.path().filename().string();
            if (filename == "main.cpp" ||
                filename == "standalone-obs-graphics-runtime.cpp" ||
                filename == "standalone-obs-graphics-runtime.h")
                continue;
        }
        const std::string text = read_text(entry.path());
        if (std::regex_search(text, obs_include) ||
            std::regex_search(text, obs_symbol) ||
            (root.filename() == "Core" &&
             std::regex_search(text, widget_include))) {
            std::cerr << "Forbidden dependency in "
                      << entry.path().string() << '\n';
            ok = false;
        }
    }
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 8)
        return 2;

    const std::string interfaces = read_file(argv[1]);
    const std::string adapter_header = read_file(argv[2]);
    const std::string adapter_source = read_file(argv[3]);
    const std::string title_source = read_file(argv[4]);

    bool ok = true;
    for (const auto &[token, label] : std::vector<std::pair<std::string, const char *>> {
             {"class IRenderBackend", "shared render backend interface"},
             {"class ITexture", "shared texture interface"},
             {"class IRenderTarget", "shared render-target interface"},
             {"class IShader", "shared shader interface"},
             {"class IRenderer", "shared renderer interface"},
             {"class IFrameRenderer", "shared frame-renderer interface"},
         }) {
        ok = require_token(interfaces, token, label) && ok;
    }
    ok = interfaces.find("obs_") == std::string::npos && ok;
    ok = interfaces.find("gs_") == std::string::npos && ok;

    ok = require_token(
             adapter_header,
             "class ObsRenderBackend final : public rendering::IRenderBackend",
             "OBS backend implements IRenderBackend") &&
         ok;
    for (const auto &[token, label] : std::vector<std::pair<std::string, const char *>> {
             {"ObsRenderBackend::create_texture", "OBS texture adapter"},
             {"ObsRenderBackend::create_render_target", "OBS target adapter"},
             {"ObsRenderBackend::create_shader", "OBS shader adapter"},
             {"ObsRenderBackend::begin_frame", "OBS frame begin adapter"},
             {"ObsRenderBackend::end_frame", "OBS frame end adapter"},
         }) {
        ok = require_token(adapter_source, token, label) && ok;
    }

    for (const auto &[token, label] : std::vector<std::pair<std::string, const char *>> {
             {"g_obs_render_backend.create_texture", "backend texture ownership"},
             {"g_obs_render_backend.create_render_target", "backend target ownership"},
             {"g_obs_render_backend.create_shader", "backend shader ownership"},
             {"external_background_snapshot_resource", "background snapshot owner"},
             {"external_background_local", "background target owner"},
             {"adjustment_coverage_target_resource", "coverage target owner"},
             {"masked_target_resource", "masked target owner"},
             {"entry.texture_resource = std::move(texture_resource)",
              "cached-frame texture owners"},
         }) {
        ok = require_token(title_source, token, label) && ok;
    }

    ok = verify_obs_free_module(argv[5]) && ok;
    ok = verify_obs_free_module(argv[6]) && ok;
    ok = verify_obs_free_module(argv[7]) && ok;
    return ok ? 0 : 1;
}
