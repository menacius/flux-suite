from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
controls_h = read("Editor/fxm-modern-controls.h")
controls_cpp = read("Editor/fxm-modern-controls.cpp")
props = read("src/editor/properties-panel/popup-state.inc")
effects = read("src/effects/effects-panel.cpp")
manifest = read("tests/test-suite-manifest.json")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema
assert 'case 241:' in schema and 'case 240:' in schema
assert '"development_version": 243' in manifest
assert 'Development Version 243' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

# Shared Transform-panel-derived styling must be the one source of truth.
assert 'fxm_transform_panel_control_style' in controls_h
assert 'fxm_transform_panel_button_style' in controls_h
assert 'fxm_apply_transform_panel_widget_style' in controls_h
assert 'min-height:18px;max-height:20px' in controls_cpp
assert 'font-size:10px' in controls_cpp
assert 'border-radius:2px' in controls_cpp
assert 'width:12px' in controls_cpp
assert 'setFixedHeight(20)' in controls_cpp
assert 'setFixedSize(20, 20)' in controls_cpp

# Every collapsible panel, dynamic effect panel and header action must normalize itself.
assert 'fxm_apply_transform_panel_widget_style(section);' in controls_cpp
assert 'fxm_apply_transform_panel_widget_style(widget);' in controls_cpp
assert 'const QString control_style = fxm_transform_panel_control_style(pal);' in props
assert 'padding:2px 8px' in props
assert 'return fxm_transform_panel_control_style(qApp->palette());' in effects
