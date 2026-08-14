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
    assert(argc == 7);
    const std::string helpers = read_file(argv[1]);
    const std::string delegate = read_file(argv[2]);
    const std::string lifecycle = read_file(argv[3]);
    const std::string population = read_file(argv[4]);
    const std::string actions = read_file(argv[5]);
    const std::string cmake = read_file(argv[6]);

    require(helpers, "enum class TitleCueVisualState : int {");
    require(helpers, "Inactive = 0");
    require(helpers, "Queued,");
    require(helpers, "Cued,");
    require(helpers, "Ending,");
    require(helpers, "title_cue_visual_state(const Title &title,");
    require(helpers, "title.cue_uncue_requested || title.pending_cue_row >= 0");
    require(helpers, "title.pending_cue_row >= 0 || waiting_for_prerender");
    require_before(helpers, "TitleCueVisualState::Ending", "TitleCueVisualState::Cued");
    require(helpers, "title_cue_visual_color(TitleCueVisualState state)");
    require(helpers, "live_cue_state_color(false, false, true)");
    require(helpers, "live_cue_state_color(true, false, false)");
    require(helpers, "live_cue_state_color(false, true, false)");

    require(delegate, "index.data(kTitleCueVisualStateRole).toInt()");
    require(delegate, "title_cue_visual_color(cue_state)");
    require(delegate, "paint_cue_state_border(");
    require(delegate, "QPen pen(color, 2.0);");

    require(lifecycle, "title_cue_visual_state(*title, waiting_for_prerender)");
    require(population, "title_cue_visual_state(*t, waiting_for_prerender)");
    require(actions, "static_cast<int>(TitleCueVisualState::Inactive)");
    require(cmake, "set(OBS_FXM_DEVELOPMENT_VERSION");

    std::cout << "title thumbnail cue state contract: PASS\n";
    return 0;
}
