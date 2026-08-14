#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
auto = (root / "src/editor/properties-panel/auto-style-and-property-actions.inc").read_text(encoding="utf-8")
sync = (root / "src/editor/properties-panel/property-synchronization.inc").read_text(encoding="utf-8")
cpp = (root / "src/editor/properties-panel.cpp").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build = (root / "src/core/build-info.h").read_text(encoding="utf-8")
schema = (root / "Core/title-serialization-schema.h").read_text(encoding="utf-8")

assert "void PropertiesPanel::apply_anchor_preset" not in auto
assert sync.count("void PropertiesPanel::apply_anchor_preset") == 1
constructor_end = sync.index("    ticker_status_timer_->start();\n}")
anchor_definition = sync.index("void PropertiesPanel::apply_anchor_preset")
set_title_definition = sync.index("void PropertiesPanel::set_title")
assert constructor_end < anchor_definition < set_title_definition
assert '#include "properties-panel/auto-style-and-property-actions.inc"' in cpp
assert '#include "properties-panel/property-synchronization.inc"' in cpp
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert "kCurrentDevelopmentVersion = 243" in schema
assert "case 206:" in schema
print("Development Version 206 MSVC anchor-preset scope contract passed")
