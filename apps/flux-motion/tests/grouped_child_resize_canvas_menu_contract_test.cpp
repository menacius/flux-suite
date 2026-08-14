#include <iostream>
#include <sstream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing grouped-child/canvas-menu contract: " << label
              << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::cerr << "usage: grouped_child_resize_canvas_menu_contract_test "
                     "<canvas-cpp> <canvas-h> <editor-cpp>\n";
        return 2;
    }
    const std::string canvas = read_file(argv[1]);
    const std::string canvas_h = read_file(argv[2]);
    const std::string editor = read_file(argv[3]);

    bool ok = true;
    ok &= require(canvas, "editor_canvas_delta_to_parent_local",
                  "canvas-to-parent local delta conversion");
    ok &= require(canvas, "QTransform start_xf = editor_parent_world_transform",
                  "resize begins from full parent/world transform");
    ok &= require(canvas, "emit flip_layers_requested(true)",
                  "canvas flip action");
    ok &= require(canvas, "emit rotate_layers_requested(-90.0)",
                  "canvas rotate action");
    ok &= require(canvas, "emit layer_order_requested(2)",
                  "canvas bring-to-front action");
    ok &= require(canvas, "emit add_layers_to_group_requested",
                  "canvas add-to-group action");
    ok &= require(canvas, "editor_snap_excluded_layer_ids",
                  "group parent excluded from child snapping");
    ok &= require(canvas, "Double-click drills into grouped artwork",
                  "group child double-click drill-down");
    ok &= require(canvas, "geometry.group_contexts",
                  "parent group context box without handles");
    ok &= require(canvas_h, "void layer_order_requested(int action)",
                  "canvas layer-order signal");
    ok &= require(canvas_h, "void set_layers_locked_requested(bool locked)",
                  "canvas lock signal");
    ok &= require(editor, "void TitleEditor::reorder_selected_layers(int action)",
                  "editor layer-order implementation");
    ok &= require(editor, "editor_selection_with_group_descendants",
                  "group descendants stay together during ordering");
    ok &= require(editor, "editor_reorder_selected_group_siblings",
                  "layer order remains scoped to the group");
    ok &= require(editor, "CanvasPreview::duplicate_layers_requested",
                  "canvas menu wiring");

    return ok ? 0 : 1;
}
