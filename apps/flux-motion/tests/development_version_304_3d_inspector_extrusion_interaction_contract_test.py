from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
read = lambda p: (ROOT / p).read_text(encoding="utf-8")


def test_development_version_304_contract():
    assert '#define FXM_DEVELOPMENT_VERSION "304"' in read('src/core/build-info.h')
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "304")' in read('CMakeLists.txt')


def test_3d_scene_uses_dedicated_dock():
    editor_h = read('src/editor/title-editor.h')
    docks = read('src/editor/title-editor/commands-docks.inc')
    title_props = read('src/editor/title-properties-panel.cpp')
    assert 'three_d_scene_dock_' in editor_h
    assert 'kThreeDSceneDockObjectName' in docks
    assert 'scene_controls_widget()' in docks
    assert 'scene_controls_layout->addWidget(camera_panel_)' in title_props
    assert 'scene_controls_layout->addWidget(light_panel_)' in title_props


def test_material_geometry_standard_inspector_and_animation():
    popup = read('src/editor/properties-panel/popup-state.inc')
    sync = read('src/editor/properties-panel/property-synchronization.inc')
    hierarchy = read('src/editor/title-editor-internal/hierarchy-model.inc')
    defaults = read('src/editor/properties-panel/panel-defaults.inc')
    assert 'fxm_add_panel_section(vl, material_options_box_' in popup
    assert 'fxm_add_panel_section(vl, geometry_options_box_' in popup
    assert 'NumericDragLabel' in popup
    assert 'btn_kf_geometry_extrusion_depth_' in sync
    assert 'btn_kf_geometry_bevel_depth_' in sync
    assert 'material_emissive_color' in hierarchy
    assert 'geometry_extrusion_depth' in hierarchy
    assert 'key.contains(QStringLiteral("material"))' in defaults
    assert 'key.contains(QStringLiteral("geometry"))' in defaults


def test_emissive_color_is_appearance_swatch_and_animated():
    popup = read('src/editor/properties-panel/popup-state.inc')
    model = read('Core/layer-model.h')
    data = read('src/core/title-data.cpp')
    editor = read('src/editor/properties-panel/construction-transform-character.inc')
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    assert 'add_appearance_row' in popup and 'Emissive Color' in popup
    for channel in 'argb':
        assert f'material_emissive_color_{channel}' in model
        assert f'material_emissive_color_{channel}' in data
    assert 'fxm_pick_color' in editor
    assert 'evaluated_material_emissive_color' in editor
    assert 'materialEmissive.a' in shader


def test_light_layers_have_object_overlay_without_artwork_box():
    paint = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
    geometry = read('src/canvas/canvas-preview/transform-snap.inc')
    helpers = read('src/canvas/canvas-preview/editor-3d-tools.inc')
    selection = read('src/canvas/canvas-preview/canvas-overlay-paint.inc')
    pointer = read('src/canvas/canvas-preview/pointer-events.inc')
    assert 'layer.type == LayerType::Audio || layer.type == LayerType::Light' in paint
    assert 'layer.type == LayerType::Light' in geometry
    assert 'draw_light_layer_overlays' in helpers
    assert 'light_layer_overlay_contains' in helpers
    assert 'light_layer_overlay_contains(*layer, view_pt)' in selection
    assert 'layer->type == LayerType::Light' in pointer


def test_xyz_fast_path_and_extrusion_pipeline_are_repaired():
    playback = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    canvas = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
    panel = read('src/editor/properties-panel/construction-transform-character.inc')
    render = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    data = read('src/core/title-data.cpp')
    assert 'snapshot_layer.position_3d = source_layer.position_3d' in playback
    assert 'snapshot_layer.position_3d_path_enabled' in playback
    assert 'selected_transform_requires_full_refresh' not in canvas
    assert 'const bool transform_only_update = transform_only_requested' in canvas
    assert 'layer_->dimension_mode = LayerDimensionMode::ThreeD' in panel
    assert 'adaptive_body_segments' in render
    assert 'local_z_offset' in render
    assert 'Development 300-303 could persist extrusion' in data
