# Flux Motion Plugin for OBS

OBS Studio 33.0.1 is the current Windows compatibility target. Extension image
textures use Qt decoding and the stable OBS graphics API, avoiding the image-file
ABI removed in OBS 33. Build with the Qt version supplied by the OBS SDK.

Install the Windows package under `C:\ProgramData\obs-studio\plugins\flux-motion`:
`flux-motion.dll` and its runtime DLLs belong directly in that folder, next to
`data`. For portable OBS, use `<OBS root>\plugins\flux-motion` instead.
The former `bin\64bit` layout is legacy in OBS 33.
See [OBS plugin layout guidance](https://obsproject.com/kb/legacy-plugin-developer-info).

See [Cue automation](CUE-AUTOMATION.md) for source Cue/Uncue buttons,
Advanced Scene Switcher integration, automatic outro completion before scene
transitions and the public OBS procedure API.

This source root owns OBS-specific integration: module/frontend lifecycle,
sources, transitions, rendering, texture presentation, audio, hotkeys, and the
OBS implementations of Shared host interfaces.

The production `flux-motion-obs-plugin` target is configured by the
`apps/flux-motion` project and contains this integration plus
the operational `TitleDock`, its cue-preview Canvas, and Core/Shared runtime
code. It does not compile `TitleEditor`, properties panels, the editing
timeline, or the full Editor window. Double-clicking a title in the dock asks
the OBS host adapter to start the separately packaged Flux Motion executable.

Shared host-neutral contracts come from `packages/flux-common`. Override
`OBS_FXM_OBS_PLUGIN_ROOT` or `FLUX_SUITE_COMMON_DIR` at CMake configure time
when embedding the component in a non-standard checkout layout.

The completed dock/Editor ownership checkpoint is documented in
`apps/flux-motion/docs/ARCHITECTURE_MIGRATION_HISTORY.md`.
