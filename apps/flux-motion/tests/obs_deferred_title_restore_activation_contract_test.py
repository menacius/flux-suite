"""OBS collection restore must survive title-store/source callback ordering."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime = (
    root
    / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc"
).read_text(encoding="utf-8")
resources = (
    root
    / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc"
).read_text(encoding="utf-8")
tick = (
    root
    / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc"
).read_text(encoding="utf-8")

# A transient missing title must not apply the null-title "show nothing" state.
assert "bool        initial_title_state_pending = false;" in runtime
create = resources[resources.index("static void *source_create"):]
create = create[: create.index("static void source_destroy")]
assert "if (initial_title)" in create
assert "data->initial_title_state_pending = true;" in create
assert "apply_source_cue_end_state(\n            data, source_title_snapshot(data)" not in create
assert '"deferred-source-cue-end-state"' in tick

# Activation is retried after the collection store appears.
assert "bool        activation_cue_pending = false;" in runtime
assert "data->cue_first_row_when_active && !title" in resources
assert "if (data->activation_cue_pending)" in tick
assert "data->scene_mask_foreground_active" in tick[tick.index("if (data->activation_cue_pending)") :]

# Titles/graphics without exposed text still own the dock's synthetic cue row.
cue_helper = resources[resources.index("static bool cue_first_live_text_row_on_activation"):]
cue_helper = cue_helper[: cue_helper.index("static void source_activate")]
assert "if (!exposed.empty())" in cue_helper
assert "title->current_cue_row = 0;" in cue_helper
assert "Titles without exposed fields use the same synthetic row" in cue_helper

print("OBS deferred title restore and activation cue contract: ok")
