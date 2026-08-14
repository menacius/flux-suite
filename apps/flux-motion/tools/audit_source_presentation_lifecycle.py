#!/usr/bin/env python3
"""Structural checks for OBS source presentation lifecycle isolation.

These checks do not replace an OBS runtime test. They prevent regressions in the
contracts that make a texture from an old title/scene collection ineligible for
Preview/Program presentation.
"""
from __future__ import annotations

from pathlib import Path
from source_bundle import read_source_bundle
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = read_source_bundle(ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
HEADER = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.h").read_text(encoding="utf-8")
PLUGIN = (ROOT / "../../plugins/obs/flux-motion/src/plugin-main.cpp").read_text(encoding="utf-8")

checks: list[tuple[str, bool]] = []

def check(name: str, condition: bool) -> None:
    checks.append((name, condition))

check(
    "frontend collection transition has an explicit begin/end contract",
    "title_source_begin_scene_collection_transition" in HEADER
    and "title_source_end_scene_collection_transition" in HEADER
    and "g_source_scene_collection_transition" in SOURCE,
)

cleanup_pos = PLUGIN.find("OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP")
begin_pos = PLUGIN.find("title_source_begin_scene_collection_transition", cleanup_pos)
save_pos = PLUGIN.find("TitleDataStore::instance().save()", cleanup_pos)
check(
    "collection cleanup blocks source presentation before saving outgoing titles",
    cleanup_pos >= 0 and begin_pos > cleanup_pos and save_pos > begin_pos,
)

changed_pos = PLUGIN.find("OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED")
load_pos = PLUGIN.find("TitleDataStore::instance().load()", changed_pos)
end_pos = PLUGIN.find("title_source_end_scene_collection_transition", changed_pos)
check(
    "new title store loads while source presentation remains blocked",
    changed_pos >= 0 and load_pos > changed_pos and end_pos > load_pos,
)

def span(start_marker: str, end_marker: str) -> str:
    start = SOURCE.find(start_marker)
    if start < 0:
        return ""
    end = SOURCE.find(end_marker, start)
    return SOURCE[start:] if end < 0 else SOURCE[start:end]

render_body = span(
    "static void source_video_render",
    "static void add_scene_list_items",
)
check(
    "video_render rejects collection-transition and generation-stale frames",
    "g_source_scene_collection_transition.load" in render_body
    and "source_presentation_generation_is_current" in render_body,
)

tick_body = span(
    "static void source_video_tick",
    "static void apply_layer_world_transform_gs",
)
check(
    "video_tick applies a hard reset before resolving the current title",
    tick_body.find("apply_source_presentation_reset") >= 0
    and tick_body.find("apply_source_presentation_reset")
        < tick_body.find("TitleDataStore::instance().get_title"),
)
check(
    "missing/restored title states invalidate the old poster",
    '"title-missing"' in tick_body and '"title-restored"' in tick_body,
)
check(
    "visual identity changes reset the source and use a per-source model revision",
    "title-visual-identity-changed" in tick_body
    and "visual_model_revision" in tick_body
    and "data->playhead, revision" not in tick_body,
)
invalidate_body = span(
    "void title_gpu_render_session_invalidate_presentation",
    "void title_gpu_render_session_destroy",
)
check(
    "hard invalidation makes the old published texture unreachable",
    "session->final_texture = nullptr" in invalidate_body
    and "session->active_presentation_target = -1" in invalidate_body
    and "session->use_submitted_final = false" in invalidate_body
    and "session->use_base_frame = false" in invalidate_body,
)
check(
    "hard invalidation discards the old model identity",
    "session->has_title = false" in invalidate_body
    and "session->model_revision = std::numeric_limits<uint64_t>::max()" in invalidate_body,
)

apply_body = span(
    "static void apply_source_presentation_reset",
    "static void set_source_output_visible",
)
check(
    "scene-mask source references are released at the same generation boundary",
    "release_active_scene_mask_scenes(data)" in apply_body,
)

scene_mask_sync = span(
    "static void sync_scene_mask_scenes_for_cue",
    "static int live_text_playlist_row_count",
)
check(
    "hidden sources cannot keep scene-mask scenes active",
    "data->output_visible" in scene_mask_sync,
)

update_body = span(
    "static void source_update",
    "static void source_activate",
)
check(
    "source settings/title rebinding cannot draw the prior presentation",
    "source-title-binding-changed" in update_body
    and "source-settings-updated" in update_body,
)
show_hide_body = span(
    "static void source_show",
    "static uint32_t source_get_width",
)
check(
    "OBS show/hide visibility boundaries invalidate retained source posters",
    'request_source_presentation_reset(data, "source-shown")' in show_hide_body
    and 'request_source_presentation_reset(data, "source-hidden")' in show_hide_body
    and "shown_on_display" in show_hide_body
    and "si.show           = source_show" in SOURCE
    and "si.hide           = source_hide" in SOURCE,
)

check(
    "visibility transitions use the reset helper instead of raw runtime assignments",
    tick_body.count("data->output_visible =") == 0
    and "set_source_output_visible" in tick_body,
)

render_session_body = span(
    "static gs_texture_t *render_gpu_session_locked",
    "void title_gpu_render_session_render",
)
check(
    "retired layers do not preserve a stale complete compositor frame",
    "const bool retiring" in render_session_body
    and "!ready && !retiring" in render_session_body,
)
check(
    "GPU fallback texture is bound to the exact published model identity",
    "gpu_session_final_matches_model" in SOURCE
    and "published_model_revision" in SOURCE
    and "published_title_id" in SOURCE
    and "published_model_revision = session->model_revision" in SOURCE
    and "published_title_id = session->title.id" in SOURCE,
)

print("Source presentation lifecycle structural audit")
failed = 0
for name, ok in checks:
    print(f"  {'PASS' if ok else 'FAIL'}: {name}")
    failed += 0 if ok else 1
print(f"Result: {len(checks) - failed} passed, {failed} failed")
sys.exit(1 if failed else 0)
