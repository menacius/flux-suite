#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {

bool require(const std::string &text, const std::string &needle,
             const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing external cue transition contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool reject(const std::string &text, const std::string &needle,
            const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Forbidden external cue transition path: " << label
              << " (" << needle << ")\n";
    return false;
}

std::string function_body(const std::string &text, const std::string &signature,
                          const std::string &next_marker)
{
    const auto begin = text.find(signature);
    if (begin == std::string::npos)
        return {};
    const auto end = text.find(next_marker, begin + signature.size());
    return end == std::string::npos ? text.substr(begin)
                                    : text.substr(begin, end - begin);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::cerr << "usage: external_data_live_cue_transition_contract_test "
                     "<source-runtime> <gpu-transitions> <dock-cues>\n";
        return 2;
    }

    const std::string runtime = read_file(argv[1]);
    const std::string transitions = read_file(argv[2]);
    const std::string dock = read_file(argv[3]);
    const std::string apply = function_body(
        runtime, "static void apply_live_text_row(",
        "/* ══════════════════════════════════════════════════════════════════");

    bool ok = true;
    ok &= require(apply, "apply_live_text_runtime_binding",
                  "source-side row application installs the cell binding");
    ok &= require(apply, "effective_live_text_cue_value",
                  "source-side row application resolves live table values");
    ok &= require(apply, "using_live_value",
                  "source-side diagnostics identify live-value use");
    ok &= require(apply, "ExternalDataLog::value_summary",
                  "source-side cue values are logged safely");
    ok &= reject(apply,
                 "apply_live_cue_layer_value(target, title->live_text_rows[value_row][col])",
                 "Loop/Pause transition must not apply empty authored table cells");

    ok &= require(transitions, "committed pending row after outro",
                  "Loop/Pause pending cue commit is logged");
    ok &= require(transitions, "apply_live_text_row(title, committed_row)",
                  "pending row uses the corrected source resolver");
    ok &= require(transitions, "playbackMode=",
                  "cue playback decision includes play mode");
    ok &= require(dock, "decision=", "dock cue decision is logged");
    ok &= require(dock, "pending-outro",
                  "Loop/Pause row switch is explicitly diagnosed");
    return ok ? 0 : 1;
}
