from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 392
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 392
assert manifest["development_version"] >= 392
assert (
    "tests/development_version_392_planar_shadow_clip_motion_transition_crash_contract_test.py"
    in json.dumps(manifest)
)

# Item 1 remains exactly as accepted: 4096 maximum and 2048 default.
prefs = read("src/core/title-preferences.cpp")
advanced = read("src/editor/title-editor/signal-handlers.inc")
assert "kAllowedSizes {{256, 512, 1024, 2048, 4096}}" in prefs
assert "8192" not in prefs[prefs.index("shadow_map_size_px"):prefs.index("cache_disk_location")]
assert "for (int size : {256, 512, 1024, 2048, 4096})" in advanced
assert "shadow_map_size->setCurrentIndex(shadow_map_index >= 0 ? shadow_map_index : 3);" in advanced

# Spot/Parallel either retain the accepted D3D clip remap or use the newer
# explicit light-space projection shared by writer and receiver.
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
shadow_start = shader.index("static constexpr const char *kGpuShadowMapEffect")
shadow_end = shader.index("static constexpr const char *kGpuAdjustmentMixEffect", shadow_start)
shadow = shader[shadow_start:shadow_end]
legacy_clip = (
    "QMatrix4x4 perspective/orthographic projections produce OpenGL-style" in shadow
    and "o.lightClip.z * 0.5 + o.lightClip.w * 0.5" in shadow
    and "v.lightClip.z / max(v.lightClip.w, 0.000001) * 0.5 + 0.5" in shadow
)
explicit_projection = all(token in shadow for token in (
    "uniform float3 shadowRight;",
    "uniform float3 shadowUp;",
    "uniform float3 shadowForward;",
    "float depth = dot(relative, shadowForward);",
    "float normalizedDepth = (depth - shadowNear)",
))
assert legacy_clip or explicit_projection
assert "o.pos = o.lightClip;" not in shadow
assert "if (pointShadow != 0)" in shadow

presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
assert "v41-d3d-planar-shadow-clip" in presentation

# Motion Blur + active/dynamic transitions must degrade safely, not crash/drop.
assert "stage=temporal-sample-exception" in presentation
assert "fallback=current-transition-frame" in presentation
assert "stage=temporal-fallback" in presentation
assert "Layer temporal_layer = layer_without_motion_blur_effects(layer);" in presentation
assert "release_temporal_resources = [&]() noexcept" in presentation
assert "temporal_resource_guard" in presentation
assert "return render_gpu_layer_to_target(\n                    session, title, temporal_layer, title_time, target" in presentation
assert "catch (const std::exception &error)" in presentation
assert "catch (...)" in presentation

# The whole compositor is also a no-throw boundary for OBS and Qt paint callbacks.
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
assert "const bool destination_composite = external_background != nullptr;\n    try {" in session
assert "stage=session-render-exception" in playback
assert "session->motion_temporal_depth = 0;" in playback
assert "session->last_draw_deferred = true;" in playback
assert "GPU compositor exception:" in playback
assert "gpu_session_has_published_frame_for_current_title(session)" in playback

print("Development Version 392 planar shadow clip and Motion Blur transition crash contract: PASS")
