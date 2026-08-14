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

void require_before(const std::string &source, const char *first, const char *second)
{
    const std::size_t a = source.find(first);
    const std::size_t b = source.find(second);
    assert(a != std::string::npos);
    assert(b != std::string::npos);
    if (a >= b) {
        std::cerr << "expected before: " << first << " < " << second << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 5);
    const std::string helpers = read_file(argv[1]);
    const std::string lifecycle = read_file(argv[2]);
    const std::string population = read_file(argv[3]);
    const std::string runtime = read_file(argv[4]);

    require(helpers, "live_cue_state_color(bool current, bool queued, bool ending = false)");
    require(helpers, "live_cue_select_cell_color(bool current, bool queued, bool ending = false)");
    require(helpers, "live_cue_row_is_ending(const Title &title, int row)");
    require(helpers, "title.cue_uncue_requested || title.pending_cue_row >= 0");
    require(helpers, "return QColor(255, 202, 74);");
    require(helpers, "return QColor(255, 202, 74, 88);");
    require_before(helpers, "if (ending)", "if (current)");

    require(lifecycle, "const bool ending = live_cue_row_is_ending(*title, row);");
    require(lifecycle, "live_cue_select_cell_color(current, queued, ending)");
    require(lifecycle, "row == title->pending_cue_row || waiting_for_prerender,\n                ending" );

    require(population, "live_cue_row_is_ending(*current_title, row)");
    require(population, "live_cue_row_is_ending(*title, 0)");
    require(population, "const bool ending = live_cue_row_is_ending(*title, row);");

    require(runtime, "data->cue_phase = TitleSourceData::CuePhase::OutroOnly;");
    require(runtime, "title->cue_uncue_requested = false;");
    require(runtime, "TitleDataStore::instance().touch_runtime_change();");

    std::cout << "cue ending visual contract: PASS\n";
    return 0;
}
