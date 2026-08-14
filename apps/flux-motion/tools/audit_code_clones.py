#!/usr/bin/env python3
"""Report exact normalized multi-line code clones across src/.

This intentionally reports only exact sequences of meaningful lines. It is a
high-confidence duplication signal, not a semantic clone detector.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import argparse
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc"}


def meaningful_lines(path: Path) -> tuple[list[str], list[int]]:
    normalized: list[str] = []
    line_numbers: list[int] = []
    in_block_comment = False
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        stripped = line.strip()
        if in_block_comment:
            if "*/" in stripped:
                in_block_comment = False
            continue
        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_block_comment = True
            continue
        if (
            not stripped
            or stripped.startswith("//")
            or stripped.startswith("#")
            or stripped in {"{", "}", "};"}
        ):
            continue
        normalized.append(re.sub(r"\s+", " ", stripped))
        line_numbers.append(line_number)
    return normalized, line_numbers


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minimum-lines", type=int, default=20)
    args = parser.parse_args()
    minimum = max(4, args.minimum_lines)

    files = sorted(
        path
        for path in (ROOT / "src").rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )
    data = {path: meaningful_lines(path) for path in files}
    windows: dict[bytes, list[tuple[Path, int]]] = defaultdict(list)
    for path, (lines, _) in data.items():
        for start in range(len(lines) - minimum + 1):
            digest = hashlib.sha1(
                "\n".join(lines[start : start + minimum]).encode("utf-8")
            ).digest()
            windows[digest].append((path, start))

    results: list[tuple[int, Path, int, int, Path, int, int]] = []
    visited: set[tuple[str, int, str, int]] = set()
    for occurrences in windows.values():
        for left_index in range(len(occurrences)):
            for right_index in range(left_index + 1, len(occurrences)):
                left_path, left_start = occurrences[left_index]
                right_path, right_start = occurrences[right_index]
                if left_path == right_path:
                    continue
                if str(left_path) > str(right_path):
                    left_path, right_path = right_path, left_path
                    left_start, right_start = right_start, left_start
                identity = (
                    str(left_path), left_start, str(right_path), right_start
                )
                if identity in visited:
                    continue
                visited.add(identity)

                left_lines, left_numbers = data[left_path]
                right_lines, right_numbers = data[right_path]
                if (
                    left_start > 0
                    and right_start > 0
                    and left_lines[left_start - 1] == right_lines[right_start - 1]
                ):
                    continue

                length = 0
                while (
                    left_start + length < len(left_lines)
                    and right_start + length < len(right_lines)
                    and left_lines[left_start + length]
                    == right_lines[right_start + length]
                ):
                    length += 1
                if length < minimum:
                    continue
                results.append(
                    (
                        length,
                        left_path,
                        left_numbers[left_start],
                        left_numbers[left_start + length - 1],
                        right_path,
                        right_numbers[right_start],
                        right_numbers[right_start + length - 1],
                    )
                )

    results.sort(reverse=True, key=lambda item: item[0])
    print(f"Exact normalized code clones (minimum {minimum} meaningful lines)")
    for length, left, left_first, left_last, right, right_first, right_last in results:
        print(
            f"  {length:3} lines: {left.relative_to(ROOT)}:{left_first}-{left_last} "
            f"<=> {right.relative_to(ROOT)}:{right_first}-{right_last}"
        )
    print(f"  TOTAL: {len(results)} clone groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
