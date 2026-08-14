from pathlib import Path

root = Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build = (root / "src/core/build-info.h").read_text(encoding="utf-8")
schema = (root / "Core/title-serialization-schema.h").read_text(encoding="utf-8")
readme = (root / "README.md").read_text(encoding="utf-8")
changelog = (root / "docs/CHANGELOG.md").read_text(encoding="utf-8")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema
assert 'case 211:' in schema
assert 'Automated source, smoke, full and stress test profiles' in readme
assert '## Development Version 211 — Compatibility and Regression Completion' in changelog
assert 'Flux_Motion_v0.8.11-alpha_development-version-240.zip' in readme

print("Development Version 211 compatibility/regression contract passed")
