#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        raise AssertionError(f"unexpected {label}: {needle}")


def main() -> None:
    build = read("src/core/build-info.h")
    cmake = read("CMakeLists.txt")
    shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
    draw = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
    playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    ui = read("src/editor/properties-panel/construction-transform-character.inc")
    selection = read("src/editor/properties-panel/selection-refresh.inc")
    readme = read("README.md")

    assert any(f'FXM_DEVELOPMENT_VERSION "{v}"' in build for v in range(251, 300)), "build development version must remain >=251"
    assert any(f'OBS_FXM_DEVELOPMENT_VERSION "{v}"' in cmake for v in range(251, 300)), "cmake development version must remain >=251"

    require(shader, "uniform int imageScaleFilter;", "GPU filter selector uniform")
    require(shader, "sampler_state pointSampler", "GPU nearest sampler")
    require(shader, "sample_cubic", "GPU bicubic filter")
    require(shader, "sample_lanczos", "GPU Lanczos filter")
    require(shader, "sample_scaled_image(v.uv)", "layer copy shader uses GPU scale filter")
    require(draw, "gs_effect_set_int(image_scale_filter", "filter selector bound from layer")
    require(draw, "static_cast<int>(layer.scale_filter)", "layer filter passed to GPU")
    require(draw, "sourceTexelSize", "source texel size bound for high-quality taps")

    require(playback, "Filtering is now applied", "direct image filtering comment")
    require(playback, "direct-image|src=", "geometry-independent direct image key")
    require(playback, "if (direct_image &&", "direct image geometry update without transform-only requirement")
    require(playback, "entry.logical_width = updated_width;", "direct image metadata-only resize")
    forbid(playback, "transform_only_update && direct_image", "old transform-only gate for direct image geometry")

    require(ui, 'new QLabel(QStringLiteral("Box size")', "box size label")
    require(ui, "image_content_layout->addWidget(image_box_size_box_);", "box size embedded in source panel")
    forbid(ui, "fxm_add_panel_section(vl, image_box_size_box_", "separate box-size panel")
    require(selection, "Box size now lives inside Image Source/Video Source", "selection refresh no separate panel title")
    require(readme, "Development Version 251", "README version note")

    print("revision 251 GPU image filtering and box size UI contract OK")


if __name__ == "__main__":
    main()
