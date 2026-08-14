from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_schema_manifest_and_cache_identity_are_298():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert 'kCurrentDevelopmentVersion = 265' in read(
        'Core/title-serialization-schema.h')
    manifest = json.loads(read('tests/test-suite-manifest.json'))
    assert manifest['development_version'] == 299
    assert '|gpu-text-pipeline=299' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    assert '|gpu-effects-v26-3d-lighting-materials-shadows|' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'v48-3d-lighting-materials' in read(
        'src/cache/cache-manager/visual-hash-keying.inc')


def test_title_light_model_supports_ae_style_light_families_and_animation():
    model = read('Core/title-data.h')
    for token in ('Ambient', 'Point', 'Spot', 'Parallel', 'Environment'):
        assert token in model
    for token in (
        'AnimatedVec3Property position', 'AnimatedVec3Property target',
        'AnimatedProperty intensity', 'AnimatedProperty cone_angle',
        'AnimatedProperty cone_feather', 'AnimatedProperty falloff_start',
        'AnimatedProperty falloff_distance', 'AnimatedProperty shadow_darkness',
        'AnimatedProperty shadow_softness', 'AnimatedProperty shadow_bias',
        'AnimatedProperty environment_rotation',
        'title_light_has_authored_keyframes'):
        assert token in model
    assert 'std::vector<TitleLight> lights;' in model
    assert 'bool default_light_enabled = true;' in model
    assert 'bool lighting_enabled = true;' in model


def test_material_model_is_opt_in_and_backward_compatible():
    model = read('Core/layer-model.h')
    assert 'bool material_accepts_lights = false;' in model
    assert 'bool material_casts_shadows = true;' in model
    assert 'bool material_accepts_shadows = true;' in model
    assert 'bool material_appears_in_reflections = true;' in model
    for token in (
        'material_ambient', 'material_diffuse', 'material_specular',
        'material_shininess', 'material_metallic', 'material_roughness',
        'material_reflection_intensity', 'material_emissive_color',
        'material_emissive_intensity'):
        assert token in model
    serialization = read('src/core/title-data.cpp')
    assert 'json_bool(j, "material_accepts_lights", false)' in serialization
    assert 'json_bool(jt, "default_light_enabled", true)' in serialization
    assert 'json_bool(jt, "lighting_enabled", true)' in serialization


def test_lights_and_materials_round_trip_through_canonical_serialization():
    source = read('src/core/title-data.cpp')
    assert 'static json light_to_json(const TitleLight &light)' in source
    assert 'static TitleLight light_from_json(const json &j, size_t index)' in source
    for key in (
        '"lights"', '"default_light_enabled"', '"lighting_enabled"',
        '"environment_exposure"', '"casts_shadows"', '"shadow_darkness"',
        '"shadow_softness"', '"shadow_bias"', '"environment_path"'):
        assert key in source
    assert 'std::unordered_set<std::string> light_ids;' in source
    assert 'merge_surviving_passthrough(source_passthrough, result)' in source


def test_gpu_shader_performs_per_pixel_planar_pbr_lighting():
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    for token in (
        'uniform int lightingEnabled;', 'uniform float3 planeOrigin;',
        'uniform float3 planeAxisU;', 'uniform float3 planeAxisV;',
        'uniform float3 worldNormal;', 'uniform float3 cameraWorldPosition;',
        'uniform float4 materialPrimary;', 'uniform float4 materialPbr;',
        'float light_distance_attenuation(', 'void accumulate_light(',
        'float4 apply_material_lighting(', 'baseColor, metallic',
        'environmentContribution', 'materialEmissive.rgb * emissiveAmount'):
        assert token in shader
    assert shader.count('uniform float4 lightPositionType') == 4
    assert shader.count('accumulate_light(lightPositionType') == 4
    assert 'if (lightingEnabled == 0 || premultipliedColor.a <= 0.000001)' in shader
    assert 'return apply_material_lighting(color, v.uv);' in shader


def test_renderer_binds_world_geometry_camera_material_and_four_light_slots():
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    for token in (
        'set_gpu_layer_lighting_params(', 'layer_world_matrix(',
        'set_vec3(plane_origin, p00);', 'set_vec3(plane_axis_u, p10 - p00);',
        'set_vec3(plane_axis_v, p01 - p00);',
        'set_vec3(world_normal, evaluated.world_normal);',
        'set_vec3(camera_world_position, evaluated.camera_position);',
        'std::array<EvaluatedLightSlot, 4> light_slots',
        'TitleLightType::Parallel',
        'TitleLightFalloff::None', 'title.default_light_enabled'):
        assert token in renderer
    assert 'layer.material_accepts_lights' in renderer
    assert 'layer.material_accepts_shadows' in renderer
    assert 'layer.material_appears_in_reflections' in renderer


def test_shadow_pass_is_depth_tested_alpha_aware_and_fail_open():
    shader = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
    resources = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    lifecycle = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    for token in (
        'kGpuShadowMapEffect', 'clip(alpha - alphaCutoff);',
        'float shadow_visibility(float3 worldPosition)',
        'for (int y = -1; y <= 1; ++y)',
        'for (int x = -1; x <= 1; ++x)'):
        assert token in shader
    for token in (
        'static bool render_gpu_shadow_map(', 'TitleLightType::Spot',
        'TitleLightType::Parallel', 'GS_CLEAR_DEPTH', 'GS_Z24_S8',
        'gs_enable_depth_test(true);', 'gs_depth_function(GS_LEQUAL);',
        'session->shadow_map_valid = drew;'):
        assert token in renderer or token in resources
    assert 'render_gpu_shadow_map(session, session->title, session->time)' in renderer
    assert 'destroy_target(session->shadow_target);' in lifecycle
    assert 'gs_effect_destroy(session->shadow_effect);' in lifecycle


def test_editor_exposes_light_material_and_timeline_authoring_controls():
    title_ui = read('src/editor/title-properties-panel.cpp')
    material_ui = read('src/editor/properties-panel/popup-state.inc')
    hierarchy = read('src/editor/title-editor-internal/hierarchy-model.inc')
    layer_stack = read('src/layers/layer-stack-widget.cpp')
    for label in (
        '3D Lights & Environment', 'Ambient', 'Point', 'Spot', 'Parallel',
        'Environment', 'Cast Shadows', 'Shadow Darkness', 'Shadow Softness',
        'Shadow Bias'):
        assert label in title_ui
    for label in (
        'Material Options', 'Accepts Lights', 'Casts Shadows',
        'Accepts Shadows', 'In Reflections', 'Metallic', 'Roughness',
        'Emission'):
        assert label in material_ui
    assert 'timeline_light_properties' in hierarchy
    assert 'light_timeline_owner_id' in hierarchy
    assert 'authorable_material_property' in hierarchy
    assert 'QStringLiteral("LGT")' in layer_stack
    assert 'title_->lights.size() >= 4' in title_ui
    assert 'title_->default_light_enabled = true;' in title_ui


def test_animation_and_cache_dependencies_include_all_new_pixel_inputs():
    runtime = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
    asset_runtime = read('Core/asset-runtime.cpp')
    disk_cache = read('src/cache/cache-manager/disk-cache-storage.inc')
    visual_hash = read('src/cache/cache-manager/cache-policy-invalidation.inc')
    for token in (
        'material_ambient.is_animated()', 'material_diffuse.is_animated()',
        'material_specular.is_animated()', 'material_metallic.is_animated()',
        'material_roughness.is_animated()',
        'material_emissive_intensity.is_animated()'):
        assert token in runtime
        assert token in asset_runtime
    assert 'title_light_has_authored_keyframes(light)' in runtime
    assert 'title_light_has_authored_keyframes(light)' in asset_runtime
    for token in ('title.lights', 'light.position', 'light.target',
                  'light.shadow_darkness', 'layer->material_roughness'):
        assert token in disk_cache
    assert 'title.lights' in visual_hash
    assert 'layer->material_emissive_intensity' in visual_hash
    assert disk_cache.count('add_anim(layer->material_ambient)') >= 2


def test_documentation_states_scope_and_known_renderer_limits():
    changelog = read('docs/CHANGELOG.md')
    readme = read('README.md')
    guide = read('docs/RENDERING_AND_CACHE.md')
    assert changelog.startswith('# v0.8.12-alpha — Development Version 299')
    assert '3D lighting, materials and planar shadows' in changelog
    assert 'Development Version 298' in readme
    assert 'Point-light cube shadows' in guide
    assert 'HDRI texture sampling' in guide
