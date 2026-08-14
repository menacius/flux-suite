#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Core boundary: " << label << " (" << token << ")\n";
    return false;
}

bool clean_core_source(const std::string &text, const char *label)
{
    static const char *forbidden[] = {
        "#include <obs", "#include \"obs", "obs_module_",
        "obs_frontend_", "blog(", "bfree(", "QWidget", "QtWidgets"
    };
    bool ok = true;
    for (const char *token : forbidden) {
        if (text.find(token) != std::string::npos) {
            std::cerr << "Forbidden Core dependency in " << label
                      << ": " << token << '\n';
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
    const std::string title_data = read_file(argv[1]);
    const std::string title_logger = read_file(argv[2]);
    const std::string host_interface = read_file(argv[3]);
    const std::string logger_interface = read_file(argv[4]);
    const std::string obs_host = read_file(argv[5]);
    const std::string obs_logger = read_file(argv[6]);
    const std::string cache_visual_hash = read_file(argv[7]);

    bool ok = clean_core_source(title_data, "title-data.cpp");
    ok = clean_core_source(title_logger, "title-logger.cpp") && ok;
    ok = require_token(title_data, "host_context_provider()",
                       "Core host-context injection") && ok;
    ok = require_token(title_logger, "fxm::log(",
                       "Core logger injection") && ok;
    ok = require_token(host_interface, "class IHostContextProvider",
                       "Shared host-context interface") && ok;
    ok = require_token(logger_interface, "class ILogger",
                       "Shared logger interface") && ok;
    ok = require_token(obs_host, "obs_module_config_path",
                       "OBS config-path adapter") && ok;
    ok = require_token(obs_host, "obs_frontend_get_current_scene_collection",
                       "OBS project-scope adapter") && ok;
    ok = require_token(obs_logger, "blog(", "OBS log adapter") && ok;
    ok = clean_core_source(cache_visual_hash, "cache visual-hash keying") && ok;
    ok = require_token(cache_visual_hash, "fxm::current_frame_rate()",
                       "cache frame-rate provider") && ok;
    return ok ? 0 : 1;
}
