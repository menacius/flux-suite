#!/usr/bin/env python3
"""Development Version 243 source contract: Video time remap + frame interpolation."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def assert_in(haystack: str, needle: str, context: str) -> None:
    assert needle in haystack, f"missing {needle!r} in {context}"


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
model = read("Core/layer-model.h")
title_data = read("src/core/title-data.cpp")
video_h = read("Core/title-video-runtime.h")
video_cpp = read("Core/title-video-runtime.cpp")
audio_cpp = read("../../plugins/obs/flux-motion/src/title-audio-runtime.cpp")
properties_h = read("src/editor/properties-panel.h")
popup = read("src/editor/properties-panel/popup-state.inc")
sync = read("src/editor/properties-panel/property-synchronization.inc")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
graph = read("src/timeline/temporal-graph-editor.inc")
actions = read("src/editor/properties-panel/auto-style-and-property-actions.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
manifest = json.loads(read("tests/test-suite-manifest.json"))

import re
assert re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(24[2-9]|25[0-9]|2[6-9][0-9]|[3-9][0-9]{2,})"', cmake), 'development version must remain >=243'
assert re.search(r'FXM_DEVELOPMENT_VERSION "(24[2-9]|25[0-9]|2[6-9][0-9]|[3-9][0-9]{2,})"', build_info), 'build info version must remain >=243'
assert re.search(r'kCurrentDevelopmentVersion = (24[3-9]|[3-9][0-9]{2,})', schema), 'schema version must remain >=243'
assert_in(schema, "case 243:", "title-serialization-schema.h")
assert_in(schema, "Video time-remap and interpolation", "title-serialization-schema.h")

for token in [
    "enum class VideoFrameInterpolationMode",
    "enum class VideoTimeRemapAudioMode",
    "struct VideoTimeRemapLoopSegment",
    "video_time_remap_enabled",
    "AnimatedProperty video_source_time",
    "video_time_remap_loop_segments",
    "video_frame_interpolation",
    "video_optical_flow_enabled",
    "video_time_remap_curve_fingerprint",
    "video_optical_flow_cache_fingerprint",
]:
    assert_in(model, token, "layer-model.h")
    field = token.split()[-1] if " " in token and not token.startswith("enum") and not token.startswith("struct") else None
    if field and field.startswith("video_"):
        assert_in(title_data, field, "title-data.cpp")

for field in [
    'j["video_time_remap_enabled"]',
    'j["video_source_time"]',
    'j["video_time_remap_loop_segments"]',
    'j["video_time_remap_audio_mode"]',
    'j["video_frame_interpolation"]',
    'j["video_optical_flow_enabled"]',
    'j["video_time_remap_curve_fingerprint"]',
    'j["video_optical_flow_cache_fingerprint"]',
]:
    assert_in(title_data, field, "title-data.cpp serialization")

for token in [
    "struct VideoTimeRemapSample",
    "evaluate_video_time_remap",
    "time_remap_curve_fingerprint_for_layer",
    "time_moving_backward",
    "freeze_section",
    "draft_preview_fallback",
    "frame_blend_used",
    "motion_interpolation_used",
    "ensure_optical_flow_analysis_for_layer",
    "cancel_optical_flow_analysis_for_layer",
]:
    assert_in(video_h, token, "title-video-runtime.h")

for token in [
    "evaluate_video_time_remap",
    "time_remap_curve_fingerprint_for_layer",
    "time_moving_backward",
    "freeze_section",
    "draft_preview_fallback",
    "frame_blend_used",
    "motion_interpolation_used",
    "ensure_optical_flow_analysis_for_layer",
    "cancel_optical_flow_analysis_for_layer",
]:
    assert_in(video_cpp, token, "title-video-runtime.cpp")

for token in [
    "layer.video_time_remap_enabled",
    "VideoFrameInterpolationMode::NearestFrame",
    "VideoFrameInterpolationMode::FrameBlend",
    "VideoFrameInterpolationMode::MotionCompensated",
    "request.freeze_section",
    "request.moving_backward",
    "request.needs_interpolation",
    "interpolation_next_frame_number",
    "interpolation_alpha",
    "blend_video_frames",
    "time_remap_curve_fingerprint_for_layer(layer)",
    "requested_freeze_section",
    "!requested_freeze_section",
]:
    assert_in(video_cpp, token, "title-video-runtime.cpp remap/decode path")

for token in [
    "class OpticalFlowAnalysisRuntime",
    "worker_([this] { worker_loop(); })",
    "video-optical-flow",
    "curve_fingerprint",
    "background-only",
    "thread may ask whether a cache is ready",
    "draft_preview_fallback",
]:
    assert_in(video_cpp, token, "title-video-runtime.cpp optical-flow runtime")

for token in [
    "video_time_remap_enabled",
    "VideoTimeRemapAudioMode::PreserveLinearClipAudio",
    "VideoTimeRemapAudioMode::MuteReverseOrFreeze",
    "evaluate_video_time_remap",
    "remap.source_time",
]:
    assert_in(audio_cpp, token, "title-audio-runtime.cpp")

for token in [
    "chk_video_time_remap_",
    "cmb_video_interpolation_",
    "cmb_video_time_remap_audio_",
    "chk_video_optical_flow_",
    "lbl_video_optical_flow_status_",
]:
    assert_in(properties_h, token, "properties-panel.h")
    assert_in(popup, token, "popup-state.inc")
    assert_in(sync, token, "property-synchronization.inc")

for token in [
    "Motion compensated",
    "Background optical-flow analysis",
    "ensure_optical_flow_analysis_for_layer",
    "cancel_optical_flow_analysis_for_layer",
]:
    assert_in(popup, token, "properties popup controls")

for token in [
    "video_source_time",
    "Source Time",
    "Speed",
]:
    assert_in(hierarchy, token, "hierarchy-model graph-property exposure")

for token in [
    "Reverse source time",
    "source_speed < -1e-6",
    "prop.name() == \"video_source_time\"",
]:
    assert_in(graph, token, "temporal graph editor")

for token in [
    "video_source_time = AnimatedProperty",
    "video_time_remap_curve_fingerprint.clear()",
    "video_optical_flow_cache_fingerprint.clear()",
    "cancel_optical_flow_analysis_for_layer(video_id)",
]:
    assert_in(actions, token, "media replacement invalidation")

for token in [
    "video_source_time = AnimatedProperty",
    "video_time_remap_enabled = false",
    "video_optical_flow_cache_fingerprint.clear()",
]:
    assert_in(commands, token, "new video layer initialization")

assert_in(readme, "Development Version 243", "README.md")
assert_in(readme, "Video layer time animation and interpolation", "README.md")
assert_in(changelog, "Development Version 243 — Video Time Remapping and Frame Interpolation", "CHANGELOG.md")
assert_in(changelog, "Motion-compensated mode uses background-only optical-flow analysis", "CHANGELOG.md")

registered = manifest["areas"]
for area in [
    "editor_gui",
    "timeline_graph",
    "serialization_migration",
    "rendering_2d_3d",
    "cache_proxy_threading",
    "audio_transport",
    "shutdown_lifetime",
    "platform_build",
]:
    assert "tests/video_time_remap_frame_interpolation_revision_243_contract_test.py" in registered[area]["python"], area

print("Development Version 243 video time-remap/interpolation contract passed")
