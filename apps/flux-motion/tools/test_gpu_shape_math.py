#!/usr/bin/env python3
"""Numerical reference checks for Phase 11 primitive geometry/padding math."""

from __future__ import annotations

import math


def polygon_distance_unit(x: float, y: float, sides: int) -> float:
    half_sector = math.pi / max(sides, 3)
    full_sector = half_sector * 2.0
    angle_from_top = math.atan2(y, x) + math.pi / 2.0
    angle_from_top -= math.floor(angle_from_top / full_sector) * full_sector
    side_normal_delta = angle_from_top - half_sector
    return math.hypot(x, y) * math.cos(side_normal_delta) - math.cos(half_sector)


def assert_close(value: float, expected: float = 0.0, epsilon: float = 1e-9) -> None:
    if abs(value - expected) > epsilon:
        raise AssertionError(f"{value} != {expected} within {epsilon}")


# The shader uses circumradius-normalized vertices. Every vertex and every edge
# midpoint must lie on the same zero contour for the legacy primitive polygon.
for sides in (3, 4, 5, 6, 12, 64):
    vertices: list[tuple[float, float]] = []
    for index in range(sides):
        angle = -math.pi / 2.0 + 2.0 * math.pi * index / sides
        vertices.append((math.cos(angle), math.sin(angle)))
    for point in vertices:
        assert_close(polygon_distance_unit(*point, sides))
    for index, first in enumerate(vertices):
        second = vertices[(index + 1) % sides]
        midpoint = ((first[0] + second[0]) * 0.5,
                    (first[1] + second[1]) * 0.5)
        assert_close(polygon_distance_unit(*midpoint, sides))

# Affine box scaling must preserve the zero contour: the shader divides by the
# half-size before evaluating the normalized polygon.
for half_width, half_height in ((320.0, 90.0), (40.0, 240.0), (1.0, 1.0)):
    sides = 6
    for index in range(sides):
        angle = -math.pi / 2.0 + 2.0 * math.pi * index / sides
        x = math.cos(angle) * half_width
        y = math.sin(angle) * half_height
        assert_close(polygon_distance_unit(x / half_width, y / half_height, sides))

# The exact rounded triangle/diamond fallback interprets roundness as a distance
# along both adjacent edges and clamps it to half of the shorter edge. Verify
# that positive roundness creates two distinct edge points at every corner.
def rounded_corner(previous: tuple[float, float], anchor: tuple[float, float],
                   following: tuple[float, float], requested: float) -> tuple[tuple[float, float], tuple[float, float], float]:
    incoming_length = math.dist(anchor, previous)
    outgoing_length = math.dist(anchor, following)
    radius = min(max(requested, 0.0), min(incoming_length, outgoing_length) * 0.5)

    def toward(point: tuple[float, float]) -> tuple[float, float]:
        dx, dy = point[0] - anchor[0], point[1] - anchor[1]
        length = math.hypot(dx, dy)
        return (dx / length, dy / length)

    to_previous = toward(previous)
    to_following = toward(following)
    incoming = (anchor[0] + to_previous[0] * radius,
                anchor[1] + to_previous[1] * radius)
    outgoing = (anchor[0] + to_following[0] * radius,
                anchor[1] + to_following[1] * radius)
    return incoming, outgoing, radius

for sides in (3, 4):
    vertices = []
    for index in range(sides):
        angle = -math.pi / 2.0 + 2.0 * math.pi * index / sides
        vertices.append((math.cos(angle) * 160.0, math.sin(angle) * 90.0))
    for index, anchor in enumerate(vertices):
        incoming, outgoing, radius = rounded_corner(
            vertices[(index - 1) % sides], anchor, vertices[(index + 1) % sides], 24.0)
        assert radius > 0.0
        assert math.dist(incoming, anchor) > 0.0
        assert math.dist(outgoing, anchor) > 0.0
        assert math.dist(incoming, anchor) <= 24.0 + 1e-9
        assert math.dist(outgoing, anchor) <= 24.0 + 1e-9

# Surface padding must fully contain the requested stroke extent plus the
# two-logical-pixel antialias guard used by the implementation.
def padding(stroke_width: float, alignment: int) -> float:
    if alignment == 0:
        return math.ceil(stroke_width) + 2.0
    if alignment == 2:
        return 2.0
    return math.ceil(stroke_width * 0.5) + 2.0

for stroke in (0.0, 0.25, 1.0, 7.5, 64.0):
    assert padding(stroke, 0) >= stroke + 2.0
    assert padding(stroke, 1) >= stroke * 0.5 + 2.0
    assert padding(stroke, 2) >= 2.0

print("Phase 11 polygon boundary, rounded-corner fallback and stroke-padding math passed")
