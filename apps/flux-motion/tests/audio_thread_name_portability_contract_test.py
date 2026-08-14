#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "../../plugins/obs/flux-motion/src/title-audio-runtime.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
build = (root / "src/core/build-info.h").read_text()
schema = (root / "Core/title-serialization-schema.h").read_text()

assert "os_set_thread_name" not in source
assert "set_audio_output_thread_name();" in source
assert "SetThreadDescription" in source
assert "GetProcAddress(kernel32, \"SetThreadDescription\")" in source
assert "pthread_setname_np" in source
assert "prctl(PR_SET_NAME" in source
assert "#if defined(_WIN32)" in source
assert "#elif defined(__APPLE__)" in source
assert "#elif defined(__linux__)" in source
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert "kCurrentDevelopmentVersion = 243" in schema
assert "case 182:" in schema
assert "case 186:" in schema
print("portable audio worker thread naming contract passed")
