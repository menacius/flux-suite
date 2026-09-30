# 2026 - v0.8.20-alpha

- Added the Scene Masks dock with automatic Preview/Program population,
  joystick positioning and W–T zoom controls.
- Prevented Preview-only mask scenes from activating nested video playback;
  Program activation now follows the media source's configured behavior.
- Made dock hotkey persistence update-safe through OBS change notifications and
  an atomic plugin-config backup.
- Kept active cues intact while mask position and zoom are adjusted live.
- Added compact Preview/Monitor sections and adjustable Smooth Motion for
  joystick, zoom, Center and Reset operations.
- Added joystick/dial controls with optional numeric entry in source
  properties and synchronized their clamped values with the dock.
- Added optional cover-style bounds enforcement so every transformed scene
  fully covers its mask without empty space.
- Removed the redundant per-mask Crop when out of mask box setting.

# 2026 - v0.8.19-3

- Restored Broadcast Graphics Live-compatible temporal Motion Blur sampling
  density while keeping its sample budget bounded.
- Removed synchronous title-store loading and disk-cache indexing from OBS
  module startup.
- Preserved resident GPU frames across ordinary scene changes.
- Added bounded temporal Motion Blur for Adjustment Layers and optimized
  scaling-layer sampling.
- Kept active Live Text cues on air while their list data is edited.

# 2026 - v0.8.19-2-alpha

- Fixed project loading when `.fxmproj` files or their parent folders contain
  Greek or other non-ASCII characters on Windows.
- Fixed Unicode-path filesystem metadata and dependency-free WAV reads in the
  OBS audio runtime.
- Retained the existing project schema and title selection behavior.

# 2026 - v0.8.19-alpha

- Kept new OBS-created titles on the active OBS resolution and frame-rate
  defaults without showing the standalone New Title format dialog.
- Fixed first-frame Text rendering and prerender readiness when the GPU text
  shader is being initialized while caching is active.
- Made Play after rendering gate transport until the current title timeline is
  fully cached and reduced the Playback and Cache dock to Clear All Cache and
  Cache Entire Timeline actions without Live Text Cue diagnostics.
- Enabled caching/prerendering by default when the shared preference does not
  yet exist on first installation; an explicitly saved enabled/disabled choice
  remains unchanged on upgrade.
- Added the native Chart Layer runtime shared with Flux Motion, including seven
  chart types, existing-provider field mappings, multi-series data, animation,
  per-value/external-field colors and shared Flux gradient fills.
- Unified external data-source settings and field mapping with the standalone
  editor, and added OBS-dock exposure for selected Static Data values while
  preserving values that remain authored in the graphic.
- Made Static Data chart frames deterministic for prerender/precache and added
  chart values, colors and gradients to cache identity.
- Extended shared renderer diagnostics with requested and actual render-target
  dimensions and preview-quality state, while retaining full-quality OBS source
  and final/cache output behavior.
- Made reduced editor quality scale the actual 3D shadow targets, including
  point-light atlases, without changing authored full-quality shadow settings.

# 2026 - v0.8.18-alpha

- Migrated the compositor source of truth to Flux Suite Common Files; the OBS
  deliverable now contains host integration and adapter code only.
- Centralized Vector Motion Blur temporal sample policy with the standalone
  application and reduced avoidable compatibility sampling work.
- Preserved OBS real-time caps, GPU-resident transform sampling, correct
  camera/parent motion, frame-rate-derived shutters and normalized
  premultiplied exposure.

# 2026 - v0.8.17-alpha

- Moved logging controls from the editor into OBS plugin preferences.
- Added editor loading progress for new and opened titles.
