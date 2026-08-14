#!/usr/bin/env python3
"""Dev415: temporary FLUX_ENCODER_EXECUTABLE Editor preference."""

from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
header = (ROOT / "../../packages/flux-common/Shared/flux-encoder-client.h").read_text(encoding="utf-8")
client = (ROOT / "../../packages/flux-common/Shared/flux-encoder-client.cpp").read_text(encoding="utf-8")
preferences = (ROOT / "src/editor/title-editor/signal-handlers.inc").read_text(encoding="utf-8")
locale = (ROOT / "data/locale/en-US.ini").read_text(encoding="utf-8")
manifest = json.loads((ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 415
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 415
assert manifest["development_version"] >= 415

for token in [
    "configuredExecutablePath()",
    "setConfiguredExecutablePath(const QString &path)",
]:
    assert token in header, token

for token in [
    'kExecutableOverrideKey = "FLUX_ENCODER_EXECUTABLE"',
    'kMotionSettingsOrg = "FluxMotion"',
    "FluxEncoderClient::configuredExecutablePath()",
    "settings.remove(QString::fromUtf8(kExecutableOverrideKey))",
    "settings.sync()",
]:
    assert token in client, token

# The editor preference takes precedence over the process environment override.
assert client.index("FluxEncoderClient::configuredExecutablePath()") < client.index(
    'qEnvironmentVariable("FLUX_ENCODER_EXECUTABLE")'
)

for token in [
    'QStringLiteral("FLUX_ENCODER_EXECUTABLE")',
    "FluxEncoderClient::configuredExecutablePath()",
    "FluxEncoderClient::setConfiguredExecutablePath(",
    "QFileDialog::getOpenFileName(",
    'fxm_tr("OBSTitles.ChooseFluxEncoderExecutable")',
    "editor->refresh_flux_encoder_export_action()",
]:
    assert token in preferences, token

for key in [
    "OBSTitles.FluxEncoderPreferences=",
    "OBSTitles.FluxEncoderExecutablePlaceholder=",
    "OBSTitles.FluxEncoderExecutableTooltip=",
    "OBSTitles.FluxEncoderExecutableHint=",
    "OBSTitles.ChooseFluxEncoderExecutable=",
]:
    assert key in locale, key

print("Dev415 Flux Encoder executable preference contract passed")
