from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p): return (ROOT/p).read_text(encoding="utf-8")
def test_light_is_layer_type_and_serialized():
 s=read("Core/layer-model.h"); c=read("src/core/title-data.cpp")
 assert "Light = 13" in s and "TitleLight  light" in s
 assert 'j["light"] = light_to_json' in c and "legacy title-level lights become" in c
def test_renderer_consumes_light_layers():
 s=read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
 assert "light_layer->type != LayerType::Light" in s and "layer_world_matrix" in s
def test_text_shape_extrusion_and_bevel():
 m=read("Core/layer-model.h"); r=read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
 assert "geometry_extrusion_depth" in m and "geometry_bevel_depth" in m
 assert "extrusion_depth" in r and "bevel_scale" in r and "LayerType::Shape" in r
def test_editor_controls_and_version():
 p=read("src/editor/properties-panel/popup-state.inc"); b=read("src/core/build-info.h")
 assert "Extrusion / Bevel" in p and int(b.split('FXM_DEVELOPMENT_VERSION \"')[1].split('\"')[0]) >= 300
