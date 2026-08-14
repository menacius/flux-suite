from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text(encoding="utf-8")

def test_light_layers_are_created_from_add_layer_menu():
    h = read("src/layers/layer-stack-widget.h")
    cpp = read("src/layers/layer-stack-widget.cpp")
    cmd = read("src/editor/title-editor/commands-docks.inc")
    assert "add_light_requested(TitleLightType type)" in h
    for name in ["Ambient Light", "Point Light", "Spot Light", "Parallel Light", "Environment Light"]:
        assert name in cpp
    assert "layer->type = LayerType::Light" in cmd
    assert "title_->add_layer(layer)" in cmd

def test_light_helpers_are_visible_in_canvas():
    header = read("src/canvas/canvas-preview.h")
    tools = read("src/canvas/canvas-preview/editor-3d-tools.inc")
    paint = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
    assert "draw_light_layer_overlays" in header
    assert "TitleLightType::Spot" in tools
    assert "cone_angle.evaluate" in tools
    assert "falloff_distance.evaluate" in tools
    assert "draw_light_layer_overlays(p)" in paint

def test_version_302():
    assert 'FXM_DEVELOPMENT_VERSION "302"' in read("src/core/build-info.h")
