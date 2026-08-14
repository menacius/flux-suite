#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

schema = read('Core/title-serialization-schema.h')
title_cpp = read('src/core/title-data.cpp')
layer_model = read('Core/layer-model.h')
layer_effects = read('src/effects/layer-effects.h')
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')

assert 'kCurrentDevelopmentVersion = 244' in schema
assert 'Development Version 244' in readme
assert 'Development Version 244 — Serialization and Migration Audit' in changelog

# Effect/plugin serialization envelope.
for token in [
    'effect_id',
    'extension_loaded_schema_version',
    'extension_runtime_schema_version',
    'extension_explicit_migration',
    'external_plugin_id',
    'external_plugin_version',
    'external_plugin_binary_id',
    'extension_binary_state',
    'extension_binary_state_base64',
    'missing_plugin_placeholder',
    'effect_preset_id',
    'effect_preset_schema_version',
]:
    assert token in title_cpp or token in schema, token

for member in [
    'extension_provider_id',
    'extension_provider_version',
    'extension_plugin_binary_id',
    'extension_binary_state_json',
    'extension_binary_state_base64',
    'missing_plugin_placeholder',
    'effect_preset_id',
    'effect_preset_schema_version',
]:
    assert member in layer_effects, member

# Missing external plugin must be placeholder/preserve, not a dropped effect.
assert 'plugin missing; state preserved' in title_cpp
assert 'effect.missing_plugin_placeholder = true' in title_cpp
assert 'l->effects.push_back(effect)' in title_cpp

# Header helpers used by migration routines must be visible before use in all
# translation units, including MSVC source files that include only the schema header.
assert schema.index('inline json animated_scalar_default(double value);') < schema.index('migrate_serialization_audit_244')
assert schema.index('inline json animated_scalar_default(double value)\n{') > schema.index('migrate_serialization_audit_244')

# Built-in schema changes must be explicit-migration gated, not default-reset.
old_reset = 'effect = fxm::effects::make_default_layer_effect(effect.type);'
assert old_reset not in title_cpp
assert 'effect.extension_explicit_migration' in title_cpp
assert 'effect.extension_schema_version < descriptor->schema_version &&' in title_cpp
assert 'migrate_serialization_audit_244' in schema
assert 'Legacy Glow and Noise must not opt into revised runtime' in schema

# Video serialization audit fields.
for member in [
    'video_source_relative',
    'video_source_absolute',
    'video_media_root',
    'video_audio_stream_index',
    'video_selected_streams_json',
    'video_color_primaries',
    'video_color_transfer',
    'video_color_matrix',
    'video_color_range',
    'video_decode_settings_json',
    'video_prefer_hardware_decode',
    'video_allow_hardware_fallback',
    'video_decode_cache_policy',
]:
    assert member in layer_model, member
    assert member.replace('_json', '') in title_cpp or member in title_cpp, member

# Missing media should remain diagnostic-only. It may append diagnostics but not return/throw.
missing_idx = title_cpp.index('diagnostics && l->type == LayerType::Video')
missing_block = title_cpp[missing_idx:missing_idx + 500]
assert 'append_unique_import_diagnostic' in missing_block
assert 'return' not in missing_block
assert 'throw' not in missing_block

# Persistence guard: new-format save only after successful load/clean initialization.
assert 'persistence_ready_for_save_' in read('Core/title-data.h')
assert 'Skipped save because the title store did not load successfully' in title_cpp
assert 'persistence_ready_for_save_ = true' in title_cpp
assert 'persistence_ready_for_save_ = false' in title_cpp

print('Development Version 244 serialization/migration audit contract OK')
