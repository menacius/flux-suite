#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime_h = (root / "../../plugins/obs/flux-motion/src/title-audio-runtime.h").read_text(encoding="utf-8")
runtime = (root / "../../plugins/obs/flux-motion/src/title-audio-runtime.cpp").read_text(encoding="utf-8")
source_tick = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc").read_text(encoding="utf-8")
source_create = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc").read_text(encoding="utf-8")
preview = (root / "src/editor/title-editor/editor-audio-preview.inc").read_text(encoding="utf-8")
scheduler = (root / "Core/audio-output-scheduler.h").read_text(encoding="utf-8")

# The private editor source is identified before SourceAudioRuntime starts.
assert "SourceAudioRuntime(obs_source_t *source, bool editor_preview = false)" in runtime_h
assert "source, data->editor_audio_preview" in source_create
assert source_create.index('data->editor_audio_preview = obs_data_get_bool(settings, "editor_audio_preview")') < source_create.index("std::make_unique<fxm::audio::SourceAudioRuntime>")

# Editor monitoring remains on the output worker, but uses its own sample-locked
# cadence rather than the buffered program-source scheduler.
assert "RealtimeMonitorCadence editor_monitor_cadence_;" in runtime_h
assert "editor-monitor-sample-locked-worker" in runtime
assert "Audio delivery is always performed by the dedicated output worker" in runtime
assert "output_cv_.notify_one();" in runtime
assert "video-tick-v178" not in runtime

# Regression from v185: deadline = now + block accumulated Windows timer
# oversleep and forced a 20 ms timestamp jump roughly every half second.
assert "class RealtimeMonitorCadence" in scheduler
assert "next_deadline_ns_ += block_ns_;" in scheduler
assert "Absolute cadence: do not use wall-clock `now` here" in scheduler
assert "editor_next_delivery_deadline_ns_ = now + block_ns" not in runtime
assert "timestamp_diff > block_ns * 2" not in runtime
assert "editor_next_output_timestamp_ns_ = now" not in runtime
assert "hard_resync_ns = 70000000ULL" in scheduler
assert "monitor-catchup" in runtime
assert "timestamp_mode=continuous" in runtime

# The startup safety margin remains three 10 ms packets, but normal timestamps
# can only advance by sample duration.
assert "prefill_packets = 3" in scheduler
assert "prefill_remaining_ == 0" in scheduler
assert "next_timestamp_ns_ += block_ns_;" in scheduler
assert "prefill_packets=3 deadline_mode=sample-locked" in runtime

# Editor audio remains audible independently of cue-end visual visibility.
assert """data->editor_audio_preview
            ? obs_source_active(data->source)""" in source_tick

# The private source remains monitor-only and diagnostics remain available.
assert "OBS_MONITORING_TYPE_MONITOR_ONLY" in preview
assert "[editor-preview-created]" in preview
assert "[FXM Audio][monitor-clock-reset]" in runtime
assert "[FXM Audio][monitor-hard-resync]" in runtime
assert "[FXM Audio][delivery-stall]" in runtime
assert "[FXM Audio][timestamp-gap]" in runtime
assert "[FXM Audio][flow]" in runtime

print("editor audio sample-locked cadence contract passed")
