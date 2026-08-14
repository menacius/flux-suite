"""Read a modular C/C++ facade together with its ordered .inc implementation files."""
from __future__ import annotations
from pathlib import Path
import re
_MODULE_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+\.inc)"\s*$')

def read_source_bundle(path: Path, _stack: set[Path] | None = None) -> str:
    path = path.resolve()
    stack = set() if _stack is None else _stack
    if path in stack:
        raise RuntimeError(f"cyclic implementation-module include: {path}")
    stack.add(path)
    output: list[str] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True):
        match = _MODULE_INCLUDE.match(line.rstrip("\r\n"))
        if match:
            output.append(read_source_bundle(path.parent / match.group(1), stack))
        else:
            output.append(line)
    stack.remove(path)
    return "".join(output)
