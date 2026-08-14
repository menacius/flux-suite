from pathlib import Path

root = Path(__file__).resolve().parents[1]
playback = (root / "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc").read_text(encoding="utf-8")

affected = playback[playback.index("else if (layer.type == LayerType::Video)"):playback.index("if (layer.type == LayerType::TransitionInput)")]

# Title does not own a frame_rate member. Video cache invalidation should use
# the media stream rate when known and fall back to the current OBS/source clock.
assert "session->title.frame_rate" not in playback
assert "title.frame_rate" not in playback
assert "frame_cache_key_for_layer(" in affected
assert "|video-frame=" not in affected

# Guard the exact MSVC regression: std::max must never be left with a single
# argument after removing the nonexistent Title::frame_rate access.
assert "std::max(1.0, static_cast<double>(session->title.frame_rate))" not in playback
assert "frame_cache_key_for_layer(" in affected
