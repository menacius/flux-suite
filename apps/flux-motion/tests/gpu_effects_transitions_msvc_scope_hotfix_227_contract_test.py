from pathlib import Path

root = Path(__file__).resolve().parents[1]
source_dir = root / "src" / "obs" / "title-source"
gpu_tick = (source_dir / "gpu-effects-transitions.inc").read_text(encoding="utf-8")

# MSVC reports every following static helper as a local function when the
# source_video_tick block is accidentally duplicated without closing its first
# body. Keep a direct contract for that regression.
assert gpu_tick.count("static void source_video_tick(void *priv, float seconds)") == 1
assert gpu_tick.count("editor_title_snapshot_for_source(data)") == 1
assert "sync_source_audio_active(data, nullptr, \"empty-title-binding\");\n        return;\n    }\n\nstatic void source_video_tick" not in gpu_tick


def stripped_brace_balance(text: str) -> tuple[int, int]:
    state = "normal"
    depth = 0
    minimum = 0
    i = 0
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "normal":
            if ch == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if ch == '"':
                state = "string"
                i += 1
                continue
            if ch == "'":
                state = "char"
                i += 1
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                minimum = min(minimum, depth)
        elif state == "line":
            if ch == "\n":
                state = "normal"
        elif state == "block":
            if ch == "*" and nxt == "/":
                state = "normal"
                i += 2
                continue
        elif state == "string":
            if ch == "\\":
                i += 2
                continue
            if ch == '"':
                state = "normal"
        elif state == "char":
            if ch == "\\":
                i += 2
                continue
            if ch == "'":
                state = "normal"
        i += 1
    assert state == "normal"
    return depth, minimum

# gpu-effects-transitions.inc intentionally opens TitleGpuRenderSession, which
# is completed by gpu-masks-groups-cache.inc. The pair must return to top-level
# scope before the next include defines more file-scope helpers.
combined = "\n".join(
    (source_dir / name).read_text(encoding="utf-8")
    for name in ["gpu-effects-transitions.inc", "gpu-masks-groups-cache.inc"]
)
depth, minimum = stripped_brace_balance(combined)
assert minimum == 0
assert depth == 0
