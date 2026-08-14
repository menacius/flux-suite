#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string read_file(const char *path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Cannot read: " << path << '\n';
        return {};
    }
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

bool require(const std::string &source, const std::string &needle,
             const char *label)
{
    if (source.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing editor/timeline revision 141 contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool absent(const std::string &source, const std::string &needle,
            const char *label)
{
    if (source.find(needle) == std::string::npos)
        return true;
    std::cerr << "Obsolete editor/timeline revision 141 contract remains: "
              << label << " (" << needle << ")\n";
    return false;
}

bool theme_icon(const std::string &source, const char *label)
{
    bool ok = true;
    ok &= require(source, "currentColor", label);
    ok &= absent(source, "rgb(74, 85, 101)", label);
    ok &= absent(source, "#4a5565", label);
    ok &= absent(source, "#4A5565", label);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 10) {
        std::cerr << "usage: editor_timeline_revision_141_contract_test "
                     "<timeline> <editor-events> <layers> <visible> <hidden> "
                     "<matte> <lock> <unlock> <build-info>\n";
        return 2;
    }

    const std::string timeline = read_file(argv[1]);
    const std::string editor_events = read_file(argv[2]);
    const std::string layers = read_file(argv[3]);
    const std::string visible = read_file(argv[4]);
    const std::string hidden = read_file(argv[5]);
    const std::string matte = read_file(argv[6]);
    const std::string lock = read_file(argv[7]);
    const std::string unlock = read_file(argv[8]);
    const std::string build_info = read_file(argv[9]);

    bool ok = true;

    ok &= require(timeline, "constexpr int kLayerTrimHandleVisualWidth = 8",
                  "larger visible strip trim handles");
    ok &= require(timeline, "constexpr int kLayerTrimHitWidth = 12",
                  "larger strip trim hit zones");
    ok &= require(timeline,
                  "std::min(in_distance, out_distance) <= kLayerTrimHitWidth",
                  "outer strip edges take interaction priority");
    ok &= require(timeline,
                  "p.fillRect(x0, y + 3, kLayerTrimHandleVisualWidth",
                  "left visual strip handle uses shared width");
    ok &= require(timeline,
                  "p.fillRect(x1 - kLayerTrimHandleVisualWidth",
                  "right visual strip handle uses shared width");

    ok &= require(timeline,
                  "if (transition_hit_at_pos(pos, &existing_hit) && existing_hit.layer",
                  "whole existing transition is a replacement drop target");
    ok &= require(timeline,
                  "preview.duration = replaced\n                        ? replaced->duration",
                  "replacement preview retains old duration");
    ok &= require(editor_events,
                  "const double replacement_duration = existing",
                  "replacement keeps the authored transition duration");
    ok &= require(editor_events,
                  "? existing->duration : transition.duration",
                  "new presets use their own duration only without replacement");

    ok &= require(timeline, "const bool deleted_text_transition",
                  "text transition deletion is identified before erase");
    ok &= require(timeline,
                  "synchronize_text_transition_animators(\n            layer->transitions, layer->text_animators",
                  "text transition deletion removes the managed animator");
    ok &= require(timeline, "clear_keyframe_selection();",
                  "stale generated keyframe selection is cleared");

    ok &= require(timeline,
                  "update(QRect(0, 0, width(), ruler_height()))",
                  "playhead movement repaints the complete ruler band");
    ok &= require(timeline,
                  "const QRect line_rect(playhead_x - 10, ruler_height(), 20",
                  "playhead body repaint remains narrow");
    ok &= absent(timeline, "QRect timecode_rect(playhead_x + 8, 2, 96, 18)",
                 "narrow timecode-only dirty region");

    ok &= require(layers, "add_header_icon(\"visibility-normal.svg\"",
                  "visibility header uses supplied glyph");
    ok &= require(layers,
                  "make_toggle(\"visibility-normal.svg\", \"no-visibility.svg\"",
                  "normal and hidden states use supplied glyphs");
    ok &= require(layers, "obs_icon(\"visibility-matte.svg\")",
                  "matte-only state uses supplied glyph");
    ok &= require(layers,
                  "make_toggle(\"layer-lock.svg\", \"layer-unlock.svg\"",
                  "lock states remain theme-rendered icons");

    ok &= theme_icon(visible, "normal visibility icon");
    ok &= theme_icon(hidden, "hidden visibility icon");
    ok &= theme_icon(matte, "matte visibility icon");
    ok &= theme_icon(lock, "lock icon");
    ok &= theme_icon(unlock, "unlock icon");

    ok &= require(build_info, "FXM_DEVELOPMENT_VERSION",
                  "centralized development version metadata");

    return ok ? 0 : 1;
}
