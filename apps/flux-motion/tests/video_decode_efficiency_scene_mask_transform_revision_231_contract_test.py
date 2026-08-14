from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
schema = read('Core/title-serialization-schema.h')
video = read('Core/title-video-runtime.cpp')
gpu_masks = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
compat = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
gpu_session = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
canvas = read('src/canvas/canvas-preview/gpu-frame-rendering.inc')
canvas_header = read('src/canvas/canvas-preview.h')
editor = read('src/editor/title-editor/document-shape-editing.inc')
editor_header = read('src/editor/title-editor.h')
events = read('src/editor/title-editor/editor-events.inc') + read('src/editor/title-editor/signal-handlers.inc')
locale = read('data/locale/en-US.ini')
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')
manifest = read('tests/test-suite-manifest.json')

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r'kCurrentDevelopmentVersion\s*=\s*243', schema)
assert 'case 231:' in schema
assert '"development_version": 243' in manifest
assert 'Development Version 243' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

# Video decode efficiency: reuse decoded frames for frame stepping/FPS duplicate mapping.
assert 'decoded_frame_cache' in video
assert 'decoded_frame_lru' in video
assert 'kMaxCachedDecodedFrames' in video
assert 'cached_decoded_frame_locked' in video
assert 'remember_decoded_frame_locked' in video

# Video effects should avoid the slow compatibility base for non-expanding effects.
assert 'video_effects_can_use_direct_gpu_base_raster' in gpu_masks
for effect in ['BrightnessContrast', 'Saturation', 'ColorOverlay', 'Sharpen', 'ChromaKey', 'DisplacementMap']:
    assert f'LayerEffectType::{effect}' in gpu_masks
assert 'Expanding effects such as Blur/Glow/Drop Shadow/Light Wrap' in gpu_masks
assert 'return render_gpu_image_layer_base_raster_direct(title, layer,\n                                                         title_time, raster_scale);' in gpu_masks
assert 'return render_gpu_image_layer_base_raster_direct(title, layer,\n        return render_gpu_image_layer_base_raster_direct' not in gpu_masks

# Left/right should be claimed by the editor and perform frame stepping.
assert 'Qt::Key_Left' in events and 'Qt::Key_Right' in events
assert 'step_backward();' in events and 'step_forward();' in events

# Canvas context menu exposes Fit Screen and Fill Screen and routes them to the editor.
assert 'OBSTitles.FitScreen' in canvas
assert 'OBSTitles.FillScreen' in canvas
assert 'fit_fill_layers_requested' in canvas_header
assert 'fit_fill_selected_layers' in editor_header and 'TitleEditor::fit_fill_selected_layers' in editor
assert 'ImageBoxMode::FitToShortSide' in editor and 'ImageBoxMode::FitImageToBox' in editor
assert 'OBSTitles.FitScreen="Fit Screen"' in locale
assert 'OBSTitles.FillScreen="Fill Screen"' in locale

# Scene masks are rendered as real placeholder layers only through the editor placeholder flag, not source/runtime mattes.
assert 'render_scene_mask_placeholder' in compat
assert 'scene_mask_placeholder_color' in compat
assert 'label = "MASK"' in compat
assert 'layer.use_as_scene_mask' in compat
assert 'layer_should_render_as_visible_content_for_gpu_session' in gpu_session
assert 'session->scene_mask_placeholder_preview_enabled' in gpu_session
keyboard = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
assert 'scene_mask_hatch_brush()' not in keyboard
assert 'post-frame magenta hatch overlay' in keyboard
