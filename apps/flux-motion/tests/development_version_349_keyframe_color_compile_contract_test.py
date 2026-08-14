from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
modern_h = read("Editor/fxm-modern-controls.h")
modern_cpp = read("Editor/fxm-modern-controls.cpp")
internal_h = read("src/editor/title-editor-internal.h")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 349
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 349

# hierarchy-model.inc is included by many editor-facing translation units, so
# it must not depend on a private constant formerly declared in a sibling .inc.
assert "C_KF_DOT" not in hierarchy
assert "return fxm_keyframe_color();" in hierarchy
assert '#include "fxm-modern-controls.h"' in internal_h

# Inspector diamonds and painted timeline keyframe shapes share the same
# exported color source, eliminating the deleted-symbol regression.
assert "QColor fxm_keyframe_color();" in modern_h
assert "QColor fxm_keyframe_color()" in modern_cpp
assert "const QColor keyframe_color = fxm_keyframe_color();" in modern_cpp

source_text = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for path in (ROOT / "src").rglob("*")
    if path.is_file() and path.suffix in {".cpp", ".h", ".inc"}
)
assert "C_KF_DOT" not in source_text

print("Development Version 349 shared keyframe-color compile contract: PASS")
