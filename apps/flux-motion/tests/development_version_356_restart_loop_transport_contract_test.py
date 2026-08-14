from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
transport = read("src/editor/title-editor/signal-handlers.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 356
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 356

# Restart loops have a distinct boundary signal. It is set for the direct
# preview Loop mode, the full-loop transport and the authored restart loop.
assert "bool loop_discontinuity = false;" in transport
assert transport.count("loop_discontinuity = unwrapped_time >= duration;") >= 3
authored_restart = transport[
    transport.index("} else {\n                t = playhead_ + dt;", transport.index("case 1:")):
    transport.index("            break;", transport.index("case 1:"))
]
assert "if (t >= loop_end)" in authored_restart
assert "loop_discontinuity = true;" in authored_restart
assert "t = loop_start + std::fmod(t - loop_end, loop_len);" in authored_restart

# The old audio clock may still report the pre-wrap end time. It cannot master
# this one discontinuous tick, and the new playhead is then published as a
# forced audio seek/resume so the private OBS source cannot remain at its end.
audio_gate = transport[
    transport.index("if (!stop_after_frame && !loop_discontinuity"):
    transport.index("if (cache_settings.cached_frames_only", transport.index(
        "if (!stop_after_frame && !loop_discontinuity"))
]
assert "obs_source_media_get_time" in audio_gate
post_apply = transport[transport.index("apply_playhead_change(next_playhead, true);") :]
assert "if (loop_discontinuity && playing_)" in post_apply
assert "sync_editor_audio_preview(true);" in post_apply

# Ping-pong is deliberately excluded: it reflects time around the boundary and
# flips direction without creating a seek discontinuity.
ping_pong = transport[
    transport.index("} else if (preview_ping_pong)"):
    transport.index("} else if (preview_loop)", transport.index(
        "} else if (preview_ping_pong)"))
]
assert "playback_reverse_ = true;" in ping_pong
assert "playback_reverse_ = false;" in ping_pong
assert "loop_discontinuity" not in ping_pong


def restart_loop_tick(playhead: float, dt: float, end: float,
                      start: float = 0.0) -> tuple[float, bool]:
    unwrapped = playhead + dt
    if unwrapped < end:
        return unwrapped, False
    return start + ((unwrapped - end) % (end - start)), True


# Regression model: at the boundary the wrapped transport wins over a stale
# audio time at the end. The next tick then continues from the loop start.
wrapped, discontinuity = restart_loop_tick(9.99, 0.02, 10.0)
stale_audio_time = 10.0
assert discontinuity and abs(wrapped - 0.01) < 1e-9
next_playhead = wrapped if discontinuity else stale_audio_time
assert abs(next_playhead - 0.01) < 1e-9
continued, second_discontinuity = restart_loop_tick(next_playhead, 0.02, 10.0)
assert not second_discontinuity and abs(continued - 0.03) < 1e-9

authored, authored_discontinuity = restart_loop_tick(7.99, 0.02, 8.0, 2.0)
assert authored_discontinuity and abs(authored - 2.01) < 1e-9

print("Development Version 356 restart-loop transport contract: PASS")
