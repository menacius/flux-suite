from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/editor/properties-panel/color-gradient-editing.inc").read_text(encoding="utf-8")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")

cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert cmake_version and int(cmake_version.group(1)) >= 284
assert build_version and int(build_version.group(1)) >= 284

match = re.search(
    r'connect\(stroke_options_trigger.*?this, \[([^\]]+)\]\(\) \{',
    source,
    flags=re.S,
)
assert match, "stroke options popup lambda was not found"
captures = {part.strip() for part in match.group(1).replace("\n", " ").split(",")}
assert "local_time" in captures, "outer stroke-options popup lambda must capture local_time"

assert '[this, local_time, emit_change](double v)' in source
assert '[this, offset, offset_key, local_time, emit_change]()' in source
print("Development Version 284 Stroke Offset lambda capture contract passed")
