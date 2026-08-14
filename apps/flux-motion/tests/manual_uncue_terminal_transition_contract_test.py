from pathlib import Path


def test_manual_uncue_never_returns_to_free_run():
    source = (Path(__file__).parents[1] / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc").read_text()
    marker = "Manual uncue is a terminal transition for every playback"
    assert marker in source
    block = source[source.index(marker) - 80: source.index(marker) + 700]
    assert "data->cue_phase = TitleSourceData::CuePhase::OutroOnly;" in block
    assert "data->cue_phase = TitleSourceData::CuePhase::FreeRun;" not in block
