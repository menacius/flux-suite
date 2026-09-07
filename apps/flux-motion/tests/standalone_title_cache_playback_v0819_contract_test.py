"""Standalone title setup and cache/playback regression contract."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")


header = read("src/editor/title-editor.h")
editor = read("src/editor/title-editor/playback-cache-preferences.inc")
transport = read("src/editor/title-editor/layout-template-tools.inc")
connections = read("src/editor/title-editor/commands-docks.inc")
standalone = read("Editor/main.cpp")
cache_h = read("src/cache/cache-manager.h")
cache = read("src/cache/cache-manager/cache-policy-invalidation.inc")
dock_h = read("src/cache/prerender-dock.h")
dock = read("src/cache/prerender-dock.cpp")
canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")
canvas_resize = read("src/canvas/canvas-preview/gpu-frame-rendering.inc")
gpu_text = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
cmake = read("CMakeLists.txt")
version = read("VERSION.txt").strip()

assert "StandaloneNewTitleSettings" in header
for token in ("NewTitle/Duration", "NewTitle/Width", "NewTitle/Height",
              "NewTitle/FrameRate", "TimecodeSpinBox", "QDoubleSpinBox"):
    assert token in editor
assert "prompt_standalone_new_title_settings" in standalone
assert "apply_standalone_new_title_settings" in standalone

assert "titleFullyReadyForPlayback" in cache_h
assert "bool CacheManager::titleFullyReadyForPlayback" in cache
assert "pending_play_after_prerender_" in header
assert "queueWholeTimeline(title_)" in transport
assert "try_start_playback_after_prerender();" in connections

for removed in ("ClearRamCache", "ClearDiskCache", "PausePrerender",
                "CacheWorkArea", "CacheUsageDiagnostics", "liveCueStats"):
    assert removed not in dock
assert "ClearAllCache" in dock and "CacheEntireTimeline" in dock
assert "cacheWorkAreaRequested" not in dock_h
assert "diagnostics_" not in dock_h

assert "QTimer::singleShot(50, canvas, retry)" in canvas
assert "update_gpu_display_textures(nullptr, nullptr, nullptr)" in canvas_resize
assert "if (final_frame_readback_only())" in gpu_text
assert "text_renderer->compile_effect()" in gpu_text
assert "settle_loaded_title_layout" in editor

assert "project(flux-motion VERSION 0.8.19)" in cmake
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "420")' in cmake
assert version == "2026 - v0.8.19-2-alpha"

print("Standalone title/cache playback v0.8.19 contract passed")
