#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_play_every_frame_varispeeds_editor_audio_instead_of_muting():
    preview = read("src/editor/title-editor/editor-audio-preview.inc")
    source_h = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
    runtime_h = read("../../plugins/obs/flux-motion/src/title-audio-runtime.h")
    runtime = read("../../plugins/obs/flux-motion/src/title-audio-runtime.cpp")
    gpu_tick = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    locale = read("data/locale/en-US.ini")

    assert "editor_audio_play_every_frame_speed_factor" in preview
    assert "timeline_seconds / wall_seconds" in preview
    assert "title_source_set_editor_transport(" in preview
    assert "editor_audio_play_every_frame_speed_factor);" in preview
    assert "obs_source_media_play_pause(editor_audio_preview_source_, !playing_)" in preview
    assert "Editor audio is skipped in Play Every Frame mode" not in preview
    assert "!playing_ || play_every_frame" not in preview
    assert "double audio_speed = 1.0" in source_h
    assert "std::atomic<double> editor_audio_speed {1.0}" in read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
    assert "editor_audio_speed.load(std::memory_order_acquire)" in gpu_tick
    assert "double playback_speed = 1.0" in runtime_h
    assert "playback_speed_ = std::clamp(playback_speed, 0.02, 4.0);" in runtime
    assert "mix_block(output_sample_cursor_, frames, reverse_, speed, left, right);" in runtime
    assert "advance_transport_cursor_scaled(output_sample_cursor_, frames, reverse_, speed);" in runtime
    assert "varispeeds editor audio to match the visual cadence" in locale


def test_cache_ui_hides_cache_only_elements_when_cache_disabled():
    prerender_h = read("src/cache/prerender-dock.h")
    prerender = read("src/cache/prerender-dock.cpp")

    assert "QWidget *cache_section_" in prerender_h
    assert "void setCacheControlsVisible(bool visible);" in prerender_h
    assert "cache_section_ = new QWidget(this);" in prerender
    assert "cached_only_ = new FxmSwitch" in prerender
    assert "void PrerenderDock::setCacheControlsVisible(bool visible)" in prerender
    assert "cache_section_->setVisible(visible);" in prerender
    assert "const bool enabled = CacheManager::instance().cacheEnabled();" in prerender
    assert "setCacheControlsVisible(enabled);" in prerender


if __name__ == "__main__":
    test_play_every_frame_varispeeds_editor_audio_instead_of_muting()
    test_cache_ui_hides_cache_only_elements_when_cache_disabled()
    print("development version 264 audio varispeed/cache visibility contracts passed")
