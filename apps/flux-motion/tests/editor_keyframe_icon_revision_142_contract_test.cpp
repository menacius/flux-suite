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
    std::cerr << "Missing editor revision 142 contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool absent(const std::string &source, const std::string &needle,
            const char *label)
{
    if (source.find(needle) == std::string::npos)
        return true;
    std::cerr << "Obsolete editor revision 142 contract remains: " << label
              << " (" << needle << ")\n";
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
    if (argc != 11) {
        std::cerr << "usage: editor_keyframe_icon_revision_142_contract_test "
                     "<hierarchy> <timeline> <layers> <lock> <unlock> <hidden> "
                     "<matte-visibility> <matte-source> <matte-destination> <build-info>\n";
        return 2;
    }

    const std::string hierarchy = read_file(argv[1]);
    const std::string timeline = read_file(argv[2]);
    const std::string layers = read_file(argv[3]);
    const std::string lock = read_file(argv[4]);
    const std::string unlock = read_file(argv[5]);
    const std::string hidden = read_file(argv[6]);
    const std::string matte_visibility = read_file(argv[7]);
    const std::string matte_source = read_file(argv[8]);
    const std::string matte_destination = read_file(argv[9]);
    const std::string build_info = read_file(argv[10]);

    bool ok = true;

    ok &= require(hierarchy,
                  "static bool layer_keyframe_sections_expanded(const Layer &layer)",
                  "one shared keyframe-section expansion predicate");
    ok &= require(hierarchy,
                  "const bool show_properties = layer_keyframe_sections_expanded(*layer);",
                  "timeline row model consumes the shared predicate");
    ok &= require(layers,
                  "if (!layer_keyframe_sections_expanded(*l)) continue;",
                  "layer list consumes the shared predicate");
    ok &= require(layers,
                  "const bool expanded = !is_expandable_container && layer_keyframe_sections_expanded(*l);",
                  "layer-list caret reflects the shared predicate");
    ok &= require(timeline,
                  "if (!entry.is_property || !entry.prop) continue;",
                  "keyframe marquee selection follows visible property rows");
    ok &= require(timeline,
                  "An empty property row is also a valid start for a marquee spanning",
                  "marquee can begin on one timeline and span other timelines");
    ok &= require(timeline,
                  "const bool property_row = row >= 0 && row < (int)rows.size() &&",
                  "property-row marquee retains the selected layer context");
    ok &= require(timeline,
                  "return entry.is_property && entry.prop && test_prop(entry.prop);",
                  "collapsed strips have no hidden aggregate keyframe hit targets");
    ok &= absent(timeline,
                 "else if (!layer->properties_expanded)",
                 "timeline-only aggregate keyframe painting");
    ok &= absent(timeline,
                 "for (auto prop : timeline_properties(*entry.layer))",
                 "timeline-only aggregate keyframe hit loop");

    ok &= absent(layers, "add_header_icon(\"matte-source.svg\"",
                 "obsolete separate matte-source header icon");
    ok &= require(layers, "add_header_icon(\"matte-alpha.svg\"",
                  "matte channel header icon");
    ok &= require(layers, "add_header_icon(\"matte-normal.svg\"",
                  "matte inversion header icon");
    ok &= require(layers, "add_matte_indicator(\"matte-source.svg\"",
                  "matte-source row icon");
    ok &= require(layers, "add_matte_indicator(\"matte-destination.svg\"",
                  "matte-destination row icon");
    ok &= require(layers,
                  "make_toggle(\"layer-lock.svg\", \"layer-unlock.svg\"",
                  "lock states remain on the OBS-theme icon path");
    ok &= require(layers, "set_layer_row_button_icon(vis, \"no-visibility.svg\")",
                  "hidden visibility state uses supplied icon");
    ok &= require(layers, "set_layer_row_button_icon(vis, \"visibility-matte.svg\")",
                  "matte-only visibility state uses supplied icon");

    ok &= theme_icon(lock, "lock icon");
    ok &= theme_icon(unlock, "unlock icon");
    ok &= theme_icon(hidden, "hidden visibility icon");
    ok &= theme_icon(matte_visibility, "matte visibility icon");
    ok &= theme_icon(matte_source, "matte source icon");
    ok &= theme_icon(matte_destination, "matte destination icon");

    ok &= require(lock, "M45.3,27.3h-.7v-6",
                  "supplied lock geometry");
    ok &= require(unlock, "M17 10.25H8.75V8",
                  "supplied unlock geometry");
    ok &= require(matte_source, "M32,16.7c-8.5,0-15.3,6.9",
                  "supplied matte-source geometry");
    ok &= require(matte_destination, "M7.416 6.078C10.418",
                  "supplied matte-destination geometry");

    ok &= require(build_info, "FXM_DEVELOPMENT_VERSION",
                  "centralized development version metadata");

    return ok ? 0 : 1;
}
