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
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 10);
    const std::string effects_cpp = read_file(argv[1]);
    const std::string effects_h = read_file(argv[2]);
    const std::string canvas_preview = read_file(argv[3]);
    const std::string canvas_events = read_file(argv[4]);
    const std::string editor_session = read_file(argv[5]);
    const std::string editor_playback = read_file(argv[6]);
    const std::string plugin = read_file(argv[7]);
    const std::string title_data_cpp = read_file(argv[8]);
    const std::string title_data_h = read_file(argv[9]);

    require(effects_h, "void begin_shutdown();");
    require(effects_h, "QJsonArray last_published_canvas_handles_");
    require(effects_cpp, "EffectsPanel::~EffectsPanel()");
    require(effects_cpp, "handles == last_published_canvas_handles_");
    require(effects_cpp, "for (QTimer *timer : findChildren<QTimer *>())");
    require(effects_cpp, "disconnect(child, nullptr, this, nullptr)");

    require(canvas_preview, "void CanvasPreview::prepare_for_shutdown()");
    require(canvas_preview, "render_coalesce_timer_->stop()");
    require(canvas_preview, "present_coalesce_timer_->stop()");
    require(canvas_preview, "adaptive_full_quality_timer_->stop()");
    require(canvas_preview, "destroy_gpu_display();");
    require(canvas_preview, "gs_swapchain_destroy(gpu_display_->swapchain)");
    require(canvas_preview, "title_gpu_render_session_destroy(gpu_render_session_)");
    require(canvas_preview, "canvas->shutting_down_");
    require(canvas_events, "handles == extension_canvas_handles_");

    require(editor_session, "void TitleEditor::begin_shutdown()");
    require(editor_session, "for (QTimer *timer : findChildren<QTimer *>())");
    require(editor_session, "disconnect(effects_panel_, nullptr, canvas_, nullptr)");
    require(editor_session, "effects_panel_->begin_shutdown()");
    require(editor_session, "canvas_->prepare_for_shutdown()");
    require(editor_playback, "if (shutting_down_)\n        return;");

    require(plugin, "static bool g_frontend_exiting = false;");
    require(plugin, "if (!g_frontend_exiting)\n        obs_frontend_remove_event_callback");
    require(plugin, "destroy_dock_ui(!g_frontend_exiting)");
    require(plugin, "g_frontend_exiting = true;");
    require(plugin, "destroy_dock_ui(true)");
    require(plugin, "TitleDataStore::instance().shutdownSaveWorker();\n    TitleDataStore::instance().save();");
    require(title_data_h, "void shutdownSaveWorker() const;");
    require(title_data_cpp, "void TitleDataStore::shutdownSaveWorker() const");
    require(title_data_cpp, "pending_save_.reset()");
    require(title_data_cpp, "save_thread_.join()");

    std::cout << "shutdown/performance regression contract: PASS\n";
    return 0;
}
