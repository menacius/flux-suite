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

void require_absent(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "obsolete UI remains: " << needle << '\n';
        assert(false);
    }
}

void require_before(const std::string &source, const char *first, const char *second)
{
    const auto first_pos = source.find(first);
    const auto second_pos = source.find(second);
    if (first_pos == std::string::npos || second_pos == std::string::npos || first_pos >= second_pos) {
        std::cerr << "expected order: " << first << " before " << second << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 6);
    const std::string panel = read_file(argv[1]);
    const std::string editor = read_file(argv[2]);
    const std::string gradient = read_file(argv[3]);
    const std::string modern_controls = read_file(argv[4]);
    const std::string layer_stack = read_file(argv[5]);

    require(editor, "Dock panels are constructed after the central canvas");
    require(editor, "EffectsPanel::extension_canvas_handles_changed");
    require(editor, "CanvasPreview::extension_canvas_handle_moved");
    require(editor, "Qt::UniqueConnection");

    require(panel, "build_effect_settings_panel(index)");
    require(panel, "apply_effect_panel_order");
    require(panel, "new FxmSwitch(panel)");
    require(panel, "addHeaderLeadingWidget(enabled_switch)");
    require(panel, "addHeaderWidget(more_button)");
    require(panel, "OBSTitles.DuplicateEffect");
    require(panel, "OBSTitles.DeleteEffect");
    require(panel, "OBSTitles.MoveEffectUp");
    require(panel, "OBSTitles.MoveEffectDown");
    require(panel, "btn_respect_masks_ = add_button");
    require(panel, "box->setProperty(\"fxmPreservePanelMargins\", true)");
    require(panel, "form->setContentsMargins(10, 0, 10, 10)");
    require_before(panel, "layout->addWidget(settings_scroll, 1)",
                   "layout->addWidget(button_bar)");
    require_absent(panel, "EffectsStackListWidget");
    require_absent(panel, "currentRowChanged(-1)");
    require_absent(panel, "OBSTitles.EffectSettings");

    require(modern_controls, "expanded/%1/%2");
    require(modern_controls, "order/%1");
    require(modern_controls, "draw_caret(painter");
    require(modern_controls, "emit source->orderChanged()");

    require(layer_stack, "new FxmCaretButton(row_widget)");
    require(layer_stack, "caret->caretState()");

    require(panel, "NumericDragLabel(QStringLiteral(\"X\")");
    require(panel, "NumericDragLabel(QStringLiteral(\"Y\")");
    require(panel, "layout->addWidget(yLabel)");
    require(panel, "layout->addWidget(y, 1)");

    require(gradient, "constrain_radial_fill");
    require(gradient, "maximum_focal_distance");
    require(gradient, "radius * 0.98");
    require(gradient, "assign_center(gradient_drag_.center + delta)");
    require(gradient, "assign_focal(gradient_drag_.focal + delta)");
    require(gradient, "const double radius = std::max(1.0, QLineF(gradient_drag_.center, local).length())");
    if (gradient.find("radial_max_radius") != std::string::npos ||
        gradient.find("clamp_to_radial_box") != std::string::npos) {
        std::cerr << "radial center/radius constraints were not restored\n";
        assert(false);
    }

    std::cout << "effects interaction regression contract: PASS\n";
    return 0;
}
