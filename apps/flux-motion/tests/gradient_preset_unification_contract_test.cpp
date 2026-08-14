#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require_contains(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing gradient preset contract: " << label << " (" << needle << ")\n";
    return false;
}

bool require_absent(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Obsolete gradient preset path remains: " << label << " (" << needle << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 7) {
        std::cerr << "usage: gradient_preset_unification_contract_test <style-h> <style-cpp> "
                     "<gradient-construction> <gradient-editing> <styles-ui> <locale>\n";
        return 2;
    }

    const std::string style_h = read_file(argv[1]);
    const std::string style_cpp = read_file(argv[2]);
    const std::string construction = read_file(argv[3]);
    const std::string editing = read_file(argv[4]);
    const std::string styles_ui = read_file(argv[5]);
    const std::string locale = read_file(argv[6]);

    bool ok = true;
    ok &= require_contains(style_h, "makeGradientPreset(const RichTextFill &fill", "preset creation from any active fill target");
    ok &= require_contains(style_h, "gradientPresetToFill", "shared payload-to-fill conversion");
    ok &= require_contains(style_h, "static void subscribe", "cross-surface preset synchronization");
    ok &= require_contains(style_cpp, "style-presets/styles.json", "single persistent preset store");
    ok &= require_contains(style_cpp, "drawCheckerboard", "transparent gradient swatch preview");
    ok &= require_contains(style_cpp, "notifyChanged", "live preset library notifications");
    ok &= require_contains(style_cpp, "QGradientStops", "multi-stop swatch preview");
    ok &= require_contains(construction, "ResponsiveSwatchGrid(preset_box, 28, 4)", "color-swatch-style gradient layout");
    ok &= require_contains(construction, "SaveGradientPreset", "visible gradient preset creation control");
    ok &= require_contains(editing, "StylePresetLibrary::gradientPresetToFill", "preset application in fill/stroke popup");
    ok &= require_contains(editing, "inline_text_fill || inline_text_stroke", "inline rich-text target support");
    ok &= require_contains(editing, "stroke ? layer_->stroke_gradient_stops : layer_->gradient_stops", "fill and stroke preset capture");
    ok &= require_contains(editing, "StylePresetLibrary::subscribe(&popup", "popup live synchronization");
    ok &= require_absent(editing, "auto make_preset =", "hard-coded local preset strip");
    ok &= require_contains(styles_ui, "StylePresetKind::Gradient", "Gradient Styles dock uses shared library");
    ok &= require_contains(locale, "OBSTitles.SaveGradientPreset", "localized gradient preset action");

    return ok ? 0 : 1;
}
