#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static std::string read_file(const char *path)
{
    std::ifstream in(path);
    if (!in) throw std::runtime_error(std::string("cannot read ") + path);
    std::ostringstream out; out << in.rdbuf(); return out.str();
}
static void require(const std::string &s, const std::string &needle)
{
    if (s.find(needle) == std::string::npos)
        throw std::runtime_error(std::string("missing: ") + needle);
}
int main()
{
    const auto lifecycle = read_file("src/editor/title-dock/dock-lifecycle.inc");
    const auto header = read_file("src/editor/title-dock.h");
    const auto runtime = read_file("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc");
    const auto cmake = read_file("CMakeLists.txt");
    require(header, "void refresh_title_list_cue_visual_states();");
    require(lifecycle, "refresh_title_list_cue_visual_states();");
    require(lifecycle, "title_cue_visual_state(*title, waiting_for_prerender)");
    require(lifecycle, "item->setData(kTitleCueVisualStateRole, state)");
    require(lifecycle, "bool visual_state_changed = false;");
    require(lifecycle, "visual_state_changed = true;");
    require(lifecycle, "if (visual_state_changed && list_->viewport())");
    require(lifecycle, "list_->viewport()->update();");
    if (lifecycle.find("visualItemRect") != std::string::npos)
        throw std::runtime_error("visualItemRect repaint path must not return");
    require(runtime, "title->current_cue_row = -1;");
    require(runtime, "title->cue_uncue_requested = false;");
    require(runtime, "TitleDataStore::instance().touch_runtime_change();");
    require(cmake, "set(OBS_FXM_DEVELOPMENT_VERSION");
    std::cout << "title thumbnail ending cleanup contract: PASS\n";
}
