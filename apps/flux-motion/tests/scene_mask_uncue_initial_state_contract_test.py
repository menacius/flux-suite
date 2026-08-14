from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc').read_text(encoding='utf-8')
resources = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc').read_text(encoding='utf-8')
transitions = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc').read_text(encoding='utf-8')

assert 'if (!data || data->waiting_for_cue)' in runtime
assert 'apply_source_cue_end_state' in resources
assert 'source-created-cue-end-state' in resources
assert 'source-updated-cue-end-state' in resources
assert 'behavior == 1' in resources and 'behavior == 2' in resources
assert transitions.count('release_active_scene_mask_scenes(data);') >= 3
assert transitions.count('data->waiting_for_cue = true;') >= 3
print('scene mask uncue and initial cue-end state contract: ok')
