# Validation report — Development Version 001

Date: 2026-07-03

## Completed checks

- Source contract verifier passes.
- All files referenced by CMake and the Qt resource collection exist.
- CMake successfully detects the C++20 compiler and reaches Qt package discovery.
- A one-second H.264/AAC MP4 encode using the software-profile settings completed successfully with FFmpeg 7.1.3.
- A one-second ProRes 422 HQ / PCM 24-bit MOV encode using the editing-profile settings completed successfully with FFmpeg 7.1.3.
- FFmpeg capability parsing was checked against a build exposing CUDA, VAAPI, QSV, DRM, OpenCL and Vulkan hardware acceleration APIs.
- Runtime probes correctly rejected NVENC, QSV and VAAPI in an environment where those encoders were compiled into FFmpeg but the corresponding GPU runtime/device was unavailable.
- Queue logic was checked for interrupted-job recovery, cancelled-job signal ordering and prevention of automatic infinite retries after a failed job.
- Hardware encode failure now performs one clean retry with the configured software fallback.

## Build limitation of the validation environment

A complete Qt compilation could not be performed in the packaging environment because the Qt 6 development SDK and `Qt6Config.cmake` are not installed. The project is configured for Qt 6.5 or newer and includes Windows/Linux build commands and CMake presets.

## Development Version 005 validation

The source contract verifier passes after the workspace, preview and preflight changes. A full Qt/MSVC build was not available in the packaging environment; Windows compilation should be performed with `build-windows.ps1`.

## Development Version 005 source validation

- Queue rows remain read-only; changes are exposed only through explicit professional actions.
- Duplicate, retry/reset, move up/down, and batch output-folder actions persist to SQLite.
- Queue order is persisted as scheduler priority, and QueueRunner selects the highest-priority pending item.
- Idle source preview uses a 0–1000 timeline scrubber and exact source-duration timecode.
- Active encoding preview remains driven by worker `outTimeMs` and disables manual seeking.

## Development Version 006 validation

- Source contract verification passes.
- Queue Format and Preset cells use controlled combo boxes and remain disabled for active jobs.
- Format families are derived from codecs, preventing H.264, HEVC and AV1 presets from being mixed merely because they share MP4 containers.
- Audio-only presets use explicit `-vn` mapping and no longer pass a video stream to audio containers.
- The built-in catalog includes delivery, broadcast, mastering, proxy, archive, audio-only and detected hardware families.
- Full Qt/MSVC compilation was not available in the packaging environment and must be confirmed with `build-windows.ps1`.


## Development Version 011

- Provider ABI headers use opaque handles, fixed-width types, `struct_size`, ABI versioning, and reserved fields.
- Provider discovery rejects missing executables, incompatible ABI versions, and in-process providers.
- Flux Motion placeholder cannot falsely appear operational.
- Preset drag/drop validates availability and active-job mutability before changing a queue item.

- Queue selection/link contrast: Flux green selection and dedicated green queue links verified at source level.

## Development Version 012

- Added a reusable preview widget with Fit and 10%–400% zoom behavior.
- Added an In/Out-aware timeline control with visible range markers and a native playhead.
- Export Settings refreshes the displayed frame at the released playhead position.
- Encoding preview shows the queued profile trim range.
- Source-level checks completed; full Qt/MSVC build remains to be confirmed on Windows.


## Development Version 013

- Output Preview no longer copies the Source pixmap.
- A debounced asynchronous FFmpeg encode/decode chain generates the Output frame with the selected profile.
- Temporary preview segments are isolated in a temporary directory and cleaned when the Export Settings dialog closes.
- Failures are surfaced in the preview with encoder diagnostics.
- Source-level checks completed; full Qt/MSVC build remains to be confirmed on Windows.


## Development Version 021

- Verified the `drawtext` timecode filter syntax with the local FFmpeg build.
- Output Preview advances the configured start timecode by the preview segment offset.
- Export Settings session state is stored under the existing `Flux/FluxEncoder` QSettings namespace.
- Added WSL/Ubuntu LTS PowerShell and Bash build entry points with dependency installation, Qt provisioning, tests and packaging.
- Full Qt/MSVC build and runtime startup were confirmed on the target Windows system.

## Flux Encoder rebrand validation

- Window exposes Queue, Preset Browser, Media Browser, Encoding, Job Settings, Watch Folders and Log through each dock's native toggle action.
- The shared Lock Docks action removes dock close, move and floating features and stays synchronized across menus.
- The Windows build produces only `Flux Encoder.exe` and `flux-encoder-worker.exe`.
- Both supplied SVG designs are embedded; the splash artwork is also used by About.
- The executable embeds the Flux Encoder icon at Windows resource level.
- The UI palette matches Flux Motion with `#42a274` for links, focus, selection, tabs, switches and progress.
- All Satoshi weights are bundled and the family is installed before the application theme is applied.
- Source, provider SDK, build scripts, settings keys and package filenames contain no legacy product identifiers or aliases.
- Both CTest contract tests pass and the packaged application passes an offscreen startup smoke test.
