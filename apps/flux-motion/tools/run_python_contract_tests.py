#!/usr/bin/env python3
"""Run every source-level Python regression contract in deterministic order."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    tests = sorted((root / "tests").glob("*.py"))
    if not tests:
        print("No Python contract tests found", file=sys.stderr)
        return 2

    failures: list[str] = []
    environment = os.environ.copy()
    environment.setdefault("PYTHONUTF8", "1")
    for test in tests:
        try:
            result = subprocess.run(
                [sys.executable, str(test)],
                cwd=root,
                env=environment,
                text=True,
                capture_output=True,
                check=False,
                timeout=90,
            )
        except subprocess.TimeoutExpired as exc:
            failures.append(test.name)
            print(f"TIMEOUT {test.name} (90 seconds)", file=sys.stderr)
            if exc.stdout:
                print(exc.stdout, file=sys.stderr, end="")
            if exc.stderr:
                print(exc.stderr, file=sys.stderr, end="")
            continue
        if result.returncode == 0:
            print(f"PASS {test.name}")
            continue
        failures.append(test.name)
        print(f"FAIL {test.name}", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")

    print(f"Python contracts: {len(tests) - len(failures)}/{len(tests)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
