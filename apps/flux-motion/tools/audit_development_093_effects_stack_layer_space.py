#!/usr/bin/env python3
"""Structural regression audit for Development Version 093."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")

gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
compat = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
raster = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
visual_hash = read("src/cache/cache-manager/visual-hash-keying.inc")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
readme = read("README.md")
doc = read("docs/CHANGELOG.md")

shader_paths = [
    "data/effect-transitions/shaders/background-color/background-color.effect",
    "data/effect-transitions/shaders/4-color-gradient/4-color-gradient.effect",
    "data/effect-transitions/shaders/lens-flare/lens-flare.effect",
    "data/effect-transitions/shaders/vignette/vignette.effect",
    "data/effect-transitions/shaders/noise/noise.effect",
    "data/effect-transitions/shaders/roughen-edges/roughen-edges.effect",
]
shaders = {path: read(path) for path in shader_paths}

checks = []
def check(name, condition):
    checks.append((name, bool(condition)))

check("ordinary stack no longer has a full-canvas reroute",
      "layer_requires_full_canvas_effect_pass" not in gpu and
      "transform-neutral, padded layer" in gpu)
check("first effect changes the retained base-raster contract",
      "|gpu-effect-surface=" in lifecycle and
      "layer_requires_preserved_effect_surface" in compat)
check("cropped raster metadata follows the retained image",
      "result.layer_box_rect.translate(-bounds.x(), -bounds.y());" in raster and
      "result.image_clip_rect.translate(-bounds.x(), -bounds.y());" in raster)
check("affine layer basis is sent to shaders",
      all(token in gpu for token in (
          "struct GpuEffectLayerSpace", "layerUvOrigin", "layerUvAxisX",
          "layerUvAxisY", "layerPixelSize", "layer_world_transform_qt")) and
      all(token in compat for token in (
          '"layerUvOrigin"', '"layerUvAxisX"', '"layerUvAxisY"',
          '"layerPixelSize"')))
check("background geometry is layer-relative",
      "Geometry is expressed relative to the layer box" in gpu and
      "const double x = -resolved.effect_padding_left * scale_x;" in gpu and
      "layer_space_uv(v_in.uv) * max(textureSize" in shaders[shader_paths[0]])
check("spatial effects consume layer coordinates",
      all("float2 layer_space_uv(float2 surface_uv)" in source
          for source in shaders.values()) and
      "point_weight(effect_uv, point1" in shaders[shader_paths[1]] and
      "float2 flare_pos = effect_uv - center;" in shaders[shader_paths[2]] and
      "float2 p = (effect_uv - center) * 2.0;" in shaders[shader_paths[3]] and
      "float2 p = effect_uv * max(scale" in shaders[shader_paths[4]] and
      "float2 p = effect_uv * max(scale" in shaders[shader_paths[5]])
check("expanding procedural effects reserve local support",
      "case LayerEffectType::LensFlare" in compat and
      "case LayerEffectType::RoughenEdges" in compat and
      "effect.affect_layers_behind" in compat)
check("old effect and prerender outputs are invalidated",
      "v10-layer-space-stack" in gpu and
      "v35-layer-space-effects-stack" in visual_hash)
cmake_version = re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION "([0-9]{3})"\)', cmake)
build_version = re.search(r'#define FXM_DEVELOPMENT_VERSION "([0-9]{3})"', build_info)
check("development version is synchronized as 093 or later",
      cmake_version is not None and build_version is not None and
      cmake_version.group(1) == build_version.group(1) and
      int(cmake_version.group(1)) >= 93)
check("version 093 change is documented",
      cmake_version is not None and
      f"Development Version {cmake_version.group(1)}" in readme and
      "Development Version 093" in doc and
      "Layer-Space Effects Stack" in doc and
      "cache identities" in doc)
check("standalone regression contract is registered",
      "effects_stack_layer_space_contract_test" in cmake and
      (ROOT / "tests/effects_stack_layer_space_contract_test.cpp").exists())

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(("PASS" if ok else "FAIL"), name)
if failed:
    sys.exit(1)
print(f"PASS Development 093 effects stack audit ({len(checks)}/{len(checks)})")
