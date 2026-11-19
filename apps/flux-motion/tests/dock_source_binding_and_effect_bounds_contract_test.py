from pathlib import Path
root = Path(__file__).resolve().parents[1]
dock = (root / "src/editor/title-dock/dock-lifecycle.inc").read_text(encoding="utf-8")
dock_header = (root / "src/editor/title-dock.h").read_text(encoding="utf-8")
obs_host = (root / "../../plugins/obs/flux-motion/src/obs-editor-host.cpp").read_text(encoding="utf-8")
effects = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc").read_text(encoding="utf-8")
resources = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc").read_text(encoding="utf-8")
live_text = (root / "src/editor/title-dock/live-text-cache-playlist.inc").read_text(encoding="utf-8")
live_helpers = (root / "src/editor/title-dock/template-library-helpers.inc").read_text(encoding="utf-8")
assert "cue->setEnabled(title_has_bound_host_source(title->id));" in dock

# OBS publishes source lifecycle events on a global signal bus. Only Flux
# Motion sources affect dock binding state, and a scene-switch burst must be
# reduced to one leading and one settling refresh instead of two jobs per
# source event.
assert 'calldata_ptr(event_data, "source")' in obs_host
assert "std::strcmp(source_id, kTitleSourceId) != 0" in obs_host
assert "host_state_refresh_timer_->setInterval(0);" in dock
assert "host_state_settle_timer_->setInterval(75);" in dock
assert "QMetaObject::invokeMethod(" in dock
assert "Qt::QueuedConnection" in dock
assert "!host_state_refresh_timer_->isActive()" in dock
assert "host_state_settle_timer_->start();" in dock
assert "QTimer::singleShot(75" not in dock
assert "host_state_refresh_timer_" in dock_header
assert "host_state_settle_timer_" in dock_header

# Ordinary scene activation/show must retain the published frame and clean
# state. Revision checks in video_tick still dirty real model changes.
activate = resources[resources.index("static void source_activate"):]
activate = activate[:activate.index("static void source_deactivate")]
show = resources[resources.index("static void source_show"):]
assert "data->dirty = true;" not in activate.split("cue_first_live_text_row_on_activation", 1)[0]
assert "data->first_frame_pending = true;" not in activate.split("cue_first_live_text_row_on_activation", 1)[0]
assert "data->dirty = true;" not in show
assert "data->first_frame_pending = true;" not in show

# Cue-list authoring refreshes the values of the active row without creating
# an Uncue transport revision.
helper = live_helpers[live_helpers.index("static void refresh_active_live_text_row_without_recue"):]
helper = helper[:helper.index("static QString live_cue_image_file_name")]
assert "title->current_cue_row" in helper
assert "cue_revision" not in helper
assert "cue_uncue_requested" not in helper
assert live_text.count("refresh_active_live_text_row_without_recue(title);") >= 9
assert "max_rich_text_stroke_width(layer, t)" in effects
assert "const ShadowRenderParams shadow = evaluated_shadow_params(layer, t);" in effects
assert "bounds.translated(shadow.dx, shadow.dy)" in effects
assert "shadow.long_length" in effects
print("dock source binding and effect bounds contract: ok")
