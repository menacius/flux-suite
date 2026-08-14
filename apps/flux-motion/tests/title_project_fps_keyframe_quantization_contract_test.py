from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_obs_open_quantizes_before_replacing_authored_fps():
    source = read("src/editor/title-editor/playback-cache-preferences.inc")
    block = source[source.index("void TitleEditor::open_title"):
                   source.index("std::shared_ptr<Title> TitleEditor::clone_title")]
    authored = block.index("const double authored_frame_rate = title_->frame_rate;")
    quantize = block.index("quantize_title_keyframes_to_frame_rate(")
    replace = block.index("title_->frame_rate = project_frame_rate;")
    assert authored < quantize < replace
    assert "std::abs(authored_frame_rate - project_frame_rate) > 1.0e-6" in block


def test_quantizer_rounds_native_keyframes_to_nearest_project_frame():
    source = read("src/core/title-data.cpp")
    start = source.index("std::size_t quantize_title_keyframes_to_frame_rate")
    end = source.index("TitleDataStore::TitleDataStore()", start)
    block = source[start:end]
    assert 'member.key() == "keyframes"' in block
    assert "const double nearest_frame = std::round(frame_position);" in block
    assert 'keyframe["time"] = nearest_frame / target_fps;' in block
    assert "std::stable_sort(" in block
    assert "compacted.back() = keyframe;" in block
    assert "title_from_json(document, false)" in block


def test_reference_nearest_frame_behavior_and_on_frame_stability():
    fps = 30.0

    def quantize(seconds: float) -> float:
        frame_position = seconds * fps
        nearest = int(frame_position + 0.5)
        if abs(frame_position - nearest) <= 1.0e-7:
            return seconds
        return nearest / fps

    assert quantize(1.0 / 30.0) == 1.0 / 30.0
    assert quantize(1.0 / 25.0) == 1.0 / 30.0
    assert quantize(2.0 / 25.0) == 2.0 / 30.0

