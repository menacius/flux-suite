from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = ROOT.parents[1]
COMMON = REPOSITORY_ROOT / "packages/flux-common"
OBS = REPOSITORY_ROOT / "plugins/obs/flux-motion"

shared_source = COMMON / "Shared/rendering-engine/title-source.cpp"
shared_modules = COMMON / "Shared/rendering-engine/title-source"
policy = (COMMON / "Shared/motion-blur-sampling.cpp").read_text(encoding="utf-8")
cmake = (OBS / "cmake/FluxMotionObsPlugin.cmake").read_text(encoding="utf-8")
gpu = (shared_modules / "gpu-presentation-readback.inc").read_text(encoding="utf-8")
compat = (shared_modules / "gpu-resources-primitives.inc").read_text(encoding="utf-8")

assert shared_source.is_file()
assert shared_modules.is_dir()
assert not (OBS / "src/title-source.cpp").exists()
assert not (OBS / "src/title-source").exists()
assert (OBS / "src/obs-render-backend.cpp").is_file()
assert "Shared/rendering-engine/title-source.cpp" in cmake
assert "motionBlurQualitySampleCount" in policy
assert "renderSharpFrameOnly" in policy
assert "(static_cast<double>(index) + 0.5)" in policy
assert "imageLike ? 1.5 : 0.8" in policy
assert "use_transform_motion_fast_path" in gpu
assert "requires_full_temporal_pipeline" in gpu
assert "motion_blur_quality_sample_count" in compat
assert "makeMotionBlurSamplingPlan" in compat

print("v0.8.18 shared renderer and motion blur contract: PASS")
