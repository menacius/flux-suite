from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 393
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 393
assert manifest["development_version"] >= 393
assert (
    "tests/development_version_393_motion_transition_render_deadlock_contract_test.py"
    in json.dumps(manifest)
)

registry = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
start = registry.index("static gs_effect_t *session_registry_effect_or_queue(\n    TitleGpuRenderSession *session, LayerEffectType type")
end = registry.index("/* Phase 14 RAM tier", start)
helpers = registry[start:end]

# render_gpu_session_locked() is entered with session->mutex already held. The
# synchronous lookup helpers must never acquire that same non-recursive mutex.
assert "Dev388 async-compile conversion added a second non-recursive lock" in helpers
assert "registry has its\n     * own recursive mutex" in helpers
assert "std::lock_guard<std::mutex> lock(session->mutex);" not in helpers.split(
    "queue_shader_compile_job", 1
)[0]
assert "session->effect_registry->find(type)" in helpers
assert "session->effect_registry->find(stable_id)" in helpers

# Worker-side mutation still synchronizes through session->mutex, while the
# registry's own recursive mutex serializes find()/compile().
assert "[session, type, human_label]() -> bool" in helpers
assert "[session, stable_id, human_label]() -> bool" in helpers
assert helpers.count("std::lock_guard<std::mutex> lock(session->mutex);") >= 4

presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
assert "stage=temporal-sample-render-exception" in presentation
assert "fallback=current-transition-frame" in presentation
assert "temporal_sample_exception_message = error.what();" in presentation
assert "return render_gpu_layer_to_target(\n                        session, title, temporal_layer, sample_time" in presentation
assert "stage=temporal-fallback" in presentation

# Host-level no-throw containment remains present as the final boundary.
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
assert "stage=session-render-exception" in playback
assert "GPU compositor exception:" in playback

print("Development Version 393 Motion Blur transition render deadlock contract: PASS")
