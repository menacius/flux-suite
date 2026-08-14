from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
shader_source = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 376
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 376
assert manifest["development_version"] >= 376
contract = "tests/development_version_376_msvc_shader_literal_split_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# MSVC C2026 truncates a single narrow string literal around 16 KiB. Keep a
# safety margin for every embedded OBS effect, while adjacent raw literals are
# concatenated by C++ into the exact same shader source at compile time.
literals = re.findall(r'R"\((.*?)\)"', shader_source, re.DOTALL)
assert literals
assert max(len(literal.encode("utf-8")) for literal in literals) < 16000

copy_start = shader_source.index(
    "static constexpr const char *kGpuLayerCopyEffect")
copy_end = shader_source.index(
    "static constexpr const char *kGpuShadowMapEffect")
copy_source = shader_source[copy_start:copy_end]
copy_literals = re.findall(r'R"\((.*?)\)"', copy_source, re.DOTALL)
assert len(copy_literals) >= 3
reconstructed = "".join(copy_literals)
for token in (
    "float4 sample_scaled_image(float2 uv)",
    "float light_distance_attenuation(",
    "void accumulate_light(",
    "float point_shadow_visibility(",
    "technique Draw",
):
    assert token in reconstructed

print("Development Version 376 MSVC shader literal split contract: PASS")
