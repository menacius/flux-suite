#!/usr/bin/env python3
"""Source contracts for v188 mixer visibility and reverse editor audio."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text(encoding="utf-8")
create_update = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc").read_text(encoding="utf-8")
tick = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc").read_text(encoding="utf-8")
registration = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc").read_text(encoding="utf-8")
audio_h = (root / "../../plugins/obs/flux-motion/src/title-audio-runtime.h").read_text(encoding="utf-8")
audio_cpp = (root / "../../plugins/obs/flux-motion/src/title-audio-runtime.cpp").read_text(encoding="utf-8")
direction = (root / "Core/audio-transport-direction.h").read_text(encoding="utf-8")
editor_audio = (root / "src/editor/title-editor/editor-audio-preview.inc").read_text(encoding="utf-8")
editor_transport = (root / "src/editor/title-editor/layout-template-tools.inc").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build_info = (root / "src/core/build-info.h").read_text(encoding="utf-8")
schema = (root / "Core/title-serialization-schema.h").read_text(encoding="utf-8")

# Mixer presence follows actual Audio layers and uses the OBS audio-active API.
assert "title_has_audio_track" in runtime
assert "layer->type == LayerType::Audio" in runtime
assert "obs_source_set_audio_active(data->source, active)" in runtime
assert "sync_source_audio_active" in create_update
assert 'sync_source_audio_active(data, nullptr, "title-missing")' in tick
assert "seen_audio_membership_revision" in runtime
assert 'sync_source_audio_active(data, title, "title-revision-changed")' in tick

# The real editor preview is explicitly distinguished from stinger/private users.
assert 'obs_data_set_bool(settings, "editor_transport_controlled", true)' in editor_audio
assert 'obs_data_get_bool(settings, "editor_transport_controlled")' in create_update
assert "if (data->editor_transport_controlled)" in tick
assert "sync_editor_transport_override(data)" in tick

# Exact editor playhead and direction use a direct atomic source bridge.
assert "title_source_set_editor_transport" in editor_audio
assert "title_source_set_editor_transport" in runtime
assert "editor_transport_time.store" in runtime
assert "editor_transport_reverse.store" in runtime
assert "editor_transport_time.load" in runtime
assert "editor_transport_reverse.load" in runtime
assert "g_title_source_instances" in runtime
assert "sync_editor_audio_preview(true);" in editor_transport
assert "sync_editor_audio_preview(false);" in editor_transport

# The audio runtime carries direction through scheduling and reads descending samples.
assert "bool discontinuity = false, bool reverse = false" in audio_h
assert "bool reverse_ = false" in audio_h
assert "int64_t output_sample_cursor_ = 0" in audio_h
assert "transport_sample_cursor" in direction
assert "transport_sample_at" in direction
assert "direction_changed = reverse != reverse_" in audio_cpp
assert "scaled_offset" in audio_cpp
assert "start_sample - scaled_offset" in audio_cpp
assert "start_sample + scaled_offset" in audio_cpp
assert "advance_transport_cursor(output_sample_cursor_, frames, reverse_)" in audio_cpp
assert "editor_reverse_independent" in audio_cpp
assert "data->editor_playback_reverse" in registration
assert "data->playback_reverse" in tick

# Delivery identity and migration ledger.
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info
assert "kCurrentDevelopmentVersion = 243" in schema
assert "case 189:" in schema

print("PASS audio mixer visibility and reverse editor audio contracts")
