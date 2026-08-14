from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SESSION = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc").read_text(encoding="utf-8")
PRESENTATION = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc").read_text(encoding="utf-8")

assert "std::string last_error_storage;" in SESSION
assert "session->last_error_storage =" in PRESENTATION
assert "session->last_error = session->last_error_storage.c_str();" in PRESENTATION
assert 'session->last_error = std::string("Effect technique executed no passes: ")' not in PRESENTATION
print("Development Version 224 MSVC diagnostic storage contract passed")
