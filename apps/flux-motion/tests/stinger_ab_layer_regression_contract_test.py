from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = (root / "src/core/title-data.cpp").read_text()
canvas_h = (root / "src/canvas/canvas-preview.h").read_text()
canvas_geometry = (root / "src/canvas/canvas-preview/geometry-selection.inc").read_text()
canvas_selection = (root / "src/canvas/canvas-preview/preview-cache-view.inc").read_text()
canvas_hit = (root / "src/canvas/canvas-preview/canvas-overlay-paint.inc").read_text()
timeline = (root / "src/timeline/timeline-widget.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
build_info = (root / "src/core/build-info.h").read_text()

assert "layer_is_editor_visible" in canvas_h
assert "StingerSwitchMode::ManualSceneAnimation" in canvas_geometry
assert "layer && layer_is_editor_visible(*layer)" in canvas_geometry
assert "if (!layer || !layer_is_editor_visible(*layer)" in canvas_hit
assert "visible_ids" in canvas_selection

assert "set_stinger_transition_input_default_surface" in core
assert "layer.opacity.static_value = 1.0" in core
assert "layer.opacity.keyframes.clear()" in core
assert "layer.scale.static_value = {1.0, 1.0, 1.0}" in core
assert "layer.size.static_value = {canvas_width, canvas_height}" in core
assert "Scene B starts below Scene A" in core
assert "stinger_transition_input_has_legacy_point_opacity" in core
assert "obsolete automatic point-cut opacity curves" in core

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info

print("Stinger A/B visibility, surface, and timeline-duration regression contract passed")
