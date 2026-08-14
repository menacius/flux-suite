from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
header = read("src/editor/title-dock.h")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
helpers = read("src/editor/title-dock/template-library-helpers.inc")
cues = read("src/editor/title-dock/list-selection-cues.inc")
locale = read("data/locale/en-US.ini")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 364
assert build_version >= 364
assert manifest["development_version"] >= 364

# The former combined option is now the default-off two-stage Cue gate. The
# visible preview route is a separately persisted subordinate option.
for token in (
    "act_select_row_before_cue_",
    "act_show_cue_preview_",
    "select_row_before_cue_ = false",
    "show_cue_preview_ = true",
):
    assert token in header
assert "act_show_cue_preview_->setEnabled(select_row_before_cue_);" in cues
select_toggle = cues.index("connect(act_select_row_before_cue_")
show_toggle = cues.index("connect(act_show_cue_preview_", select_toggle)
assert "act_show_cue_preview_->setEnabled(enabled);" in cues[select_toggle:show_toggle]

# Select Row remains responsible for PreviewReady state and relative hotkey
# navigation even when the visible local/OBS Preview output is disabled.
assert "select_row_before_cue_ && allow_uncue" in cues
assert "select_row_before_cue_ && isPreviewReady()" in cues
route = cues[cues.index("void TitleDock::sync_live_cue_preview_output_route()"):
             cues.index("bool TitleDock::enterPreview(")]
assert "!isPreviewReady() || !show_cue_preview_" in route
assert "detach_live_cue_preview_from_obs();" in route
assert "live_cue_preview_panel_->hide();" in route

# Existing users migrate from the original option; new installs keep selection
# off while Show Preview is ready to use as soon as selection is enabled.
for token in (
    'kSelectRowBeforeCueKey = "selectRowBeforeCue"',
    'kShowCuePreviewKey = "showCuePreview"',
    'kLegacyShowPreviewBeforeCueKey = "showPreviewBeforeCue"',
):
    assert token in helpers
assert "settings.contains(select_row_key)" in lifecycle
assert "settings.value(legacy_preview_key, false).toBool()" in lifecycle
assert "QString::fromUtf8(kShowCuePreviewKey), true" in lifecycle

for key in (
    "OBSTitles.SelectRowBeforeCue",
    "OBSTitles.ShowCuePreview",
):
    assert key in locale
assert "OBSTitles.ShowPreviewBeforeCue=" not in locale

print("Development Version 364 Select Row and optional Preview contract: PASS")
