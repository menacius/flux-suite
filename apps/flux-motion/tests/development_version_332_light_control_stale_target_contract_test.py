#!/usr/bin/env python3
"""Dev332: Light controls cannot publish reusable layer-target pixels."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
present = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 332
assert int(build.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 332
assert manifest["development_version"] >= 332

# Light layers remain in the title model for illumination, but never enter the
# visible-artwork compositor where a no-op can expose stale target storage.
visibility_start = runtime.index(
    "static bool layer_should_render_as_visible_content("
)
visibility_end = runtime.index("\n}\n", visibility_start)
visibility = runtime[visibility_start:visibility_end]
assert "if (layer.type == LayerType::Light)" in visibility
assert "return false;" in visibility

render_start = present.index("static bool render_gpu_layer_to_target(")
render_end = present.index("\n}", render_start)
render_prefix = present[render_start:render_end]
assert "if (layer.type == LayerType::Light) return false;" in render_prefix
assert "if (layer.type == LayerType::Light) return true;" not in present

# Root and nested-group compositors both apply the central content predicate.
assert session.count(
    "layer_should_render_as_visible_content_for_gpu_session("
) >= 6

# The control still participates in actual lighting and shadow evaluation.
assert present.count("light_layer->type != LayerType::Light") >= 1
assert session.count("light_layer->type != LayerType::Light") >= 1
assert "v52-light-control-no-artwork" in cache_abi

print("Dev332 Light control stale-target contract passed")
