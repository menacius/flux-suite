#!/usr/bin/env python3
"""Development Version 242 Video proxy, decode cache and hardware acceleration contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
read = lambda rel: (ROOT / rel).read_text(encoding="utf-8")

model = read("Core/layer-model.h")
data = read("src/core/title-data.cpp")
schema = read("Core/title-serialization-schema.h")
video_h = read("Core/title-video-runtime.h")
video_cpp = read("Core/title-video-runtime.cpp")
props = read("src/editor/properties-panel/auto-style-and-property-actions.inc")
layout = read("src/editor/title-editor/layout-template-tools.inc")
cache_cpp = read("src/cache/cache-manager.cpp")
disk_cache = read("src/cache/cache-manager/disk-cache-storage.inc")
manifest = json.loads(read("tests/test-suite-manifest.json"))
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(24[2-9]|25[0-9]|2[6-9][0-9]|[3-9][0-9]{2,})"', read("CMakeLists.txt"))
assert re.search(r'FXM_DEVELOPMENT_VERSION "(24[2-9]|25[0-9]|2[6-9][0-9]|[3-9][0-9]{2,})"', read("src/core/build-info.h"))
assert re.search(r"kCurrentDevelopmentVersion = (24[2-9]|[3-9][0-9]{2,})", schema) and "case 242:" in schema

for field in (
    "video_has_hdr",
    "video_source_fingerprint",
    "video_proxy_path",
    "video_proxy_fingerprint",
    "video_proxy_profile",
    "video_proxy_complete",
    "video_proxy_alpha",
    "video_proxy_hdr",
    "video_proxy_audio_preserved",
    "video_proxy_progress_percent",
    "video_proxy_generating",
):
    assert field in model, field
    assert f'j["{field}"]' in data, field

assert "fingerprint_string_for_media_source(layer_->video_source)" in props
assert "video_proxy_path.clear()" in props and "video_proxy_complete = false" in props
assert "video_proxy_generating = false" in props
assert "Video replacement must always pass through probe_media()" in props
assert "layer_->video_has_hdr = media.has_hdr" in props

for token in (
    "VideoSourceFingerprint",
    "fingerprint_for_media_source",
    "fingerprint_string_for_media_source",
    "VideoProxyStatus",
    "proxy_status_for_layer",
    "proxy_progress_for_title",
    "ensure_proxy_for_layer",
    "cancel_proxy_for_layer",
    "delete_proxy_for_layer",
    "pause_proxy_generation",
    "resume_proxy_generation",
    "proxy_generation_paused",
):
    assert token in video_h and token in video_cpp, token

# Proxy generation: background-only, disk-only, title/layer progress, cancel, pause/resume, auto relink,
# source fingerprint, alpha/HDR profiles, audio stream copy preservation and source-only invalidation.
for token in (
    "class VideoProxyRuntime",
    "worker_([this] { worker_loop(); })",
    "QStandardPaths::findExecutable(QStringLiteral(\"ffmpeg\"))",
    "write_proxy_manifest",
    "manifest_matches_source",
    "proxy_reference_usable",
    "proxy_path_for(layer, fingerprint)",
    "QFile::remove(state.proxy_path)",
    "progress_percent",
    "proxy_progress_for_title",
    "cancel(const std::string &layer_id)",
    "delete_proxy_for_layer(const Layer &layer)",
    "pause()",
    "resume()",
    "audio_stream_preservation",
    "-c:a",
    "copy",
    "yuva444p10le",
    "smpte2084",
    "source_fingerprint",
):
    assert token in video_cpp, token

assert "source_changed = state.source_fingerprint != fingerprint" in video_cpp
assert "transform" in model.lower() and "non-baked effects must never invalidate" in model
assert "source-fingerprint:" in video_cpp and ";proxy:" in video_cpp
assert "resolve_decode_path" in video_cpp and "served_from_proxy" in video_cpp

# Decode cache: playhead-window cache, different editor/live budgets, active request priority,
# forward/reverse prefetch, cache-aware scrubbing and release hooks.
for token in (
    "VideoDecodeClient::Editor",
    "VideoDecodeClient::LiveOutput",
    "decode_cache_budget_for_client",
    "prefetch_plan_for_client",
    "requested_reverse",
    "cached_decoded_frame_locked",
    "nearest_cached_frame_at_or_before_locked",
    "decoded_frame_lru",
    "decoded_frame_cache",
    "requested_generation",
    "frameReady",
    "forget_layer",
    "clear()",
):
    assert token in video_cpp, token
assert "seconds = client == VideoDecodeClient::Editor ? 6 : 3" in video_cpp
assert "plan.forward" in video_cpp and "plan.reverse" in video_cpp
assert "entry->requested_generation != generation" in video_cpp
assert "FrameRuntime::instance().forget_layer(id)" in layout
assert "FrameRuntime::instance().clear()" in disk_cache
assert "QFile::remove(proxy_manifest_path_for_proxy(path))" in video_cpp
assert '#include "title-video-runtime.h"' in cache_cpp

# Hardware decode: platform abstraction, software fallback, reduced-copy transfer path and non-fatal failures.
for token in (
    "HardwareDecodeBackend",
    "HardwareDecodeStatus",
    "setHardwareDecodeBackend",
    "hardwareDecodeStatus",
    "platform_default_hardware_backend",
    "hw_type_for_backend",
    "avcodec_get_hw_config",
    "av_hwdevice_ctx_create",
    "get_hw_format",
    "av_hwframe_transfer_data",
    "force_software_for_source",
    "HardwareDecodeBackend::SoftwareOnly",
    "fell_back_to_software",
    "failure_reason",
):
    assert token in video_h or token in video_cpp, token
assert "open(next_path, requested_stream, HardwareDecodeBackend::SoftwareOnly)" in video_cpp and "software_ok" in video_cpp
assert "hardware frame transfer failed; falling back to software on next open" in video_cpp
assert "hardware_decode_attempted" in video_h and "hardware_decode_used" in video_h

assert manifest["development_version"] >= 242
for area in ("editor_gui", "rendering_2d_3d", "cache_proxy_threading", "audio_transport", "shutdown_lifetime", "platform_build"):
    assert "tests/video_proxy_decode_hardware_revision_242_contract_test.py" in manifest["areas"][area]["python"], area
assert "Development Version 242" in readme and "Video proxy" in readme
assert re.search(r"# v0.8.11-alpha — Development Version (24[3-9]|[3-9][0-9]{2,})", changelog)
assert "Development Version 242 — Video Proxy, Decode Cache and Hardware Acceleration" in changelog

print("Development Version 242 video proxy/decode-cache/hardware contract: PASS")
