from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


helpers = read("src/editor/title-dock/template-library-helpers.inc")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
population = read("src/editor/title-dock/live-text-cache-playlist.inc")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))

cmake_version = int(re.search(
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build_info).group(1))
assert cmake_version >= 357
assert build_version >= 357
assert manifest["development_version"] >= 357

assert "live_cue_row_is_ending(const Title &title, int row)" in helpers
predicate = re.search(
    r"live_cue_row_is_ending\(const Title &title, int row\).*?\n\}",
    helpers,
    re.DOTALL,
)
assert predicate
assert "row == title.current_cue_row" in predicate.group(0)
assert "title.cue_uncue_requested || title.pending_cue_row >= 0" in predicate.group(0)

assert lifecycle.count("live_cue_row_is_ending(*title, row)") >= 3
assert "live_cue_row_is_ending(*current_title, row)" in population
assert "live_cue_row_is_ending(*title, 0)" in population
assert "const bool ending = live_cue_row_is_ending(*title, row);" in population

stale_manual_only = (
    "row == title->current_cue_row && title->cue_uncue_requested",
    "current && title->cue_uncue_requested",
    "live_cue_state_color(true, false, current_title->cue_uncue_requested)",
)
for stale in stale_manual_only:
    assert stale not in lifecycle
    assert stale not in population

print("development version 357 cue hand-off ending visual contract passed")
