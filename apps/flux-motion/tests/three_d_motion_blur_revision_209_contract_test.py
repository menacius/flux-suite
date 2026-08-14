from pathlib import Path

root = Path(__file__).resolve().parents[1]
compat = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc").read_text()
gpu = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc").read_text()
readme = (root / "README.md").read_text()
changelog = (root / "docs/CHANGELOG.md").read_text()

assert "camera-aware motion blur" in readme
assert "Camera-Aware 3D Motion Blur" in changelog
assert "std::array<double, 5> times" in compat
assert "std::array<std::array<QPointF, 9>, 5> world_points" in compat
assert "layer_or_ancestor_uses_3d(title, layer)" in gpu
assert "travel < 0.01" in gpu
print("Development Version 209 camera-aware 3D motion-blur contract passed")
