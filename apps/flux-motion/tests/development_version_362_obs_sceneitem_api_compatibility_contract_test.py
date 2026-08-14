from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
cues = read("src/editor/title-dock/list-selection-cues.inc")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 362
assert build_version >= 362
assert manifest["development_version"] >= 362

# obs_sceneitem_set_selected is not part of the supported OBS scene-item API.
# Locking the private preview item is sufficient to prevent transform editing.
assert "obs_sceneitem_set_selected" not in cues
assert "obs_sceneitem_set_locked(preview_item, true);" in cues
assert "obs_sceneitem_set_visible(preview_item, true);" in cues

print("Development Version 362 OBS scene-item API compatibility contract: PASS")
