#!/usr/bin/env python3
"""Dev346: active PSD effects, PSD rich text and collapsed import groups."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
source = (ROOT / "src/editor/title-editor/import-documents.inc").read_text(
    encoding="utf-8"
)
manifest = json.loads(
    (ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8")
)

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 346
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 346
assert manifest["development_version"] >= 346

# Modern lfx2/lmfx, legacy lrFX and the final FXM stack all reject disabled FX.
assert source.count("if (effect.enabled) effects.push_back") >= 2
assert source.count("if (effect.enabled) {") >= 2
assert "return !effect.enabled" in source
assert "layer->effects.erase(std::remove_if" in source
assert "masterFXSwitch" in source

# Photoshop EngineData run arrays become canonical UTF-8 FXM rich-text ranges.
for token in [
    "StyleRun", "ParagraphRun", "StyleSheetData", "RunLengthArray",
    "psd_engine_run_lengths", "psd_utf8_range", "HorizontalScale",
    "VerticalScale", "FontCaps", "FillColor", "FillFlag", "StrokeColor",
    "StrokeFlag",
    "RichTextCharAll", "RichTextParagraphAll",
    "rich_text_document_apply_paragraph_format",
    "rich_text_document_sync_layer_mirrors_canonical",
]:
    assert token in source, token

# Every group created or loaded by an import starts collapsed.
compact = re.sub(r"\s+", "", source)
assert compact.count("group_collapsed=true") >= 4
assert "layer->type==LayerType::Group)layer->group_collapsed=true" in compact
assert "group_collapsed=false" not in compact

print("Dev346 PSD active-effects, rich-text and collapsed-groups contract passed")
