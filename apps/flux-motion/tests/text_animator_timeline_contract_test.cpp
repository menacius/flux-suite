#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string read_all(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const std::string hierarchy = read_all(argv[1]);
    const std::string timeline = read_all(argv[2]);
    assert(hierarchy.find("__textanim__") != std::string::npos);
    assert(hierarchy.find("layer.text_animators.animators") != std::string::npos);
    assert(hierarchy.find("selector.start") != std::string::npos);
    assert(hierarchy.find("selector.wiggly_frequency") != std::string::npos);
    assert(hierarchy.find("selector.completion") != std::string::npos);
    assert(hierarchy.find("selector.stagger_percent") != std::string::npos);
    assert(hierarchy.find("TimelinePropertyRef") != std::string::npos);
    assert(timeline.find("find_timeline_property") != std::string::npos);
    assert(timeline.find("copy_selected_keyframes") != std::string::npos);
    assert(timeline.find("paste_keyframes_at") != std::string::npos);
    assert(timeline.find("#include \"text-animator-presets.h\"") !=
           std::string::npos);
    /* Transition-duration and layer-trim drags must update the managed
     * animator continuously so the canvas does not freeze until mouse-up. */
    size_t sync_count = 0;
    size_t search_from = 0;
    while ((search_from = timeline.find(
                "synchronize_text_transition_animators", search_from)) !=
           std::string::npos) {
        ++sync_count;
        ++search_from;
    }
    assert(sync_count >= 3);
    std::cout << "text animator timeline contract passed\n";
    return 0;
}
