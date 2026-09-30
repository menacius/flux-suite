from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc').read_text(encoding='utf-8')
resources = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc').read_text(encoding='utf-8')
transitions = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc').read_text(encoding='utf-8')

assert 'if (!data || data->waiting_for_cue)' in runtime
assert 'apply_source_cue_end_state' in resources
assert 'source-created-cue-end-state' in resources
assert 'source-updated-cue-end-state' in resources
source_update = resources.split('static void source_update(', 1)[1].split(
    'static bool cue_first_live_text_row_on_activation(', 1)[0]
assert 'if (updated_title && title_changed)' in source_update
assert 'else if (!updated_title && title_changed)' in source_update
assert 'if (title_changed && updated_title)' in source_update
assert 'reload_source_project(data, title_changed);' in source_update
assert 'clear_live_cue_runtime_for_source(data, previous_title_id);' in source_update
assert source_update.index('if (title_changed) {') < source_update.index(
    'clear_live_cue_runtime_for_source(data, previous_title_id);')
assert 'behavior == 1' in resources and 'behavior == 2' in resources
assert transitions.count('release_active_scene_mask_scenes(data);') >= 3
assert transitions.count('data->waiting_for_cue = true;') >= 3
print('scene mask uncue and initial cue-end state contract: ok')
