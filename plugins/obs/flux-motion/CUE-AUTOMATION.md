# Cue automation in OBS

The OBS plugin exposes source-local Cue and Uncue commands. Uncue plays the
remaining outro and applies the title's configured cue-end behavior. Calling
Uncue again while the outro runs does not skip or restart it. Cue captures the
latest authored title, including edits held back from Program.

## Advanced Scene Switcher

In a macro, add **Source → Press settings button**, choose the Flux Motion
source, then choose **Cue** or **Uncue**. These are ordinary OBS source property
buttons, so no keyboard shortcut needs to be assigned. They are also available
in the source's Properties dialog, side by side on the **Settings buttons** row.

Advanced Scene Switcher's implementation of this action is in its
[source action](https://github.com/WarmUpTill/SceneSwitcher/blob/master/plugins/base/macro-action-source.cpp).

The existing Flux Motion title hotkeys remain available for title-wide
automation, including selecting a row and routing a title to Program. Source
buttons/API control the selected OBS source; they do not route scenes or change
the cue of a separate Duplicate Source.

## Uncue before a scene transition

Enable **Uncue before scene transition** in each participating Flux source's
Properties. This per-source option is disabled by default and is saved by OBS
with the source, including across scene-collection/session reloads.

Then enable the master switch: **Settings → Uncue before scene transition** in
Titles and Graphics, or **Tools → Flux Motion Preferences → Scene transitions →
Enable per-source uncue before scene transitions**. These two controls share one
persistent global preference, disabled by default.

Both switches must be enabled. For automatic transitions, only opted-in sources
in the outgoing scene finish their outros before OBS runs the selected
transition. Other sources stay cued. This includes opted-in titles in scenes
shared with the destination, groups and nested scenes. OBS's manual T-bar
is left under the user's control. If no source participates, OBS transitions
immediately. Source participation is sampled when the transition begins.

Native plugins can read/write the standard OBS source setting
`uncue_before_scene_transition`. This preference only controls automatic uncue;
explicit Cue/Uncue commands continue to work regardless of either toggle.

The outgoing scene and its audio stay live during the wait. The original
transition resumes afterward, including its configured duration. A stalled
outro has a timeout based on the remaining title duration. Turning off the
preference during a wait resumes the transition immediately. Scene-collection
cleanup cancels the wait and releases all retained scene references.

## Native OBS plugin API

Call the selected source's public OBS `proc_handler_t`. Calls queue transport
work for the next source video tick and do not require the OBS UI thread.
Keep an OBS source reference alive throughout the call.

| Procedure | Input | Output |
| --- | --- | --- |
| `flux_motion_cue` | `cue_row`: zero-based row, or `-1` for last/first row | `accepted`: command queued |
| `flux_motion_uncue` | none | `accepted`: command queued |
| `flux_motion_get_cue_state` | none | `active`, `ending`, `pending`, `remaining_seconds`, `cue_row`, `pending_changes` |

```cpp
calldata_t params;
calldata_init(&params);
calldata_set_int(&params, "cue_row", -1);
bool supported = proc_handler_call(obs_source_get_proc_handler(source),
                                  "flux_motion_cue", &params);
bool queued = supported && calldata_bool(&params, "accepted");
calldata_free(&params);
```

Use a fresh `calldata_t` and `flux_motion_uncue` to release the source. Poll
`flux_motion_get_cue_state` until both `active` and `pending` are false to wait
for completion; `ending` identifies an outro in progress. `accepted` acknowledges
queueing, not completion or row validity. A subsequent command replaces an
unconsumed command. Out-of-range cue rows are ignored. These procedures are
rejected on private editor transport sources.

Source property button identifiers are `flux_motion_cue` and
`flux_motion_uncue`. Property-button callers should use OBS's
`obs_property_button_clicked` API.

`pending_changes` refers to edits withheld from the currently on-air `cue_row`;
editing other rows does not mark that cue as Pending.
