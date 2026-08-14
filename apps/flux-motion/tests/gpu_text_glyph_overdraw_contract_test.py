from pathlib import Path

root = Path(__file__).resolve().parents[1]
renderer = (root / "src/rendering/title-gpu-text-renderer.cpp").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build = (root / "src/core/build-info.h").read_text(encoding="utf-8")

assert "static void crop_quad_padding" in renderer
assert "required_padding = std::clamp" in renderer
assert "extents.outside + 2.5f" in renderer
assert "cluster_animation->blur" in renderer
assert "cluster_animation->stroke_width_delta" in renderer
assert "OBS_FXM_DEVELOPMENT_VERSION \"231\"" in cmake
assert "FXM_DEVELOPMENT_VERSION \"231\"" in build
print("gpu text glyph overdraw contract: ok")
