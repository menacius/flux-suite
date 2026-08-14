#!/usr/bin/env python3
"""Deterministic FXM automated test-suite runner.

The source profile needs only Python. Native profiles additionally consume a
configured CMake/CTest build directory containing the plugin's test targets.
The manifest version is validated against the current CMake development build
instead of a historical hard-coded delivery number.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable


@dataclass
class Result:
    name: str
    kind: str
    status: str
    duration_seconds: float
    return_code: int
    stdout: str = ""
    stderr: str = ""


def current_development_version(root: Path) -> int:
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r'set\(OBS_FXM_DEVELOPMENT_VERSION\s+"([0-9]+)"\)', cmake)
    if not match:
        raise ValueError("cannot determine the current development version")
    return int(match.group(1))


def load_manifest(root: Path) -> dict:
    path = root / "tests" / "test-suite-manifest.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("unsupported test-suite manifest schema")
    current = current_development_version(root)
    if data.get("development_version") != current:
        raise ValueError(
            "test-suite manifest is not synchronized with "
            f"Development Version {current}")
    return data


def validate_manifest(root: Path, manifest: dict) -> list[str]:
    errors: list[str] = []
    areas = manifest.get("areas", {})
    required = manifest.get("required_areas", [])
    for name in required:
        if name not in areas:
            errors.append(f"missing required test area: {name}")
    for area_name, area in areas.items():
        python_tests = area.get("python", [])
        native_tests = area.get("native", [])
        if not python_tests and not native_tests:
            errors.append(f"test area has no tests: {area_name}")
        for relative in python_tests:
            path = root / relative
            if not path.is_file():
                errors.append(f"missing Python test in {area_name}: {relative}")
    profiles = manifest.get("profiles", {})
    for name in ("source", "smoke", "full", "stress"):
        if name not in profiles:
            errors.append(f"missing required profile: {name}")
    all_contracts = {str(path.relative_to(root)).replace(os.sep, "/")
                     for path in (root / "tests").glob("*.py")}
    represented = {item for area in areas.values() for item in area.get("python", [])}
    if not represented.issubset(all_contracts):
        errors.append("manifest references Python files outside the contract suite")

    cmake_text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    ctest_names = set(re.findall(r"add_test\(NAME\s+([A-Za-z0-9_\-]+)", cmake_text))
    native_references = {item for area in areas.values() for item in area.get("native", [])}
    for profile in profiles.values():
        listed = profile.get("native_tests", [])
        if isinstance(listed, list):
            native_references.update(str(item) for item in listed)
    missing_native = sorted(native_references - ctest_names)
    if missing_native:
        errors.append("manifest references unknown CTest targets: " + ", ".join(missing_native))
    return errors


def run_process(name: str, kind: str, command: list[str], cwd: Path,
                timeout: int) -> Result:
    start = time.perf_counter()
    environment = os.environ.copy()
    environment.setdefault("PYTHONUTF8", "1")
    try:
        completed = subprocess.run(command, cwd=cwd, env=environment, text=True,
                                   capture_output=True, timeout=timeout,
                                   check=False)
        status = "passed" if completed.returncode == 0 else "failed"
        return Result(name, kind, status, time.perf_counter() - start,
                      completed.returncode, completed.stdout, completed.stderr)
    except subprocess.TimeoutExpired as exc:
        return Result(name, kind, "timeout", time.perf_counter() - start, 124,
                      exc.stdout or "", exc.stderr or "")


def python_contracts(root: Path) -> Iterable[Path]:
    return sorted((root / "tests").glob("*.py"))


def run_python_suite(root: Path, timeout: int, fail_fast: bool) -> list[Result]:
    results: list[Result] = []
    for test in python_contracts(root):
        result = run_process(test.name, "python", [sys.executable, str(test)], root, timeout)
        results.append(result)
        print(f"{result.status.upper():7} {test.name} ({result.duration_seconds:.2f}s)")
        if result.status != "passed":
            if result.stdout:
                print(result.stdout, file=sys.stderr, end="")
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
            if fail_fast:
                break
    return results


def run_native_suite(root: Path, build_dir: Path, tests: object,
                     timeout: int, jobs: int) -> list[Result]:
    if not build_dir.is_dir():
        raise ValueError(f"native build directory does not exist: {build_dir}")
    command = ["ctest", "--test-dir", str(build_dir), "--output-on-failure",
               "--timeout", str(timeout), "-j", str(max(1, jobs))]
    if tests != "all":
        names = [str(item) for item in tests]
        if not names:
            return []
        regex = "^(" + "|".join(names) + ")$"
        command.extend(["-R", regex])
    result = run_process("ctest", "native", command, root,
                         max(timeout * 10, 600))
    print(f"{result.status.upper():7} native CTest ({result.duration_seconds:.2f}s)")
    if result.status != "passed":
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
    return [result]


def write_report(path: Path, profile: str, results: list[Result],
                 development_version: int) -> None:
    payload = {
        "development_version": development_version,
        "profile": profile,
        "passed": sum(result.status == "passed" for result in results),
        "failed": sum(result.status != "passed" for result in results),
        "duration_seconds": sum(result.duration_seconds for result in results),
        "results": [asdict(result) for result in results],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("source", "smoke", "full", "stress"),
                        default="source")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--timeout", type=int)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--json-report", type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    manifest = load_manifest(root)
    errors = validate_manifest(root, manifest)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 2
    print("Test-suite manifest: valid")
    if args.validate_only:
        return 0

    profile = manifest["profiles"][args.profile]
    timeout = args.timeout or int(manifest.get("default_timeout_seconds", 90))
    results: list[Result] = []
    if profile.get("run_all_python_contracts", False):
        results.extend(run_python_suite(root, timeout, args.fail_fast))
        if args.fail_fast and any(result.status != "passed" for result in results):
            if args.json_report:
                write_report(args.json_report, args.profile, results,
                             int(manifest["development_version"]))
            return 1

    if profile.get("run_native", False):
        if args.build_dir is None:
            print("ERROR: --build-dir is required for native profiles", file=sys.stderr)
            return 2
        results.extend(run_native_suite(root, args.build_dir,
                                        profile.get("native_tests", "all"),
                                        timeout, args.jobs))

    if args.json_report:
        write_report(args.json_report, args.profile, results,
                             int(manifest["development_version"]))
    passed = sum(result.status == "passed" for result in results)
    print(f"Automated suite: {passed}/{len(results)} passed")
    return 0 if all(result.status == "passed" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
