# 2026 - v0.8.19-1-alpha

- Split ordinary 2D layer copying from the advanced lighting/shadow shader to
  prevent long D3D compilation stalls on affected Windows systems.
- Added the legacy Broadcast Graphics Live source identifier so existing OBS
  scene collections continue to load after upgrading.

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
