from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
api = (ROOT / 'src/extensions/fxm-plugin-api.h').read_text()
catalog_h = (ROOT / 'src/extensions/effect-extension-catalog.h').read_text()
catalog_cpp = (ROOT / 'src/extensions/effect-extension-catalog.cpp').read_text()
effects_panel = (ROOT / 'src/effects/effects-panel.cpp').read_text()
layer_model = (ROOT / 'Core/layer-model.h').read_text()
props_h = (ROOT / 'src/editor/properties-panel.h').read_text()
timeline = (ROOT / 'src/timeline/timeline-widget.cpp').read_text()
popup = (ROOT / 'src/editor/properties-panel/popup-state.inc').read_text()
sync = (ROOT / 'src/editor/properties-panel/property-synchronization.inc').read_text()
readme = (ROOT / 'README.md').read_text()
changelog = (ROOT / 'docs/CHANGELOG.md').read_text()

def require(text, needle, label):
    assert needle in text, f'missing {label}: {needle}'

# Development Version 243 metadata is the new current release.
require((ROOT / 'CMakeLists.txt').read_text(), 'OBS_FXM_DEVELOPMENT_VERSION "243"', 'CMake version')
require((ROOT / 'src/core/build-info.h').read_text(), 'FXM_DEVELOPMENT_VERSION "243"', 'runtime version')
require((ROOT / 'Core/title-serialization-schema.h').read_text(), 'kCurrentDevelopmentVersion = 243', 'schema version')
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')
require(readme, 'Development Version 243 fixes parented/direct-image Color Overlay raster invalidation', 'README current summary')

# Strip-length locked Video/Audio range workflow.
for needle in [
    'layer_timeline_span_seconds',
    'normalize_layer_media_range_to_timeline_span',
    'set_layer_media_range_in_point',
    'set_layer_media_range_out_point',
]:
    require(layer_model, needle, f'range helper {needle}')
assert 'add_form_row(video_form, QStringLiteral("Source")' not in popup
assert 'lbl_video_range_preview_' not in props_h
assert 'btn_video_set_in_' not in props_h
require(popup, 'video_range_layout->addWidget(spn_video_in_', 'video range in compact row')
require(popup, 'video_range_layout->addWidget(spn_video_out_', 'video range out compact row')
require(popup, 'lbl_audio_range_preview_', 'audio range preview label')
require(sync, 'spn_video_in_->setValue(layer_->video_in_point)', 'compact video range in sync')
require(sync, 'spn_video_out_->setValue(layer_->video_out_point)', 'compact video range out sync')
require(timeline, 'set_layer_media_range_in_point(*layer, dragged.start_media_in + media_delta)', 'timeline start trim media range')
require(timeline, 'set_layer_media_range_out_point(*layer, dragged.start_media_out + media_delta)', 'timeline end trim media range')

# Public Modular Visual Effects SDK ABI v4 and metadata contracts.
for needle in [
    '#define FXM_PLUGIN_API_VERSION_4 4u',
    'typedef struct fxm_effect_descriptor_v4',
    'fxm_effect_backend_v4',
    'fxm_effect_color_space_v4',
    'fxm_effect_alpha_contract_v4',
    'parameter_metadata_json',
    'custom_property_widgets_json',
    'render_passes_json',
    'inputs_json',
    'auxiliary_inputs_json',
    'layer_references_json',
    'requirements_json',
    'state_serialization_json',
    'fxm_plugin_can_unload_v4_fn',
    'fxm_plugin_query_v4_fn',
]:
    require(api, needle, f'ABI v4 symbol {needle}')

for needle in [
    'parameterMetadata', 'customPropertyWidgets', 'renderPasses', 'auxiliaryInputs',
    'layerReferences', 'declaredColorSpace', 'declaredAlphaContract',
    'declaredInputCount', 'cpuWorkerOnly', 'multiPass', 'pluginPath',
    'quarantineEntries', 'blacklistEntries', 'clearQuarantine', 'blacklistPath',
    'retainPluginInstance', 'releasePluginInstance'
]:
    require(catalog_h, needle, f'catalog SDK field/control {needle}')

for needle in [
    'std::async(std::launch::async',
    'FXM_EFFECT_PLUGIN_PATH',
    'quarantine.json',
    'blacklist.json',
    'crash-reports',
    'fxm_plugin_query_v4',
    'scan exception',
    'FXM_EFFECT_BACKEND_CPU_WORKER_ONLY',
    'FXM_EFFECT_BACKEND_GPU_MULTI_PASS',
    'activeInstances->value(it->providerId) <= 0',
    'can_unload',
    'before_unload'
]:
    require(catalog_cpp, needle, f'safe scanner/runtime {needle}')

# Effects browser exposes rescan/quarantine controls and richer plugin badges.
for needle in ['Rescan plugins', 'Clear quarantine', 'catalog.rescan()', 'catalog.clearQuarantine()', 'MULTI-PASS', 'WORKER CPU']:
    require(effects_panel, needle, f'effects browser control {needle}')

# Developer guide and samples are included in the source tree.
for rel in [
    'docs/visual-effects-sdk.md',
    'sdk/visual-effects/README.md',
    'sdk/visual-effects/samples/gpu-invert/manifest.fxm-effect.json',
    'sdk/visual-effects/samples/gpu-invert/invert.effect',
    'sdk/visual-effects/samples/native-v4/sample_effect_plugin.cpp',
]:
    assert (ROOT / rel).exists(), f'missing SDK artifact {rel}'

print('Development Version 243 media range and Modular Visual Effects SDK contract: PASS')
