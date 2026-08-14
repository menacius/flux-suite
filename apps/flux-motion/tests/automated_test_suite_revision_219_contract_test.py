#!/usr/bin/env python3
"""Source contract for Development Version 219 automated tests and render hot-path repair."""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
opaque = read("Core/serialization-passthrough.h")
title_data = read("src/core/title-data.cpp")
layer_model = read("Core/layer-model.h")
effects = read("src/effects/layer-effects.h")
transitions = read("src/transitions/layer-transition.h")
runner = read("tools/run_automated_test_suite.py")
manifest = json.loads(read("tests/test-suite-manifest.json"))
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION \"(219|2[2-9][0-9]|[3-9][0-9]{2,})\"\)', cmake)
assert re.search(r'#define FXM_DEVELOPMENT_VERSION \"(219|2[2-9][0-9]|[3-9][0-9]{2,})\"', build)
assert re.search(r'kCurrentDevelopmentVersion = (219|2[2-9][0-9]|[3-9][0-9]{2,})', schema)
assert 'case 219:' in schema

assert 'class OpaqueSerializationPassthrough' in opaque
assert 'std::shared_ptr<const std::string>' in opaque
assert 'std::is_nothrow_copy_constructible' in read("tests/serialization_passthrough_hot_path_test.cpp")
for model in (layer_model, effects, transitions, read("Core/title-data.h")):
    assert 'OpaqueSerializationPassthrough serialization_passthrough_json' in model
    assert 'std::string serialization_passthrough_json' not in model

assert 'bool preserve_serialization_passthrough = true' in title_data
assert 'layer_to_json(layer, false, false, nullptr, nullptr, false)' in title_data
assert 'Render fingerprints explicitly disable it' in title_data

assert manifest['development_version'] >= 219
assert len(manifest['required_areas']) >= 10
for area in manifest['required_areas']:
    assert area in manifest['areas']
    assert manifest['areas'][area]['python'] or manifest['areas'][area]['native']
for profile in ('source', 'smoke', 'full', 'stress'):
    assert profile in manifest['profiles']
assert '--validate-only' in runner
assert '--json-report' in runner
assert 'ctest' in runner

assert 'serialization_passthrough_hot_path_test' in cmake
assert 'run_automated_test_suite.py --validate-only' in cmake
assert re.search(r'Development Version (219|2[2-9][0-9]|[3-9][0-9]{2,})', readme)
assert re.search(r'# v0.8.11-alpha — Development Version (219|2[2-9][0-9]|[3-9][0-9]{2,})', changelog)
architecture = read('docs/ARCHITECTURE_AND_BUILD.md')
assert '**source:**' in architecture
assert '**stress:**' in architecture

print('Development Version 219 automated test-suite contract passed')
