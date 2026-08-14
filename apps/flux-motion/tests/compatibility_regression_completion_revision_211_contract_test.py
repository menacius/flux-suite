from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

def require(source: str, *needles: str) -> None:
    for needle in needles:
        assert needle in source, f"missing compatibility contract: {needle}"

transform = read("src/rendering/layer-transform-3d.cpp")
serialization = read("src/core/title-data.cpp")
snapshot = read("Core/title-snapshot.h")
editor_tools = read("src/editor/title-editor/layout-template-tools.inc")
title_editor_source = read("src/editor/title-editor.cpp")
editor_header = read("src/editor/title-editor.h")
camera_panel = read("src/editor/title-properties-panel.cpp")
camera_header = read("src/editor/title-properties-panel.h")
persistence = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
cache_signature = read("src/cache/cache-manager/worker-publication.inc")
scene_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
audio_preview = read("src/editor/title-editor/editor-audio-preview.inc")
shutdown_contract = read("tests/shutdown_performance_regression_contract_test.cpp")

# Legacy 2D content must stay on the historical affine path.
require(transform,
    "if (!layer_or_ancestor_uses_3d(title, layer) || !layer_supports_3d(layer))",
    "return legacy_transform;")

# Complete 3D layer, camera, switch, assignment and depth serialization.
layer_write_tokens = (
    'j["dimension_mode"]', 'j["transform_axis_space"]', 'j["position_z"]',
    'j["position_3d_path_enabled"]', 'j["position_3d"]', 'j["rotation_x"]',
    'j["rotation_y"]', 'j["scale_z"]', 'j["anchor_z"]',
    'j["orientation_x"]', 'j["orientation_y"]', 'j["orientation_z"]',
    'j["camera_id"]', 'j["camera_assignment"]', 'j["depth_mode"]',
    'j["depth_test"]', 'j["write_to_depth"]', 'j["double_sided"]',
    'j["backface_culling"]')
layer_read_tokens = (
    'json_int(j, "dimension_mode"', 'json_int(j, "transform_axis_space"',
    'aprop_from_json(j["position_z"]', 'vec3_aprop_from_json(j["position_3d"]',
    'aprop_from_json(j["rotation_x"]', 'aprop_from_json(j["rotation_y"]',
    'aprop_from_json(j["scale_z"]', 'aprop_from_json(j["anchor_z"]',
    'aprop_from_json(j["orientation_x"]', 'aprop_from_json(j["orientation_y"]',
    'aprop_from_json(j["orientation_z"]', 'discrete_property_from_json(j["camera_assignment"]',
    'json_int(j, "depth_mode"', 'json_bool(j, "depth_test"',
    'json_bool(j, "write_to_depth"', 'json_bool(j, "double_sided"',
    'json_bool(j, "backface_culling"')
camera_tokens = (
    '{"position_x", aprop_to_json(camera.position_x)}',
    '{"position_y", aprop_to_json(camera.position_y)}',
    '{"position_z", aprop_to_json(camera.position_z)}',
    '{"target_x", aprop_to_json(camera.target_x)}',
    '{"target_y", aprop_to_json(camera.target_y)}',
    '{"target_z", aprop_to_json(camera.target_z)}',
    '{"orientation_x", aprop_to_json(camera.orientation_x)}',
    '{"orientation_y", aprop_to_json(camera.orientation_y)}',
    '{"orientation_z", aprop_to_json(camera.orientation_z)}',
    '{"rotation_x", aprop_to_json(camera.rotation_x)}',
    '{"rotation_y", aprop_to_json(camera.rotation_y)}',
    '{"rotation_z", aprop_to_json(camera.rotation_z)}',
    '{"focal_length", aprop_to_json(camera.focal_length)}',
    '{"field_of_view", aprop_to_json(camera.field_of_view)}',
    '{"zoom", aprop_to_json(camera.zoom)}',
    '{"near_clip", aprop_to_json(camera.near_clip)}',
    '{"far_clip", aprop_to_json(camera.far_clip)}',
    '{"projection_mode", aprop_to_json(camera.projection_mode)}',
    'result["position_3d"]', 'result["target_3d"]',
    'read_prop("orientation_x", camera.orientation_x)',
    'read_prop("rotation_z", camera.rotation_z)',
    'camera.projection_mode = aprop_from_json(j["projection_mode"], "camera_projection")',
    'vec3_aprop_from_json(j["position_3d"], camera.position_3d)',
    'vec3_aprop_from_json(j["target_3d"], camera.target_3d)',
    'jt["active_camera"]', 'jt["cameras"]', 'camera_to_json(camera)',
    'camera_from_json(jt["cameras"][i], i)')
for token in layer_write_tokens + layer_read_tokens + camera_tokens:
    assert token in serialization, f"missing 3D serialization token: {token}"

# Undo/redo restores the whole authored title, not a brittle field list, and
# publishes an authoritative visual/cache boundary afterwards.
require(snapshot,
    "target = clone_title_snapshot(snapshot);",
    "target.id = identity;",
    "target.render_camera_override_id = render_camera_override;",
    "target.proxy_metadata = proxy_metadata;",
    "valid_runtime_row", "std::clamp(playlist_next_row",
    "target.cue_revision = cue_revision + 1;")
require(editor_tools,
    "restore_title_authoring_snapshot(*title_, *snapshot);",
    "force_next_title_visual_update();",
    "schedule_cache_invalidation();",
    "publish_editor_audio_runtime_state();",
    "sync_editor_audio_preview(true);")
require(title_editor_source, '#include "title-snapshot.h"',
    '#include "title-editor/layout-template-tools.inc"')
assert title_editor_source.index('#include "title-snapshot.h"') < \
    title_editor_source.index('#include "title-editor/layout-template-tools.inc"')

# Cameras can be duplicated/copied/pasted, and layer clipboard payloads carry
# document-local camera definitions plus static and keyframed assignments.
require(camera_header, "std::unique_ptr<TitleCamera> camera_clipboard_;")
require(camera_panel,
    "Duplicate Camera", "Copy Camera", "Paste Camera",
    "TitleCamera camera = source;", "camera.id = TitleDataStore::make_uuid();")
require(editor_header,
    "std::vector<TitleCamera> layer_clipboard_cameras_;",
    "std::string layer_clipboard_source_title_id_;")
require(editor_tools,
    "referenced_camera_ids", "camera_id_remap",
    "for (DiscreteKeyframe &key : layer->camera_assignment.keyframes)",
    "copying_between_titles", "destination_camera_ids")

# Templates retain authored layers/cameras but never inherit a stale proxy,
# editor camera override, or active runtime cue/playlist state.
require(serialization,
    "t->proxy_metadata = TitleProxyMetadata{};",
    "t->render_camera_override_id.clear();",
    "exported_copy.proxy_metadata = TitleProxyMetadata{};",
    "exported_copy.render_camera_override_id.clear();")

# Persistence contributes to render/cache identity and honours per-layer ignore.
require(persistence,
    "title.cue_background_persistence", "title.cue_text_persistence",
    "title.cue_persistence_transition", "!layer.ignore_persistence")
require(cache_signature,
    "title->cue_background_persistence", "title->cue_text_persistence")

# Scene-mask source activation/release and editor audio transport remain wired.
require(scene_runtime,
    "scene_mask_foreground_active", "active_scene_mask_scenes",
    "release_active_scene_mask_scenes(data)")
require(audio_preview,
    "title_source_set_editor_transport(editor_audio_preview_source_, playhead_",
    "playback_reverse_", "release_editor_audio_preview()")

# Existing shutdown contract continues to guard timers, GPU objects, save jobs,
# and repeated editor/plugin teardown paths.
require(shutdown_contract,
    "prepare_for_shutdown", "begin_shutdown", "shutdownSaveWorker",
    "save_thread_.join()")

print("Development Version 211 compatibility/regression completion contract passed")
