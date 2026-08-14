from pathlib import Path

def read(path):
    return Path(path).read_text(encoding='utf-8')

core_h = read('Core/title-data.h')
core_cpp = read('src/core/title-data.cpp')
editor = read('src/editor/title-editor/layout-template-tools.inc')
layout = read('src/editor/title-editor/panels-colors.inc')
gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
readback = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')

assert 'serialize_layer_clipboard_json' in core_h
assert 'flux-motion-layer-clipboard' in core_cpp
assert 'application/x-fxm-layer-clipboard+json' in editor
assert 'QApplication::clipboard()->setMimeData' in editor
assert 'deserialize_layer_clipboard_json' in editor
assert 'float wetAlpha = min(exposureAlpha, coverageAlpha);' in gpu
assert 'resolvedOccupancy' not in gpu[gpu.index('static constexpr const char *kGpuTemporalCompositeEffect'):gpu.index('static constexpr const char *kGpuTemporalCompositeEffect') + 5000]
assert 'sample_cap = std::min(sample_cap, 24);' in readback
assert 'tabifyDockWidget(styles_dock_, effects_presets_dock_)' in layout
assert 'splitDockWidget(graphic_props_dock_, layer_props_dock_, Qt::Vertical)' in layout
import re
layout_version = re.search(r'kEditorLayoutVersion = (\d+);', read('src/editor/title-editor-internal/widget-property-helpers.inc'))
assert layout_version and int(layout_version.group(1)) >= 6
