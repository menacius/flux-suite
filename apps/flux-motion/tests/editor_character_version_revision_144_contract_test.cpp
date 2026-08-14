#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &source, const std::string &needle,
             const char *label)
{
    if (source.find(needle) != std::string::npos) return true;
    std::cerr << "Missing editor revision 144 contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool absent(const std::string &source, const std::string &needle,
            const char *label)
{
    if (source.find(needle) == std::string::npos) return true;
    std::cerr << "Obsolete editor revision 144 contract remains: " << label
              << " (" << needle << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 9) {
        std::cerr << "usage: editor_character_version_revision_144_contract_test "
                     "<character-ui> <cmake> <build-info> <plugin-main> "
                     "<vcpkg> <readme> <docs-readme> <changelog>\n";
        return 2;
    }

    const std::string character_ui = read_file(argv[1]);
    const std::string cmake = read_file(argv[2]);
    const std::string build_info = read_file(argv[3]);
    const std::string plugin_main = read_file(argv[4]);
    const std::string vcpkg = read_file(argv[5]);
    const std::string readme = read_file(argv[6]);
    const std::string docs_readme = read_file(argv[7]);
    const std::string changelog = read_file(argv[8]);

    bool ok = true;

    ok &= absent(character_ui,
                 "add_full_width_field(char_grid, 1, \"Font\", cmb_font_)",
                 "Font label row");
    ok &= absent(character_ui,
                 "add_full_width_field(char_grid, 2, \"Style\", cmb_font_style_)",
                 "Style label row");
    ok &= require(character_ui,
                  "char_grid->addWidget(cmb_font_, 1, 0, 1, 5)",
                  "full-width font family selector");
    ok &= require(character_ui,
                  "char_grid->addWidget(cmb_font_style_, 2, 0, 1, 5)",
                  "full-width font style selector");
    ok &= require(character_ui,
                  "cmb_font_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed)",
                  "expanding font family selector");
    ok &= require(character_ui,
                  "cmb_font_style_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed)",
                  "expanding font style selector");

    ok &= require(cmake, "project(flux-motion VERSION 0.8.11)",
                  "CMake public version");
    ok &= require(cmake, "set(OBS_FXM_PRERELEASE \"alpha\")",
                  "alpha prerelease identity");
    ok &= require(cmake, "set(OBS_FXM_DEVELOPMENT_VERSION \"228\")",
                  "development version 221");
    ok &= require(build_info, "#define PLUGIN_VERSION \"0.8.11-alpha\"",
                  "core runtime version");
    ok &= require(build_info, "#define FXM_DEVELOPMENT_VERSION \"228\"",
                  "core development version");
    ok &= require(plugin_main, "#define PLUGIN_VERSION \"0.8.11-alpha\"",
                  "OBS plugin runtime version");
    ok &= require(vcpkg, "\"version-string\": \"0.8.11-alpha\"",
                  "vcpkg version");
    ok &= require(readme, "v0.8.11-alpha` · `Development Version 243",
                  "README current build");
    ok &= require(readme, "since `v0.8.9-alpha` Development Version 189",
                  "README release-change baseline");
    ok &= require(readme,
                  "Flux_Motion_v0.8.11-alpha_development-version-240.zip",
                  "README current package example");
    ok &= require(docs_readme, "canonical documents for `v0.8.11-alpha`",
                  "canonical docs version");
    ok &= require(changelog,
                  "Development Version 144 — v0.8.8-alpha and Character panel cleanup",
                  "development changelog entry");

    return ok ? 0 : 1;
}
