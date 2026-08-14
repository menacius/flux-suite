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
void reject(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "unexpected: " << needle << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 7);
    const std::string canvas_h = read_file(argv[1]);
    const std::string canvas_view = read_file(argv[2]);
    const std::string canvas_events = read_file(argv[3]);
    const std::string editor_session = read_file(argv[4]);
    const std::string editor_events = read_file(argv[5]);
    const std::string signal_handlers = read_file(argv[6]);

    require(canvas_h, "void set_playhead(double t, bool playback_frame = false);");
    require(canvas_h, "void set_playback_active(bool active);");
    require(canvas_h, "bool present_gpu_display_if_due();");
    require(canvas_h, "void reset_live_playback_fps_measurement();");
    require(canvas_h, "void record_live_playback_present();");
    require(canvas_h, "QElapsedTimer render_stats_timer_;");
    require(canvas_view, "gs_swapchain_t *swapchain = nullptr;");
    require(canvas_view, "gpu_display_->swapchain = gs_swapchain_create(&info);");
    require(canvas_view, "gs_load_swapchain(gpu_display_->swapchain);");
    require(canvas_view, "gs_present();");
    require(canvas_view, "if (project_rate_present)\n            record_live_playback_present();");
    require(canvas_view, "reset_live_playback_fps_measurement();");
    require(canvas_view, "double CanvasPreview::live_playback_fps() const");
    reject(canvas_view, "obs_display_create(");
    reject(canvas_view, "obs_display_add_draw_callback(");
    require(canvas_view, "playback_frame_pending_ = playback_frame;");
    require(canvas_view, "playback_present_pending_ = playback_frame;");
    require(canvas_view, "editing_present_pending_ = !playback_frame;");
    require(canvas_view, "std::ceil(1000.0 / std::clamp(hz, 1.0, 1000.0))");
    require(canvas_events, "const bool canvas_transform_drag = drag_changed_ &&");
    require(canvas_events, "drag_mode_ == DragMode::Move || drag_mode_ == DragMode::Rotate");
    require(canvas_events, "editor_camera_transform_only_pending_ || canvas_transform_drag;");
    require(canvas_events, "const bool priority_edit_frame = realtime_transform ||");
    require(canvas_events, "? std::max(1, display_refresh_interval_ms_)");
    require(canvas_events, ": std::max(display_refresh_interval_ms_, render_interval_ms_);");
    require(canvas_events, "const int cadence_ms = playback_frame_pending_ ? 0 : editing_cadence_ms;");
    require(canvas_events, "!full_gpu_model_refresh_pending_");
    require(canvas_events, "render_interval_ms_ = std::clamp(cost_ms + 1, 1, 34);");
    require(canvas_events, "present_gpu_display_if_due();");

    require(editor_session, "return std::max(1.0 / 1000.0, obs_frame_duration());");
    require(editor_session, "std::floor(\n        editor_playback_ui_frame_duration() * 1000.0)");
    reject(editor_session, "std::lround(editor_playback_ui_frame_duration() * 1000.0)");
    require(editor_session, "gui_refresh_timer_->setInterval(16);");
    require(editor_session, "const bool pointer_drag = QApplication::mouseButtons() != Qt::NoButton;");
    require(editor_session, "!pointer_drag || dock_layout_transition_ || playing_");
    require(editor_session, "gui_refresh_timer_->stop();");
    require(editor_session, "clock_timer_->setInterval(100);");
    require(editor_session, "canvas_->refresh_runtime_dynamic_content();");
    reject(editor_session, "kMaxPlaybackUiHz");
    require(editor_events, "apply_playhead_change(t, false);");
    require(editor_events, "canvas_->set_playhead(t, playback_frame);");
    reject(editor_events, "canvas_->set_playhead(t, playing_);");
    require(signal_handlers, "std::ceil(1000.0 / hz)");
    require(signal_handlers, "apply_playhead_change(next_playhead, true);");
    require(signal_handlers, "void TitleEditor::reset_playback_timer_cadence()");
    require(signal_handlers, "void TitleEditor::schedule_next_playback_timer_interval()");
    require(signal_handlers, "playback_timer_fractional_error_ms_");
    require(signal_handlers, "const bool main_window_layout_transition = watched == this");
    require(signal_handlers, "const bool dock_structure_transition = watched_dock");
    require(signal_handlers, "const bool watched_in_editor = watched_canvas_window ||");
    require(signal_handlers, "if (type == QEvent::MouseButtonPress && watched_in_editor");
    require(signal_handlers, "gui_refresh_timer_->start();");
    reject(signal_handlers, "const bool editor_widget = watched_widget &&");
    require(canvas_view, "if (transport_playback_active_ && !playback_present_pending_ &&");
    require(canvas_view, "!editing_present_pending_");
    require(canvas_view, "playback_present_pending_ && !editing_present_pending_");
    require(canvas_view, "const int cadence_ms = project_rate_present ? 0 : display_refresh_interval_ms_;");
    require(canvas_view, "present_coalesce_timer_->start(");
    require(canvas_view, "void CanvasPreview::set_playback_active(bool active)");

    std::cout << "canvas refresh pacing contract: PASS\n";
    return 0;
}
