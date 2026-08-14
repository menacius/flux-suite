from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src' / 'editor' / 'fxm-modern-controls.cpp'


def test_qpainterpath_is_included_for_defaults_icon_arrow():
    text = SRC.read_text(encoding='utf-8')
    assert '#include <QPainterPath>' in text
    assert 'QPainterPath arrow;' in text
