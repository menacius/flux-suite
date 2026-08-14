from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/canvas/canvas-preview/editor-3d-tools.inc").read_text(encoding="utf-8")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
header = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")

assert 'static_cast<double>(projection->width)' in source
assert 'static_cast<double>(projection->height)' in source
assert 'static_cast<double>(canvas_view_rect().width())' in source
assert 'static_cast<double>(canvas_view_rect().height())' in source
assert 'const double radius = std::clamp(' in source
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "303")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "303"' in header
