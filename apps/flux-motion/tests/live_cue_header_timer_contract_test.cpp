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

void require_before(const std::string &source, const char *first,
                    const char *second)
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
    assert(argc == 9);
    const std::string title_data = read_file(argv[1]);
    const std::string title_data_impl = read_file(argv[2]);
    const std::string source_runtime = read_file(argv[3]);
    const std::string source_tick = read_file(argv[4]);
    const std::string lifecycle = read_file(argv[5]);
    const std::string timer_ui = read_file(argv[6]);
    const std::string locale = read_file(argv[7]);
    const std::string cmake = read_file(argv[8]);

    require(title_data, "struct LiveCueRuntimeSnapshot {");
    require(title_data, "void publish_live_cue_runtime_state(");
    require(title_data, "void clear_live_cue_runtime_state(");
    require(title_data, "LiveCueRuntimeSnapshot live_cue_runtime_state(");

    require(title_data_impl, "std::mutex g_live_cue_runtime_mutex;");
    require(title_data_impl, "g_live_cue_runtime_states[title_id]");
    require(title_data_impl, "g_live_cue_runtime_states.erase(it);");

    require(source_runtime, "static void publish_live_cue_runtime(");
    require(source_runtime, "const bool source_running = data->scene_mask_foreground_active &&");
    require(source_runtime, "data->shown_on_display.load(std::memory_order_acquire);");
    require(source_runtime, "data->cue_timer_started_at = now;");
    require(source_runtime, "now - data->cue_timer_started_at");
    require(source_runtime, "publish_live_cue_runtime_state(");
    require(source_runtime, "clear_live_cue_runtime_state(");
    require(source_tick, "publish_live_cue_runtime(data, title);");
    require(source_tick, "clear_live_cue_runtime_for_source(data, data->title_id);");

    require(lifecycle, "update_live_cue_timer_label();\n        uint64_t revision");
    require(lifecycle, "live_header->addWidget(live_cue_timer_lbl_);");
    require_before(lifecycle, "live_header->addStretch();",
                   "live_header->addWidget(live_cue_timer_lbl_);");

    require(timer_ui, "QString format_live_cue_elapsed(double seconds)");
    require(timer_ui, "QString format_live_cue_countdown(double seconds)");
    require(timer_ui, "const bool ending = title->cue_uncue_requested ||");
    require(timer_ui, "const bool play_once = title->playback_mode == 0;");
    require(timer_ui, "if (ending || play_once)");
    require(timer_ui, "title->duration - runtime.playhead");
    require(timer_ui, "runtime.elapsed_seconds");
    require(timer_ui, "now_ms - runtime.updated_ms <= 2000");
    require(timer_ui, "live_cue_timer_lbl_->setVisible(false);");

    require(locale, "OBSTitles.LiveCueElapsedTooltip=");
    require(locale, "OBSTitles.LiveCueOutroRemainingTooltip=");
    require(locale, "OBSTitles.LiveCuePlayOnceRemainingTooltip=");
    require(cmake, "set(OBS_FXM_DEVELOPMENT_VERSION \"");

    std::cout << "live cue header timer contract: PASS\n";
    return 0;
}
