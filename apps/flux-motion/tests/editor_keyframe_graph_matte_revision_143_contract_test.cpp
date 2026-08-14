#include <algorithm>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &source, const std::string &needle,
             const char *label)
{
    if (source.find(needle) != std::string::npos) return true;
    std::cerr << "Missing editor revision 143 contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool absent(const std::string &source, const std::string &needle,
            const char *label)
{
    if (source.find(needle) == std::string::npos) return true;
    std::cerr << "Obsolete editor revision 143 contract remains: " << label
              << " (" << needle << ")\n";
    return false;
}

size_t occurrences(const std::string &source, const std::string &needle)
{
    size_t count = 0;
    for (size_t pos = 0; (pos = source.find(needle, pos)) != std::string::npos;
         pos += needle.size())
        ++count;
    return count;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 10) {
        std::cerr << "usage: editor_keyframe_graph_matte_revision_143_contract_test "
                     "<timeline> <graph> <layers> <layer-header> <commands> "
                     "<editor-events> <graph-icon> <locale> <build-info>\n";
        return 2;
    }

    const std::string timeline = read_file(argv[1]);
    const std::string graph = read_file(argv[2]);
    const std::string layers = read_file(argv[3]);
    const std::string layer_header = read_file(argv[4]);
    const std::string commands = read_file(argv[5]);
    const std::string editor_events = read_file(argv[6]);
    const std::string graph_icon = read_file(argv[7]);
    const std::string locale = read_file(argv[8]);
    const std::string build_info = read_file(argv[9]);

    bool ok = true;

    ok &= absent(timeline, "menu.addMenu(fxm_tr(\"OBSTitles.Easing\"))",
                 "legacy Easing submenu");
    ok &= require(timeline, "menu.addMenu(fxm_tr(\"OBSTitles.TemporalInterpolation\"))",
                  "timeline Temporal Interpolation menu");
    ok &= require(timeline, "OBSTitles.KeyframeVelocity",
                  "timeline Keyframe Velocity command");
    ok &= require(layers, "menu.addMenu(fxm_tr(\"OBSTitles.TemporalInterpolation\"))",
                  "layer-list Temporal Interpolation menu");
    ok &= require(layers, "property_temporal_mode_changed",
                  "layer-list temporal mode command");
    ok &= require(layers, "property_easy_ease_requested",
                  "layer-list Easy Ease command");
    ok &= require(layers, "property_velocity_requested",
                  "layer-list Keyframe Velocity command");

    ok &= require(layer_header, "void set_playhead(double timeline_time);",
                  "playhead-aware layer-list API");
    ok &= require(layers, "void LayerStack::update_property_rows()",
                  "live property-row refresh");
    ok &= require(layers, "prop.vector->evaluate(local_time)",
                  "evaluated vector values");
    ok &= require(layers, "prop.graph_value(local_time)",
                  "evaluated scalar values");
    ok &= require(layers, "keyframe_index_at_time(prop, local_time)",
                  "current-frame diamond state");
    ok &= require(layers, "QSignalBlocker blocker(value_x)",
                  "programmatic textbox refresh without key creation");
    ok &= require(layers, "QDoubleSpinBox:focus{border-color:%4;}",
                  "plugin-consistent textbox focus style");
    ok &= require(layers, "selection-background-color:%4",
                  "plugin-consistent textbox selection style");
    ok &= require(commands, "set_animated_value(*prop.vector, lt, {x, y})",
                  "layer-list vector editing");
    ok &= require(commands, "set_animated_value(*prop.scalar, lt, x)",
                  "layer-list scalar editing");
    ok &= require(editor_events, "if (layers_) layers_->set_playhead(t);",
                  "playhead refreshes layer-list fields");

    ok &= require(graph, "Qt::ShiftModifier",
                  "Graph Editor Shift drag modifier");
    ok &= require(graph, "Qt::ControlModifier | Qt::MetaModifier",
                  "Graph Editor Ctrl/Command drag modifier");
    ok &= require(graph, "Qt::AltModifier",
                  "Graph Editor Alt handle-break modifier");
    ok &= require(graph, "snap_time(desired_global)",
                  "Graph Editor keyframe time snapping");
    ok &= require(graph, "influence = std::round(influence / 5.0) * 5.0",
                  "Graph Editor handle snapping");
    ok &= require(graph, "format_timecode(tick)",
                  "Graph Editor timeline ruler labels");
    ok &= require(graph, "QPolygon playhead_marker",
                  "Graph Editor ruler playhead marker");
    ok &= require(graph, "p.setClipRect(header.intersected(dirty))",
                  "ruler uses independent clip");

    ok &= require(commands, "graph_editor_toggle->setIcon(obs_icon(\"graph.svg\"))",
                  "supplied Graph Editor icon");
    ok &= require(graph_icon, "currentColor", "theme-aware Graph Editor icon");
    ok &= absent(graph_icon, "#4a5565", "fixed Graph Editor icon color");
    ok &= absent(graph_icon, "rgb(74, 85, 101)", "fixed Graph Editor icon RGB");

    ok &= require(locale, "OBSTitles.MaskHeader=\"Matte Source\"",
                  "Matte Source header title");
    ok &= require(layers, "add_header_icon(\"matte-destination.svg\"",
                  "destination glyph as matte-role column header");
    ok &= absent(layers, "add_header_icon(\"matte-source.svg\"",
                 "separate matte-source role column");
    if (occurrences(layers, "add_header_icon(\"matte-destination.svg\"") != 1) {
        std::cerr << "Matte destination header must occur exactly once\n";
        ok = false;
    }
    ok &= require(layers, "add_matte_indicator(\"matte-source.svg\"",
                  "source glyph in shared role column");
    ok &= require(layers, "add_matte_indicator(\"matte-destination.svg\"",
                  "destination glyph in shared role column");

    ok &= require(build_info, "FXM_DEVELOPMENT_VERSION",
                  "centralized development version metadata");
    return ok ? 0 : 1;
}
