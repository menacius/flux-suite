## 2026 - v0.8.18-alpha

- Synchronized application, provider and package metadata with the Flux Suite
  v0.8.18 alpha release.
- Retained the versioned Flux Motion renderer-provider handoff against the
  suite-owned rendering contracts; encoding behavior is otherwise unchanged.

## 2026 - v0.8.17-alpha

- Flux Suite coordinated alpha release.

## 2026 - 0.8.16-alpha

- Synchronized the Flux Encoder, Flux Motion Editor, Flux Motion OBS Plugin and
  headless renderer product version.

## v0.11.0 — Flux Encoder rebrand

- Added every application dock to the Window menu with visibility-synchronized checkmarks.
- Added a shared **Lock Docks** action to the Window menu and workspace popup.
- Renamed the application, worker, build targets, packages, settings namespaces and provider SDK to Flux Encoder with no legacy aliases.
- Added the supplied Flux Encoder application icon and shared splash/About artwork.
- Adopted the Flux Motion graphite palette with `#42a274` as the application accent.
- Bundled the complete Satoshi font family and applied it throughout the UI.
- Renamed the render-provider manifest and protocol surface for Flux Encoder and Flux Motion.

## v0.11.0 Development Version 021 — Session Persistence, Timecode Reliability and WSL Build

- Export Settings now preserves window geometry, maximized state, splitter proportions, selected preview/settings tabs, zoom and Fit mode between sessions.
- Corrected the FFmpeg timecode overlay to use `timecode_rate` and 24-hour wrapping.
- Compressed Output Preview now offsets its starting timecode to the actual preview segment, so the displayed overlay matches the selected video position.
- Final rendering and compressed preview share the same FFmpeg filter construction.
- Added `build-wsl-ubuntu-lts.ps1` plus an Ubuntu helper that installs dependencies, provisions Qt 6 when needed, builds, tests, installs and packages Flux Encoder under WSL.

# Development Version 020 — Format-aware Preset Selector and Output-first Preview

- Replaced the free-form Preset header field with a format-aware preset dropdown.
- Added **Custom** as the first preset option and reveals a custom preset name field only when selected.
- Filters presets by the selected Format and disables unavailable hardware presets with a reason tooltip.
- Applying a preset updates the export controls, profile snapshot, estimate and compressed output preview.
- Reordered preview tabs to **Output, Source, Compare**, with Output selected by default.

## Development Version 019 — Professional Encoding Model

- Format now selects the codec family and output container.
- Removed direct editable FFmpeg video encoder selection from Export Settings.
- Performance now exposes user-facing Software Encoding or compatible Hardware Encoding.
- Flux Encoder resolves NVENC, Quick Sync, AMF or VAAPI automatically from runtime capabilities.
- Added professional Profile, Level and Quality / Speed controls.
- Audio-only formats automatically disable video export.
- Export summary reports the selected format and performance mode instead of raw FFmpeg names.

## Development Version 018 — Professional Metadata and Resolution Controls

- Added a functional professional Metadata Export dialog with preservation rules, marker/chapter handling, templates, searchable metadata fields, and editable embedded values.
- Added metadata profile serialization and FFmpeg metadata/chapter mapping.
- Rebuilt Basic Video Settings resolution controls with Match Source, per-dimension overrides, and linked aspect-ratio behavior.

## v0.11.0 Development Version 017 — Bitrate Units

- Video target and maximum bitrate can be entered in either Mbps or kbps.
- Switching units preserves the exact bitrate value.
- Decimal Mbps values are supported down to 0.001 Mbps.
- Maximum bitrate is now serialized and passed to FFmpeg as maxrate/bufsize.

## v0.11.0 Development Version 016 — Professional Main Workspace and Maximizable Export Settings

- Reworked the main window into a tighter professional dock workspace with centered workspace selection.
- Added compact custom panel headers with panel menus, floating and close actions.
- Refined the default Media Browser / Preset Browser / Queue / Watch Folders / Encoding proportions.
- Enabled nested, tabbed and grouped dock movement while preserving saved workspaces.
- Added minimize, maximize and close controls plus a resize grip to Export Settings.

## v0.11.0 Development Version 015 — Professional Export Settings Workspace

- Rebuilt Export Settings around the professional wide-preview / narrow-inspector proportions.
- Added Source, Output and side-by-side Compare preview modes.
- Added functional source preview scaling and rotation controls.
- Added an professional timeline bar with current/duration timecodes, visible In/Out range, mark controls and source-range selector.
- Moved Format, Preset, Comments, Output Name, Export Video/Audio and Summary into the fixed right inspector header.
- Added a fixed inspector footer with render-quality option, estimated size, metadata action and OK/Cancel controls.
- Added dedicated professional styling for tabs, inspector, preview toolbar, timeline and action controls.

# Changelog

## v0.11.0 Development Version 013 — Compressed Output Preview

- Output Preview now renders a short segment with the currently selected video codec, rate-control, bitrate/quality, resolution, pixel format and video filters.
- The displayed Output frame is decoded from that compressed segment instead of mirroring the Source frame.
- Preview generation is asynchronous and debounced when export settings change.
- Scrubbing refreshes Source immediately and schedules a matching compressed Output preview.
- Encoder or container failures are shown directly in the Output Preview rather than silently falling back to an uncompressed frame.

## v0.11.0 Development Version 012 — Preview Range and Zoom

- Export Settings now shows the selected In/Out range directly on the timeline.
- The timeline playhead is initialized at the exact frame shown in the preview.
- Scrubbing and releasing the timeline refreshes both Source and Output preview frames.
- The Encoding panel timeline also shows the queued job In/Out range.
- Added a 10%–400% zoom slider and a Fit toggle to both Export Settings and Encoding previews.
- Preview zoom preserves aspect ratio and keeps the current frame centered.

## v0.11.0 Development Version 011 — Queue Selection Visibility Fix

- Queue row selection now uses a neutral graphite highlight instead of the same accent color as links.
- Format, Preset and Output File links use a dedicated high-contrast blue in normal, hovered and pressed states.
- Queue links remain readable and visually identifiable while their row is selected.


## 0.11.0 — Development Version 011

- Added Flux Encoder Render Provider SDK v1 with a versioned C ABI.
- Added provider manifest discovery and ABI/protocol compatibility checks.
- Added generic render-project source descriptors and queue serialization.
- Added a deliberately disabled Flux Motion provider placeholder manifest for future headless Flux Motion rendering.
- Added drag-and-drop presets from Preset Browser to Queue rows and multi-selection.
- Preset drops preserve output folder/base name and update only the required extension.
- Active jobs and unavailable hardware presets reject drops with a clear explanation.

## v0.10.0 — Development Version 010

- Added professional queue action strip and prominent Start Queue button.
- Added a guided empty queue state and clearer selection feedback.
- Preserved output folder and base filename when changing formats or presets.
- Added safer queue removal with confirmation and active-job protection.
- Added clearer renderer names and colored queue states.
- Improved automatic profile choice for audio-only sources.
- Added contextual tooltips and status-bar feedback.

## v0.9.0 — Development Version 009

- Implemented previously inert Export Settings controls: field order, pixel aspect, codec profile, audio sample rate/channels, loudness normalization, true-peak limiting, metadata, fast-start, timecode, caption burn-in and video limiter.
- Replaced unsupported publishing toggles with explicitly disabled provider-dependent options.
- Made Media Browser location navigation and double-click import functional.
- Added persistent, automatically scanned Watch Folders.
- Made the Renderer selector apply compatible software/hardware profiles to selected jobs.


## 0.9.0 — Development Version 009

- Converted Queue Format and Preset fields to professional split links.
- Clicking the linked label opens Export Settings; clicking the arrow opens a quick-select dropdown.
- Format changes select a compatible available preset and update the output extension.
- Preset dropdowns are filtered to the selected format and expose runtime availability.
- Converted Output File into a plain link without a dropdown; clicking it opens the save-file picker.
- Locked all three controls while a queue item is active.

## 0.7.0 — Development Version 007

- Replaced the queue Format and Preset combo boxes with professional link controls.
- Clicking either Format or Preset opens Export Settings for that exact queue item.
- Link controls become read-only while a job or the queue is active.
- Export Settings changes update the job profile snapshot and output extension safely.

## 0.6.0 — Development Version 006

- Rebuilt the default workspace around the professional panel structure: Media Browser and Preset Browser on the left, Queue/Watch Folders above Encoding on the right.
- Added real inline Format and Preset dropdowns to every mutable queue row.
- Format selection now filters the Preset dropdown by codec/output family instead of only by file extension.
- Active and completed queue items keep unsafe controls read-only.
- Expanded the built-in catalog with H.264, HEVC, AV1, VP9, Apple ProRes, Avid DNxHR, MPEG-2 broadcast, proxy, audio-only, NVENC, QSV, AMF, VAAPI and CUDA-assisted profiles.
- Added correct audio-only FFmpeg mapping so WAV, AAC, MP3 and FLAC presets do not attempt to mux a video stream.
- Preset changes update the output extension and stored profile snapshot immediately.

## 0.5.0 — Development Version 005

- Added professional queue context menu and batch actions.
- Added duplicate, retry/reset, move up/down, and batch output-folder commands.
- Queue ordering now persists and controls scheduler priority.
- Added source-preview scrubber and current/total timecode display.
- Active encode preview remains synchronized to FFmpeg progress and is read-only.
- Preserved explicit read-only behavior for detected and calculated fields.
## Development Version 014

- Moved the Effects tab immediately to the right of Audio in Export Settings.
- Added an professional hard-coded Timecode Overlay effect with start timecode, position, size, foreground opacity, and background opacity controls.
- Applied the overlay through FFmpeg drawtext so it is visible in compressed Output Preview and final exports.
- Added profile serialization and a contract test for the timecode effect.
