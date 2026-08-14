from pathlib import Path

root = Path(__file__).resolve().parents[1]
def read(path): return (root / path).read_text(encoding="utf-8")

build = read("src/core/build-info.h")
logger_h = read("src/core/title-logger.h")
logger = read("src/core/title-logger.cpp")
ui = read("src/editor/title-editor/signal-handlers.inc")
runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
tick = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
render = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")

assert 'FXM_DEVELOPMENT_VERSION "313"' in build
assert 'QString group;' in logger_h
for group in ["Core and application", "OBS source", "Editor and interface", "Title model and animation", "Rendering", "Cache and media"]:
    assert group in logger
for category in ["SourceTiming", "SourcePresentation", "SourceFlicker", "SourceMasks"]:
    assert category in logger
assert 'QMap<QString, QVBoxLayout *>' in ui
assert 'diagnostic_tick_serial' in runtime
assert 'event=tick-cadence-anomaly' in tick
assert 'event=render-skipped' in render
assert 'event=render-presented' in render
assert 'event=visible-frame-gap' in render
assert 'event=frame-consistency-anomaly' in render
print("Development Version 313 logging/source diagnostics contract passed")
