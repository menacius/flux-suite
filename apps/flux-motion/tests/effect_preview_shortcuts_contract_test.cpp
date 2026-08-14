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

void forbid(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "forbidden: " << needle << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 15);
    const std::string title_source = read_file(argv[1]);
    const std::string transition_preview = read_file(argv[2]);
    const std::string title_editor = read_file(argv[3]);
    const std::string lens_flare = read_file(argv[4]);
    const std::string vignette = read_file(argv[5]);
    const std::string noise = read_file(argv[6]);
    const std::string roughen = read_file(argv[7]);
    const std::string detail = read_file(argv[8]);
    const std::string glare = read_file(argv[9]);
    const std::string halation = read_file(argv[10]);
    const std::string finishing = read_file(argv[11]);
    const std::string keying = read_file(argv[12]);
    const std::string source_effects = read_file(argv[13]);
    const std::string cache_manager = read_file(argv[14]);

    require(title_source, "set_effect_float_param(effect, \"angle\", resolved.effect_angle);");
    require(title_source, "set_effect_float_param(effect, \"falloff\", resolved.effect_falloff);");
    require(title_source, "effect.effect_secondary_color");
    require(title_source, "effect.effect_complexity");
    require(title_source, "gpu-effects-v17-keying-matte");

    require(lens_flare, "uniform float2 texelSize;");
    require(lens_flare, "float flare_disc");
    require(lens_flare, "uniform float ghostCount;");
    require(lens_flare, "technique Draw");
    forbid(lens_flare, "break;");
    require(vignette, "float aspect");
    require(vignette, "technique Draw");
    require(noise, "float profile_noise");
    require(noise, "technique Draw");
    forbid(noise, "break;");
    require(roughen, "float a1");
    require(roughen, "technique Draw");
    forbid(roughen, "ddx(");
    forbid(roughen, "ddy(");
    forbid(roughen, "break;");
    require(detail, "uniform texture2d blurredImage;");
    require(detail, "technique Sharpen");
    require(glare, "technique GlareComposite");
    require(halation, "technique HalationComposite");
    require(finishing, "technique ChromaticAberration");
    require(finishing, "technique Threshold");
    require(keying, "technique ChromaKey");
    require(keying, "technique SpillSuppression");
    require(keying, "technique MatteChoker");
    require(source_effects, "technique LightWrap");
    require(source_effects, "technique DisplacementMap");
    require(source_effects, "uniform texture2d sourceImage;");
    require(source_effects, "uniform int inputIsComposition;");

    require(transition_preview, "procedural_preview_mask");
    require(transition_preview, "LayerTransitionType::Blocks");
    require(transition_preview, "LayerTransitionType::ImageWipe");
    require(transition_preview, "LayerTransitionType::Clock");
    require(transition_preview, "LayerTransitionType::Iris");
    require(transition_preview, "LayerTransitionType::GradientWipe");
    require(transition_preview, "QPainter::CompositionMode_DestinationIn");

    require(title_editor, "register_editor_shortcut");
    require(title_editor, "addAction(action);");
    require(title_editor, "focus_in_editor");
    require(title_editor, "modal_child_active");
    require(title_editor, "editor_is_active");
    require(title_editor, "key_event->matches(QKeySequence::Save)");
    require(title_editor, "key_event->matches(QKeySequence::Copy)");
    require(title_editor, "key_event->matches(QKeySequence::Paste)");
    require(title_editor, "act_snap_enabled_->toggle();");
    require(title_editor, "key_event->key() == Qt::Key_Y");
    require(title_editor, "ev->key() == Qt::Key_Y");
    require(title_editor, "key_event->key() == Qt::Key_Space");
    require(title_editor, "QEvent::ShortcutOverride");
    require(title_editor, "plain_tool_key");
    require(title_editor, "QApplication::mouseButtons() == Qt::NoButton");
    require(title_editor, "group_selected_layers();");
    require(title_editor, "ungroup_selected_layers();");
    require(title_editor, "qobject_cast<QPlainTextEdit *>(widget)");
    forbid(title_editor, "event->type() == QEvent::KeyPress && isActiveWindow()");

    require(cache_manager, "v43-obs-effect-entrypoint-fix");

    std::cout << "procedural effects / transition preview / editor shortcuts contract: PASS\n";
    return 0;
}
