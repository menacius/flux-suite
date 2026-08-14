from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

animation_h = read("Core/animation.h")
animation_cpp = read("Core/animation.cpp")
serialization = read("src/core/title-data.cpp")
schema = read("Core/title-serialization-schema.h")
layer_model = read("Core/layer-model.h")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
timeline_h = read("src/timeline/timeline-widget.h")
timeline_cpp = read("src/timeline/timeline-widget.cpp")
graph = read("src/timeline/temporal-graph-editor.inc")
layer_stack_h = read("src/layers/layer-stack-widget.h")
layer_stack_cpp = read("src/layers/layer-stack-widget.cpp")
commands = read("src/editor/title-editor/commands-docks.inc")
cache = read("src/cache/cache-manager/disk-cache-storage.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

# Version identity and migration ledger.
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in read("CMakeLists.txt")
assert 'FXM_DEVELOPMENT_VERSION "243"' in read("src/core/build-info.h")
assert 'kCurrentDevelopmentVersion = 243' in schema
assert 'case 212:' in schema

# Legacy vector names remain, but every payload carries Z and old XY is promoted.
assert 'struct Vec2Value' in animation_h and 'double z = 0.0;' in animation_h
assert 'using AnimatedVectorProperty = AnimatedVec2Property;' in animation_h
for token in (
    'a.z + b.z', 'a.z - b.z', 'value.z * scalar',
    'a.z + (b.z - a.z) * progress', 'p0.z',
    '(vb.z - va.z) / (b - a)'):
    assert token in animation_cpp, token
for token in (
    '{"z", p.static_value.z}', '{"z", k.value.z}',
    'vector_payload_has_z', 'promote_legacy_scalar_z_track',
    'promote_legacy_scalar_z_track(l->scale, l->scale_z)',
    'promote_legacy_scalar_z_track(l->origin_prop, l->anchor_z)'):
    assert token in serialization, token
assert 'return {xyz.x, xyz.y, xyz.z};' in layer_model
assert 'add(v.x); add(v.y); add(v.z);' in cache
assert 'add(quant(v.x, step)); add(quant(v.y, step)); add(quant(v.z, step));' in cache

# One aggregate transform row, with union-of-axis key times for old projects.
for token in (
    '"position", &layer.position_3d_path_enabled',
    '"rotation", {&layer.rotation_x, &layer.rotation_y, &layer.rotation}',
    '"orientation", {&layer.orientation_x, &layer.orientation_y',
    '"camera_position"', '"camera_target"', '"camera_rotation"',
    'scalar_group_keyframe_times()', 'scalar_group_keyframe_location'):
    assert token in hierarchy, token
assert '"position_x", &layer.position_3d_path_enabled' not in hierarchy
assert '"position_y", &layer.position_3d_path_enabled' not in hierarchy

# Camera visibility and Add Layer camera creation/activation.
assert 'void add_camera_requested();' in layer_stack_h
assert 'void on_add_camera();' in layer_stack_h
assert 'add_camera_requested' in layer_stack_cpp
assert 'connect(layers_, &LayerStack::add_camera_requested' in commands
assert 'title_->active_camera_id = camera_id;' in commands
assert 'if (default_camera && !title_camera_has_authored_keyframes(camera))' in hierarchy
assert 'const bool show_camera_switches = has_custom_camera ||' in hierarchy
assert 'default_camera_animated ||' in hierarchy

# Both views share structure immediately and timeline/keyframe selection drives Graph Editor.
assert 'void keyframe_structure_changed();' in timeline_h
assert timeline_cpp.count('emit keyframe_structure_changed();') >= 2
assert 'connect(timeline_, &TimelineWidget::keyframe_structure_changed' in commands
assert 'layers_->refresh();' in commands
assert 'set_graph_target(layer_id, prop_name);' in timeline_cpp
assert 'select_graph_property(rows[row].owner_id, rows[row].prop.name(),' in timeline_cpp
assert 'graph_target_owner_id_' in timeline_h and 'graph_target_property_name_' in timeline_h
assert ('find_timeline_property(graph_target_owner_id_' in graph or
        'timeline_property_for_owner(' in graph)

# XY-only UI operations must retain authored Z rather than flattening 3D state.
for token in (
    'Vec2Value next = prop.vector->evaluate(lt);',
    'current.z',
    'next_scale_z',
):
    assert token in (hierarchy + layer_stack_cpp + commands + read("src/canvas/canvas-preview.cpp") +
                     read("src/canvas/canvas-preview/editor-3d-tools.inc") +
                     read("src/canvas/canvas-preview/transform-snap.inc") +
                     read("src/canvas/canvas-preview/geometry-selection.inc") +
                     read("src/editor/properties-panel/auto-style-and-property-actions.inc")), token

# The same animated-only predicate drives property-row visibility in both views.
assert 'if (!prop.is_animated() && !authorable_camera_assignment) continue;' in hierarchy
assert 'const auto shared_timeline_rows = timeline_rows(title_);' in layer_stack_cpp

assert 'Layer List, Timeline and Graph Editor share one expanded Vector3 row model' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

print('Development Version 212 unified Vector3/camera/timeline contract passed')
