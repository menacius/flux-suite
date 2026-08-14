from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
controls = read("Editor/fxm-modern-controls.cpp")
props = read("src/editor/properties-panel/popup-state.inc")
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
gpu_masks = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert re.search(r'OBS_FXM_DEVELOPMENT_VERSION \"(241|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})\"', cmake)
assert re.search(r'FXM_DEVELOPMENT_VERSION \"(241|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})\"', build)
assert re.search(r'kCurrentDevelopmentVersion = (241|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})', schema)
assert 'case 241:' in schema and 'case 240:' in schema
assert manifest['development_version'] >= 241
assert re.search(r'Development Version (241|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})', readme)
assert re.search(r'# v0.8.11-alpha — Development Version (241|24[0-9]|2[5-9][0-9]|[3-9][0-9]{2,})', changelog)

# Properties buttons keep their authored Revision 239 metrics instead of being
# collapsed by the shared Transform-panel normalizer.
for token in (
    'has_explicit_button_style',
    'has_fixed_button_height',
    'has_fixed_button_width',
    'fxmPreserveButtonMetrics',
):
    assert token in controls, token
assert 'padding:2px 8px' in props
assert 'fxm_transform_panel_button_style(pal)' not in props

# Parented image/video layers with Color Overlay must not reuse a stale direct
# image raster during transform-only parenting changes.
assert 'grouped_or_parented' in gpu_masks
assert 'LayerEffectType::ColorOverlay' in gpu_masks
assert 'return false;' in gpu_masks
assert '|gpu-direct-image=' in playback
assert '!entry.key.empty() && entry.key == key' in playback
assert playback.count('!entry.key.empty() && entry.key == key') >= 2

print('Development Version 243 properties buttons and parented Color Overlay contract: PASS')
