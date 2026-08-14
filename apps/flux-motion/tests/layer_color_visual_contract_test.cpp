#include <iostream>
#include <sstream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing layer-color visual contract: " << label
              << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        std::cerr << "usage: layer_color_visual_contract_test "
                     "<preferences-h> <preferences-cpp> <internal-h> <editor-cpp> <canvas-cpp>\n";
        return 2;
    }

    const std::string preferences_h = read_file(argv[1]);
    const std::string preferences_cpp = read_file(argv[2]);
    const std::string internal_h = read_file(argv[3]);
    const std::string editor_cpp = read_file(argv[4]);
    const std::string canvas_cpp = read_file(argv[5]);

    bool ok = true;
    ok &= require(preferences_h, "GroupLayer", "dedicated group timeline color role");
    ok &= require(preferences_cpp, "QStringLiteral(\"groupLayer\")",
                  "persistent group timeline color key");
    ok &= require(preferences_cpp, "case TimelineColorRole::GroupLayer:",
                  "group timeline default color");
    ok &= require(internal_h, "TimelineColorRole::GroupLayer",
                  "group layers map to their own color");
    ok &= require(editor_cpp, "OBSTitles.GroupLayers",
                  "group timeline color preference row");
    ok &= require(canvas_cpp, "QColor outline_color = layer_color(layer, 0);",
                  "selection boxes use layer color");
    ok &= require(canvas_cpp, "editing_text_layer ? Qt::SolidLine : Qt::DashLine",
                  "text editing box uses solid line");
    ok &= require(canvas_cpp, "QColor hover_color = layer_color(layer, 0);",
                  "hover boxes use layer color");
    ok &= require(canvas_cpp, "aggregate_color = layer_color(*active_layer, 0);",
                  "multi-selection transform box follows active layer color");

    return ok ? 0 : 1;
}
