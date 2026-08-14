#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &source, const std::string &needle,
             const char *label)
{
    if (source.find(needle) != std::string::npos) return true;
    std::cerr << "Missing editor UI revision 140 contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool absent(const std::string &source, const std::string &needle,
            const char *label)
{
    if (source.find(needle) == std::string::npos) return true;
    std::cerr << "Obsolete editor UI revision 140 contract remains: " << label
              << " (" << needle << ")\n";
    return false;
}

bool theme_icon(const std::string &source, const char *label)
{
    bool ok = true;
    ok &= require(source, "currentColor", label);
    ok &= absent(source, "#4a5565", label);
    ok &= absent(source, "#4A5565", label);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 17) {
        std::cerr << "usage: editor_ui_revision_140_contract_test "
                     "<commands> <graph> <sidebar> <layers> <timeline> "
                     "<dist-h> <dist-v> <flip-h> <flip-v> <matte-alpha> "
                     "<matte-clipping> <graph-icon> <matte-inverted> "
                     "<matte-luma> <matte-normal> <build-info>\n";
        return 2;
    }

    const std::string commands = read_file(argv[1]);
    const std::string graph = read_file(argv[2]);
    const std::string sidebar = read_file(argv[3]);
    const std::string layers = read_file(argv[4]);
    const std::string timeline = read_file(argv[5]);
    const std::string dist_h = read_file(argv[6]);
    const std::string dist_v = read_file(argv[7]);
    const std::string flip_h = read_file(argv[8]);
    const std::string flip_v = read_file(argv[9]);
    const std::string matte_alpha = read_file(argv[10]);
    const std::string matte_clipping = read_file(argv[11]);
    const std::string graph_icon = read_file(argv[12]);
    const std::string matte_inverted = read_file(argv[13]);
    const std::string matte_luma = read_file(argv[14]);
    const std::string matte_normal = read_file(argv[15]);
    const std::string build_info = read_file(argv[16]);

    bool ok = true;
    ok &= require(commands, "graph_editor_toggle->setIcon(obs_icon(\"graph.svg\"))",
                  "Graph Editor supplied icon");
    ok &= require(commands, "graph_editor_toggle->setCheckable(true)",
                  "Graph Editor toggle state");
    ok &= require(commands, "graph_view->setVisible(false)",
                  "Graph view initially hidden");
    ok &= require(commands, "fit_graphs->setVisible(false)",
                  "Fit Graphs initially hidden");
    ok &= require(commands, "fit_graph_selection->setVisible(false)",
                  "Fit Selection initially hidden");
    ok &= require(commands, "graph_view->setVisible(enabled)",
                  "Graph view follows toggle");
    ok &= require(commands, "fit_graphs->setVisible(enabled)",
                  "Fit Graphs follows toggle");
    ok &= require(commands, "fit_graph_selection->setVisible(enabled)",
                  "Fit Selection follows toggle");

    ok &= require(graph, "if (ev->pos().y() < ruler_height()) return false;",
                  "shared ruler mouse handling");
    ok &= require(graph, "drag_mode_ = DragMode::Playhead;",
                  "direct graph playhead dragging");
    ok &= require(graph, "if (!graph_drag && drag_mode_ != DragMode::None)",
                  "non-graph drag forwarding");
    ok &= require(graph, "if (!graph_drag) return false;",
                  "non-graph release forwarding");

    ok &= require(sidebar, "hold_timer_.setInterval(250)",
                  "shared hold-menu delay");
    ok &= require(sidebar, "QGuiApplication::screenAt(global_center)",
                  "screen-aware flyout side");
    ok &= require(sidebar, "const int right_x", "right-side flyout placement");
    ok &= require(sidebar, "const int left_x", "left-side flyout placement");
    ok &= absent(sidebar, "menu()->popup(mapToGlobal(QPoint(width() + 2, 0)))",
                 "fixed right-only flyout");

    ok &= require(layers, "constexpr int kLayerStackMinimumWidth =",
                  "safe layer-list minimum width");
    ok &= require(layers, "name->setMinimumWidth(kLayerNameMinimumWidth)",
                  "header Layer Name minimum");
    ok &= require(layers, "name_cell->setMinimumWidth(kLayerNameMinimumWidth)",
                  "row Layer Name minimum");
    ok &= require(layers, "setMinimumWidth(kLayerStackMinimumWidth)",
                  "splitter cannot compress columns");

    ok &= require(timeline, "const int cache_y = rh - cache_h - 1",
                  "cache band at ruler bottom");
    ok &= require(timeline, "const int tick_baseline = cache_y - 1",
                  "tick band above cache");
    ok &= require(timeline, "const int label_top = 2",
                  "separate ruler label band");

    ok &= theme_icon(dist_h, "horizontal distribute icon");
    ok &= theme_icon(dist_v, "vertical distribute icon");
    ok &= theme_icon(flip_h, "horizontal flip icon");
    ok &= theme_icon(flip_v, "vertical flip icon");
    ok &= theme_icon(matte_alpha, "alpha matte icon");
    ok &= theme_icon(matte_clipping, "clipping matte icon");
    ok &= theme_icon(graph_icon, "Graph Editor icon");
    ok &= theme_icon(matte_inverted, "inverted matte icon");
    ok &= theme_icon(matte_luma, "luma matte icon");
    ok &= theme_icon(matte_normal, "normal matte icon");

    ok &= require(build_info, "FXM_DEVELOPMENT_VERSION",
                  "development version metadata remains centralized");

    return ok ? 0 : 1;
}
