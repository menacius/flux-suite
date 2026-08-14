from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
cmake_dev = int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_dev = int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_dev >= 391
assert build_dev >= 391
assert manifest["development_version"] >= 391
assert "tests/development_version_391_planar_shadowmap_contract_test.py" in json.dumps(manifest)

prefs_cpp = read("src/core/title-preferences.cpp")
advanced = read("src/editor/title-editor/signal-handlers.inc")
shadow_render = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")

# 8192 was intentionally removed after runtime testing; 4096 is again the max UI/runtime bound.
assert "kAllowedSizes {{256, 512, 1024, 2048, 4096}}" in prefs_cpp
assert "kAllowedSizes {{256, 512, 1024, 2048, 4096, 8192}}" not in prefs_cpp
assert "for (int size : {256, 512, 1024, 2048, 4096})" in advanced
assert "for (int size : {256, 512, 1024, 2048, 4096, 8192})" not in advanced
assert shadow_render.count("std::clamp(TitlePreferences::shadow_map_size_px(), 256, 4096)") >= 2
assert "std::clamp(TitlePreferences::shadow_map_size_px(), 256, 8192)" not in shadow_render

# Spot and Parallel lights use planar 2D maps and must explicitly reset the full shadow-target viewport.
planar_start = shadow_render.index("} else {\n        /* gs_texrender_begin()")
planar_end = shadow_render.index("gs_matrix_pop();", planar_start)
planar_branch = shadow_render[planar_start:planar_end]
assert "Spot and Parallel maps need the same explicit full-map" in planar_branch
assert "gs_set_viewport(0, 0," in planar_branch
assert "session->shadow_map_width[shadow_slot]" in planar_branch
assert "session->shadow_map_height[shadow_slot]" in planar_branch
assert "draw_casters();" in planar_branch

# The lighting shader still resolves both planar shadow kinds, not only Point cube-atlas maps.
assert "if (shadowKind == 1 || shadowKind == 3)" in shader
assert "planar_shadow_visibility(" in shader
assert "shadow_light.type == TitleLightType::Parallel ? 3 : 1" in shadow_render
assert "TitleLightType::Spot" in shadow_render
assert "TitleLightType::Parallel" in shadow_render

print("Development Version 391 planar shadowmap and shadowmap-size contract: PASS")
