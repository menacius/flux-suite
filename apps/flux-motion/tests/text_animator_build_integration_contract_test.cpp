#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string read_all(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file && "contract input must exist");
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    const std::string properties_panel = read_all(argv[1]);
    const std::string animator_controls = read_all(argv[2]);
    const std::string gpu_text = read_all(argv[3]);

    const auto sync = properties_panel.find(
        "#include \"properties-panel/property-synchronization.inc\"");
    const auto selection = properties_panel.find(
        "#include \"properties-panel/selection-refresh.inc\"");
    const auto animator = properties_panel.find(
        "#include \"properties-panel/text-animator-controls.inc\"");
    assert(sync != std::string::npos);
    assert(selection != std::string::npos);
    assert(animator != std::string::npos);
    /* The construction/synchronization include chain intentionally spans the
     * PropertiesPanel constructor. Full Text Animator member definitions must
     * only be emitted after that chain has returned to file scope. */
    assert(sync < selection && selection < animator);
    assert(animator_controls.find("namespace {") != std::string::npos);
    assert(animator_controls.find(
        "void PropertiesPanel::build_text_animator_section") !=
        std::string::npos);
    assert(animator_controls.find("copy.transition_managed = false") !=
           std::string::npos);
    assert(animator_controls.find("transition.id == removed.transition_id") !=
           std::string::npos);

    assert(gpu_text.find(
        "fxm::ticker_runtime::sample(\n            session->title.id, layer,") !=
        std::string::npos);
    assert(gpu_text.find("\n            title.id, layer,") == std::string::npos);

    std::cout << "text animator build integration contract passed\n";
    return 0;
}
