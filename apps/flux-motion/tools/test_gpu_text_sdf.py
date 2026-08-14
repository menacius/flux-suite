#!/usr/bin/env python3
"""Compile and exercise the actual Phase 12C C++ signed-distance-field source."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/rendering/title-gpu-text-sdf.cpp"
HEADER = ROOT / "src/rendering/title-gpu-text-sdf.h"
COMPILER = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")

if not COMPILER:
    raise SystemExit("No C++ compiler is available for the Phase 12C SDF test")

program = r'''
#include "title-gpu-text-sdf.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <limits>
#include <vector>

using fxm::gpu_text::build_glyph_sdf;
using fxm::gpu_text::text_stroke_coverage_extents;
using fxm::gpu_text::text_stroke_draw_phase;

static int value_at(const std::vector<uint8_t> &pixels, int width, int x, int y)
{
    return static_cast<int>(pixels[static_cast<size_t>(y) * width + x]);
}

static float smoothstep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float coverage_inside(float distance, float aa = 0.01f)
{
    return smoothstep(-aa, aa, distance);
}

static float stroke_coverage(float signed_distance,
                             const fxm::gpu_text::TextStrokeCoverageExtents &extent)
{
    return std::clamp(
        coverage_inside(signed_distance + extent.outside) -
        coverage_inside(signed_distance - extent.inside), 0.0f, 1.0f);
}

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

static Rgba over(const Rgba &source, const Rgba &destination)
{
    return {source.r + destination.r * (1.0f - source.a),
            source.g + destination.g * (1.0f - source.a),
            source.b + destination.b * (1.0f - source.a),
            source.a + destination.a * (1.0f - source.a)};
}

int main()
{
    const auto outer = text_stroke_coverage_extents(12.0f, 0);
    const auto mid = text_stroke_coverage_extents(12.0f, 1);
    const auto inner = text_stroke_coverage_extents(12.0f, 2);
    assert(outer.outside == 12.0f && outer.inside == 0.0f);
    assert(mid.outside == 6.0f && mid.inside == 6.0f);
    assert(inner.outside == 0.0f && inner.inside == 12.0f);
    assert(text_stroke_draw_phase(false) == 0);
    assert(text_stroke_draw_phase(true) == 2);

    // Signed distance is positive inside the glyph. Verify actual placement,
    // not only the returned width tuple.
    assert(stroke_coverage(-6.0f, outer) > 0.99f);
    assert(stroke_coverage(6.0f, outer) < 0.01f);
    assert(stroke_coverage(-6.0f, inner) < 0.01f);
    assert(stroke_coverage(6.0f, inner) > 0.99f);
    assert(stroke_coverage(-3.0f, mid) > 0.99f);
    assert(stroke_coverage(3.0f, mid) > 0.99f);
    assert(stroke_coverage(-8.0f, mid) < 0.01f);
    assert(stroke_coverage(8.0f, mid) < 0.01f);

    // The same overlapping mid-stroke pixel must produce a different visible
    // result when composed behind versus in front of an opaque fill.
    const Rgba red_stroke{1.0f, 0.0f, 0.0f, 1.0f};
    const Rgba blue_fill{0.0f, 0.0f, 1.0f, 1.0f};
    const Rgba transparent{};
    const Rgba behind = over(blue_fill, over(red_stroke, transparent));
    const Rgba front = over(red_stroke, over(blue_fill, transparent));
    assert(behind.b > 0.99f && behind.r < 0.01f);
    assert(front.r > 0.99f && front.b < 0.01f);

    const uint8_t single[9] = {
        0, 0, 0,
        0, 255, 0,
        0, 0, 0,
    };
    int width = 0;
    int height = 0;
    const auto sdf = build_glyph_sdf(single, 3, 3, 3, 4, width, height);
    assert(!sdf.empty());
    assert(width == 15 && height == 15); // spread + two-pixel guard on each side
    assert(value_at(sdf, width, 7, 7) > 128);
    assert(value_at(sdf, width, 0, 0) < 128);
    assert(value_at(sdf, width, 7, 7) > value_at(sdf, width, 7, 6));

    const uint8_t block[16] = {
        255, 255, 255, 255,
        255, 255, 255, 255,
        255, 255, 255, 255,
        255, 255, 255, 255,
    };
    int block_width = 0;
    int block_height = 0;
    const auto block_sdf = build_glyph_sdf(
        block, 4, 4, 4, 3, block_width, block_height);
    assert(!block_sdf.empty());
    assert(value_at(block_sdf, block_width, 8, 8) > 128);
    assert(value_at(block_sdf, block_width, 0, 0) < 128);

    int repeat_width = 0;
    int repeat_height = 0;
    const auto repeat = build_glyph_sdf(
        single, 3, 3, 3, 4, repeat_width, repeat_height);
    assert(repeat_width == width && repeat_height == height);
    assert(repeat == sdf);

    int invalid_width = 99;
    int invalid_height = 99;
    const auto invalid = build_glyph_sdf(
        nullptr, 3, 3, 3, 4, invalid_width, invalid_height);
    assert(invalid.empty());
    assert(invalid_width == 0 && invalid_height == 0);

    invalid_width = invalid_height = 99;
    assert(build_glyph_sdf(single, 3, 3, 2, 4,
                           invalid_width, invalid_height).empty());
    assert(invalid_width == 0 && invalid_height == 0);

    invalid_width = invalid_height = 99;
    assert(build_glyph_sdf(single, 3, 3, 3,
                           std::numeric_limits<int>::max(),
                           invalid_width, invalid_height).empty());
    assert(invalid_width == 0 && invalid_height == 0);

    invalid_width = invalid_height = 99;
    assert(build_glyph_sdf(single, std::numeric_limits<int>::max(), 1,
                           std::numeric_limits<int>::max(), 4,
                           invalid_width, invalid_height).empty());
    assert(invalid_width == 0 && invalid_height == 0);

    std::cout << "Phase 12D C++ SDF stroke placement, order, padding and determinism passed\n";
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="obs-fxm-phase12c-sdf-") as directory:
    temp = Path(directory)
    test_cpp = temp / "test.cpp"
    executable = temp / "test-gpu-text-sdf"
    test_cpp.write_text(program, encoding="utf-8")
    command = [
        COMPILER,
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{HEADER.parent}",
        str(test_cpp),
        str(SOURCE),
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=ROOT)
