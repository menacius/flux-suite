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

void reject_between(const std::string &source, const char *begin,
                    const char *end, const char *needle)
{
    const auto first = source.find(begin);
    assert(first != std::string::npos);
    const auto last = source.find(end, first);
    assert(last != std::string::npos);
    const std::string section = source.substr(first, last - first);
    if (section.find(needle) != std::string::npos) {
        std::cerr << "forbidden in section: " << needle << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 11);
    const std::string layer_model = read_file(argv[1]);
    const std::string title_data = read_file(argv[2]);
    const std::string properties = read_file(argv[3]);
    const std::string properties_header = read_file(argv[4]);
    const std::string hotkeys = read_file(argv[5]);
    const std::string canvas = read_file(argv[6]);
    const std::string title_source = read_file(argv[7]);
    const std::string title_editor = read_file(argv[8]);
    const std::string cmake = read_file(argv[9]);
    const std::string locale = read_file(argv[10]);

    require(layer_model, "int         ticker_playback_mode = 0");
    require(title_data, "j[\"ticker_playback_mode\"] = l.ticker_playback_mode");
    require(title_data, "json_int(j, \"ticker_playback_mode\", 0), 0, 3");

    require(properties, "btn_ticker_pause_ = new QToolButton");
    require(properties, "TickerPlaybackMode::AlwaysPlay");
    require(properties, "TickerPlaybackMode::PausedUntilCued");
    require(properties, "TickerPlaybackMode::PausedUntilHotkey");
    require(properties, "TickerPlaybackMode::CustomPlayback");
    require(properties, "fxm::ticker_runtime::toggle_pause");
    require(properties, "fxm::ticker_runtime::reset_for_mode_change");
    require(properties_header, "void runtime_visual_changed()");

    require(hotkeys, "HotkeyAction::TickerTogglePause");
    require(hotkeys, "HotkeyAction::TickerStop");
    require(hotkeys, ".toggle\"");
    require(hotkeys, ".stop\"");
    require(hotkeys, "descriptor->layer_id");

    require(canvas, "fxm::ticker_runtime::set_adaptive_pause(title_->id, true)");
    require(canvas, "fxm::ticker_runtime::set_adaptive_pause(title_->id, false)");
    require(canvas, "begin_adaptive_interaction();\n        refresh_inline_text_edit");

    require(title_source, "fxm::ticker_runtime::sample(title_id, layer, title_cued)");
    reject_between(title_source, "static QPainterPath ticker_text_path",
                   "static QColor color_from_argb", "currentMSecsSinceEpoch");

    require(title_editor, "PropertiesPanel::runtime_visual_changed");
    reject_between(title_editor, "PropertiesPanel::runtime_visual_changed",
                   "PropertiesPanel::text_char_format_changed", "set_dirty(true)");

    require(cmake, "src/core/ticker-runtime.cpp");
    require(cmake, "ticker_runtime_contract_test");
    require(locale, "OBSTitles.TickerPausedUntilHotkey");
    require(locale, "OBSTitles.TickerStopHotkey");

    std::cout << "ticker controls integration contract: PASS\n";
    return 0;
}
