#!/usr/bin/env python3
"""Dev347: visible PSD text content and EngineData scale normalization."""
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

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 347
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 347
assert manifest["development_version"] >= 347

# Photoshop normally serializes 100% H/V scale as 1.0; tolerate producers
# which use percentage values without shrinking the common form to 1%.
for token in [
    "static float psd_engine_scale",
    "value > 10.0 ? value / 100.0 : value",
    'number(QStringLiteral("HorizontalScale"), format.scale_x)',
    'number(QStringLiteral("VerticalScale"), format.scale_y)',
]:
    assert token in source, token
assert "format.scale_x * 100.0" not in source
assert "format.scale_y * 100.0" not in source

# The descriptor-decoded UTF-8 text is authoritative at the layer conversion
# boundary, so canonical synchronization cannot replace it with an empty value.
compact = re.sub(r"\s+", "", source)
for token in [
    "conststd::stringdecoded_text=source.text.toUtf8().toStdString();",
    "layer->text_content=decoded_text;",
    "layer->rich_text.plain_text=decoded_text;",
    "layer->rich_text.normalize();rich_text_document_sync_layer_mirrors_canonical(*layer);",
]:
    assert token in compact, token

print("Dev347 PSD text content and scale contract passed")
