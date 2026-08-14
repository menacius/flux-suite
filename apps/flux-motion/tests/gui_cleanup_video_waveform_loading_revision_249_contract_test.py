#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def assert_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
    props_h = read("src/editor/properties-panel.h")
    popup = read("src/editor/properties-panel/popup-state.inc")
    sync = read("src/editor/properties-panel/property-synchronization.inc")
    video_h = read("Core/title-video-runtime.h")
    video_cpp = read("Core/title-video-runtime.cpp")
    compat = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc")
    gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    build = read("src/core/build-info.h")

    if not any(v in build for v in [f'FXM_DEVELOPMENT_VERSION "{v}"' for v in range(249, 300)]):
        raise AssertionError("missing revision bump for 249/250/251")

    assert_contains(props_h, "live_playback_box_", "live playback box compatibility member")
    assert_contains(popup, "Live Playback", "live playback transport setup")
    assert "add_form_row(video_form, fxm_tr(\"OBSTitles.AssetPlayback\"), cmb_video_playback_)" not in popup, \
        "video playback combo must not remain in Video Layer section"

    for field in ["row_audio_source_", "row_audio_preview_", "row_audio_range_", "row_audio_range_tools_"]:
        assert_contains(props_h, field, f"audio duplicate row member {field}")
        assert_contains(sync, f"set_form_row_visible({field}, !is_video)", f"hide duplicate row {field} for video audio")
    assert_contains(sync, "set_form_row_visible(cmb_audio_playback_, !is_video)", "hide duplicate audio playback for video")
    assert_contains(sync, "set_form_row_visible(chk_audio_independent_, !is_video)", "hide duplicate independent playback for video")

    assert_contains(props_h, "bar_video_loading_", "video loading progress bar")
    assert_contains(props_h, "bar_audio_waveform_", "waveform progress bar")
    assert_contains(video_h, "struct VideoFrameLoadingStatus", "runtime loading status struct")
    assert_contains(video_cpp, "FrameRuntime::loading_status_for_layer", "runtime loading status implementation")
    assert_contains(sync, "loading_status_for_layer(*layer_)", "inspector video progress binding")
    assert_contains(sync, "audio_waveform_progress_percent", "inspector waveform progress binding")

    assert_contains(compat, "make_video_loading_placeholder_raster", "video loading placeholder raster")
    assert_contains(compat, "Loading video…", "placeholder user message")
    assert_contains(compat, "video-loading|", "placeholder cache identity")
    assert_contains(gpu, "make_video_loading_placeholder_raster", "direct GPU placeholder fallback")
    assert_contains(read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc"), "video_loading_progress_percent", "placeholder progress cache metadata")
    assert_contains(read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc"), "video-loading-progress", "placeholder progress cache key")

    print("revision 249 GUI/video/waveform loading contract OK")


if __name__ == "__main__":
    main()
