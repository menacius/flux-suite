from pathlib import Path
import re

repo_root = Path(__file__).resolve().parents[3]
common = repo_root / "packages/flux-common/Shared/rendering-engine/title-source"
effects = (common / "gpu-effects-transitions.inc").read_text(encoding="utf-8")
draw = (common / "gpu-presentation-readback.inc").read_text(encoding="utf-8")
session = (common / "gpu-masks-groups-cache.inc").read_text(encoding="utf-8")
registration = (common / "source-registration.inc").read_text(encoding="utf-8")

assert "kGpuLayerCopyEffect" in effects
assert "kGpuLitLayerCopyEffect" in effects
unlit = effects.split("kGpuLayerCopyEffect", 1)[1].split("kGpuLitLayerCopyEffect", 1)[0]
assert "shadowMap0" not in unlit
assert "lightingEnabled" not in unlit
assert "lit_copy_effect" in session
assert '"core:lit-layer-copy"' in draw
assert re.search(r"lighting_enabled\s*\?\s*session->lit_copy_effect\s*:\s*session->copy_effect", draw)
assert 'legacy_si.id = "broadcast_graphics_live_source"' in registration
print("Development Version 420 shader startup and legacy OBS contract passed.")
