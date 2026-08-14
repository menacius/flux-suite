#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {
void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing: " << needle << '\n';
        assert(false);
    }
}

void forbid_between(const std::string &source, const char *begin,
                    const char *end, const char *needle)
{
    const std::size_t first = source.find(begin);
    assert(first != std::string::npos);
    const std::size_t last = source.find(end, first);
    assert(last != std::string::npos);
    if (source.substr(first, last - first).find(needle) != std::string::npos) {
        std::cerr << "unexpected between markers: " << needle << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 6);
    const std::string runtime = read_file(argv[1]);
    const std::string dock = read_file(argv[2]);
    const std::string hotkeys = read_file(argv[3]);
    const std::string live_cache = read_file(argv[4]);
    const std::string title_data = read_file(argv[5]);

    require(runtime, "const int cue_row_count = live_text_playlist_row_count(*title);");
    require(runtime, "data->cue_phase = TitleSourceData::CuePhase::OutroOnly;");
    require(runtime, "if (has_pending)\n                    data->playhead = pause_time;");
    forbid_between(runtime, "} else if (is_uncue) {",
                   "} else {\n                    data->playhead = 0.0;",
                   "data->playhead = loop_end;");
    require(runtime, "if (title->cue_end_behavior == 1)");
    require(runtime, "if (title->cue_end_behavior == 2)\n                        data->playhead = 0.0;");

    require(dock, "title->current_cue_row = 0;");
    require(dock, "title->cue_uncue_requested = uncue_active;");
    require(hotkeys, "const bool uncue_active = title->current_cue_row == 0 ||");
    require(hotkeys, "title->current_cue_row = 0;");
    require(hotkeys, "title->cue_uncue_requested = uncue_active;");

    const std::string exact_state =
        "const bool exact_runtime_state = title->current_cue_row == row;";
    const std::size_t first_exact = live_cache.find(exact_state);
    assert(first_exact != std::string::npos);
    assert(live_cache.find(exact_state, first_exact + 1) != std::string::npos);
    require(live_cache, "cue_title = clone_title_snapshot(title);");

    require(title_data, "bool cue_uncue_requested = false;");

    std::cout << "uncue playback/text snapshot contract: PASS\n";
    return 0;
}
