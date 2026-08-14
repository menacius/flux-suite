#!/usr/bin/env python3
"""Source contract for anchor-stable 3D raster presentation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


transform_h = read("src/rendering/layer-transform-3d.h")
transform_cpp = read("src/rendering/layer-transform-3d.cpp")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")

# Content must be projected from its real source/local quads, not by fitting a
# unit square and extrapolating that matrix over off-centre raster coordinates.
assert "bool projected_local_quad_transform(" in transform_h
assert "bool projected_local_quad_transform(" in transform_cpp
assert "const QPolygonF &source_quad" in transform_h
assert "const QPolygonF &local_quad" in transform_h
assert "const QMatrix4x4 local_to_clip" in transform_cpp
assert "QTransform::quadToQuad(source_quad, canvas_quad, transform)" in transform_cpp
assert "clip.w() <= kEpsilon" in transform_cpp

# The final GPU presentation computes the same padded local rectangle already
# used by projected bounds and fits the homography directly in texture space.
for token in (
    "const double local_x0 = sx * origin.x();",
    "const double local_y0 = sy * origin.y();",
    "const double local_x1 = local_x0 + sx * logical_width;",
    "const double local_y1 = local_y0 + sy * logical_height;",
    "QPointF(static_cast<double>(texture_width),",
    "fxm::transform3d::projected_local_quad_transform(",
    "gpu_qtransform_to_gs_matrix(texture_to_canvas)",
    "else if (exact_projected_raster)",
):
    assert token in presentation

# Once the exact texture-to-canvas homography is installed, the old local
# scale/translation must not be applied a second time.
assert "if (!exact_projected_raster)" in presentation
assert "making content and overlays use the same geometry" in presentation

print("Anchor-stable projected 3D content contract passed")
