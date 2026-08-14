from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def block_between(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_development_version_and_manifest_are_294_without_pixel_cache_bump():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read("CMakeLists.txt")
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read("src/core/build-info.h")
    assert json.loads(read("tests/test-suite-manifest.json"))["development_version"] == 298
    # This delivery changes editor scheduling/lifecycle, not rendered pixels.
    assert '|gpu-text-pipeline=299' in read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")


def test_canvas_is_frame_driven_while_transport_and_inspectors_are_throttled():
    source = read("src/editor/title-editor/editor-events.inc")
    refresh = block_between(
        source,
        "void TitleEditor::refresh_playback_ui",
        "void TitleEditor::apply_playhead_change")
    apply = block_between(
        source,
        "void TitleEditor::apply_playhead_change",
        "void TitleEditor::on_title_modified")

    assert "std::clamp(display_refresh_hz_, 1.0, 30.0)" in refresh
    assert "kInspectorIntervalMs = 100" in refresh
    assert "kDiagnosticsIntervalMs = 250" in refresh
    assert "panel->isVisibleTo(this)" in refresh
    assert "timeline_->isVisibleTo(this)" in refresh
    assert "layers_->isVisibleTo(this)" in refresh
    assert "canvas_->set_playhead(t, playback_frame);" in apply
    assert "refresh_playback_ui(t, !playback_frame || !playing_);" in apply

    # Expensive panel fan-out must not return to the per-frame apply path.
    assert "timeline_->set_playhead(t);" not in apply
    assert "props_->update_playhead(t);" not in apply
    assert "effects_panel_->update_playhead(t);" not in apply
    assert "update_footer_diagnostics();" not in apply


def test_high_rate_passive_pointer_events_are_coalesced_without_breaking_input():
    source = read("src/editor/title-editor/signal-handlers.inc")
    event_filter = source[source.index("bool TitleEditor::eventFilter"):]

    assert "QEvent::MouseMove" in event_filter
    assert "QEvent::HoverMove" in event_filter
    assert "QApplication::mouseButtons() == Qt::NoButton" in event_filter
    assert "kPassivePointerIntervalMs = 33" in event_filter
    assert "playback_pointer_ui_clock_" in event_filter
    assert 'property("fxmContinuousPointerDuringPlayback")' in event_filter
    assert "canvas_pointer_target" in event_filter
    assert "timeline_pointer_target" in event_filter
    # Button, wheel and drag delivery remain in the normal filter path.
    assert "QEvent::MouseButtonPress" in event_filter
    assert "QEvent::Wheel" in event_filter


def test_editor_audio_preview_is_lazy_and_does_not_dirty_source_every_frame():
    source = read("src/editor/title-editor/editor-audio-preview.inc")
    helper = block_between(
        source,
        "bool editor_title_contains_audio_media_impl",
        "class FxmEditorAudioMeter")
    ensure = block_between(
        source,
        "void TitleEditor::ensure_editor_audio_preview",
        "void TitleEditor::release_editor_audio_preview")
    sync = block_between(
        source,
        "void TitleEditor::sync_editor_audio_preview",
        "void TitleEditor::publish_editor_audio_runtime_state")

    assert "layer->type == LayerType::Audio" in helper
    assert "layer->type == LayerType::Video" not in helper
    assert "LayerType::Asset" in helper
    assert "TitleDataStore::instance().get_title" in helper
    assert "!editor_title_contains_audio_media(title_)" in ensure
    assert "if (discontinuity || editor_audio_snapshot_dirty_)" in sync
    assert sync.count("title_source_set_editor_title_snapshot(") == 1
    assert "play_state_changed" in sync
    assert "if (discontinuity || play_state_changed)" in sync


def test_hidden_meter_redundant_ui_and_disabled_log_work_are_suppressed():
    audio = read("src/editor/title-editor/editor-audio-preview.inc")
    docks = read("src/editor/title-editor/commands-docks.inc")
    layout = read("src/editor/title-editor/layout-template-tools.inc")
    session = read("src/editor/title-editor/window-session.inc")
    sidebar = read("src/editor/tools-sidebar.cpp")
    logger_h = read("src/core/title-logger.h")
    logger_cpp = read("src/core/title-logger.cpp")

    assert "editor_audio_meter_->isVisibleTo(this)" in audio
    assert "editor_audio_meter_timer_->setInterval(16)" in audio
    assert "editor_audio_meter_timer_->setTimerType(Qt::PreciseTimer)" in audio
    assert "&QDockWidget::visibilityChanged" in docks
    assert "editor_audio_meter_timer_->stop();" in docks
    assert "status_diagnostics_label_->isVisibleTo(this)" in audio
    assert "status_diagnostics_label_->text() != diagnostics_text" in audio
    assert "title_lbl_->text() != title_text" in layout
    assert "windowTitle() != window_title" in layout

    clock_block = block_between(
        session,
        "clock_timer_->setInterval(100);",
        "clock_timer_->start();")
    assert "update_title_bar();" not in clock_block

    assert "foreground_color_ == color" in sidebar
    assert "background_color_ == color" in sidebar
    assert "bool wouldLog(" in logger_h
    assert "if (::TitleLogger::wouldLog" in logger_h
    assert "bool wouldLog(" in logger_cpp


def test_test_suite_manifest_version_is_future_safe():
    runner = read("tools/run_automated_test_suite.py")
    assert "def current_development_version" in runner
    assert "OBS_FXM_DEVELOPMENT_VERSION" in runner
    assert '"development_version": development_version' in runner
    assert "Development Version 239 automated test-suite runner" not in runner
    assert 'data.get("development_version") != current' in runner


def test_documentation_records_the_editor_gui_thread_audit():
    readme = read("README.md")
    changelog = read("docs/CHANGELOG.md")
    assert "Development Version 294" in readme
    assert "coalesced to 30 Hz" in readme
    assert changelog.startswith(
        "# v0.8.12-alpha — Development Version 299")
    assert "Editor playback and UI-event performance audit" in changelog
    assert "private source dirty on every playback frame" in changelog
