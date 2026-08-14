#!/usr/bin/env python3
"""Structural contract checks for the Windows incremental updater/package flow."""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / "build-windows.ps1").read_text(encoding="utf-8")
UPDATER = (ROOT / "update-and-build.ps1").read_text(encoding="utf-8")
CONFIG = json.loads((ROOT / "update-and-build.config.json").read_text(encoding="utf-8"))
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")
    print(f"PASS: {message}")


project = re.search(r"project\s*\(\s*[^\s\)]+\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)", CMAKE, re.I | re.S)
prerelease = re.search(r'^\s*set\s*\(\s*OBS_FXM_PRERELEASE\s+"([^"]*)"\s*\)', CMAKE, re.I | re.M)
dev = re.search(r'^\s*set\s*\(\s*OBS_FXM_DEVELOPMENT_VERSION\s+"([^"]*)"\s*\)', CMAKE, re.I | re.M)
require(project is not None, "CMake project version is present")
require(prerelease is not None, "CMake prerelease channel is present")
require(dev is not None, "CMake development version is present")
expected = f"Flux_Motion_v{project.group(1)}-{prerelease.group(1)}_development-version-{dev.group(1)}_windows-x64.zip"
require(re.fullmatch(
    r"Flux_Motion_v[0-9]+(?:\.[0-9]+)*-[A-Za-z0-9.-]+_development-version-[0-9]{3}_windows-x64\.zip",
    expected) is not None, "expected current package filename is stable")

for token in (
    "[string]$PackageName = \"Flux_Motion\"",
    "[string]$PackagePlatform",
    "[string]$PackageOutputDir",
    "[switch]$SkipPackage",
    "$DevelopmentComponent = \"development-version-$DevelopmentVersion\"",
    "$PackageFileName =",
    "Compress-Archive -LiteralPath $StagedPluginRoot",
):
    require(token in BUILD, f"build helper contains {token}")

stage_pos = BUILD.index("=== Assembling final OBS plugin package ===")
install_pos = BUILD.index("=== Installing final package to OBS ===")
zip_pos = BUILD.index("=== Creating distribution ZIP ===")
require(stage_pos < install_pos < zip_pos, "one staged package feeds OBS install and ZIP creation")
require('Copy-Item -Force -Recurse (Join-Path $StagedPluginRoot "*") $ObsPluginRoot' in BUILD, "OBS install copies the final staged package")
require('Copy-Item -Force -LiteralPath $Dll.FullName -Destination $StagedPluginBin' in BUILD, "runtime DLLs are included in the staged package")
require("if ($SkipInstall)" in BUILD and "if ($SkipPackage)" in BUILD, "install and ZIP creation can be disabled independently")

require(CONFIG.get("SkipInstall") is False, "OBS installation is enabled by default")
require(CONFIG.get("CreatePackage") is True, "distribution ZIP creation is enabled by default")
require(CONFIG.get("PackageName") == "Flux_Motion", "configured package name matches naming contract")
require(CONFIG.get("PackagePlatform") == "", "platform defaults to architecture-derived naming")
require(CONFIG.get("PackageOutputDirectory") == "", "ZIP defaults to the active build directory")

for token in (
    "$CreatePackage = [bool](Get-ConfigValue $Config 'CreatePackage' $true)",
    "if (-not $CreatePackage) { $BuildArguments += '-SkipPackage' }",
    "@{ ConfigName = 'PackageName';        Argument = '-PackageName' }",
    "@{ ConfigName = 'PackagePlatform';    Argument = '-PackagePlatform' }",
    "$BuildArguments += '-PackageOutputDir'",
):
    require(token in UPDATER, f"updater forwards packaging setting: {token}")

print(f"\nExpected package: {expected}")
print("All update/build packaging contracts passed.")
