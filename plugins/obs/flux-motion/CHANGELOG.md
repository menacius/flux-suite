# 2026 - v0.8.21-alpha

See the [complete changelog since v0.8.20](https://github.com/menacius/flux-suite/blob/v0.8.21-alpha/docs/releases/v0.8.21-alpha.md)
for grouped release notes, installer changes and validation support.

- Restore loading on OBS Studio 33.0.1 by replacing removed image-file API
  imports with Qt image decoding and stable OBS texture uploads. Package Windows
  plugin binaries beside the data folder for the OBS 33 plugin manager.

- Share each Flux source's completed video frame between Studio Preview,
  Program and projectors, while keeping duplicate sources independent.
  Retain per-view backdrop compositing and live inputs on held titles.
- Keep all scene-mask mattes resident together and preflight fallback paths
  before capturing scenes that would be discarded.

- Place the native Cue/Uncue properties buttons side by side with the
  Settings buttons label on the left, retaining their automation callbacks.

- Add an opt-in Uncue before scene transition toggle to every Flux source.
  The global preference now acts as a master switch for opted-in sources.

- Double-clicking a live cue word now grants editing focus before selecting
  the word; typing replaces it immediately and normal commit behavior applies.
  Retain hotkey and externally managed read-only field protection.

- Restrict Pending to the edited live text cue row already on Program; other
  rows/titles and uncued edits do not trigger the indicator. Compare the held
  and authored graph with the same stable cue row applied.
- Use the literal Pending rail label with older installed locale files.

- Fix uncue-before-transition for titles in nested scenes shared by both
  Program scenes; observe newly assigned private output transitions.
- Keep GPU presentations and cache identities resident while changing
  scene-mask position/zoom instead of rebuilding the complete title graph.
- Alternate pending cue indicators red/yellow, retaining a constant Pending
  label in the collapsed Titles and Graphics rail.

- Reuse the Titles and Graphics caret painting for all scene-mask panels.
- Add a persistent, opt-in uncue-before-transition preference, waiting for
  outgoing outros, including shared titles, and retaining the native transition.
- Expose source-local Cue/Uncue properties buttons for Advanced Scene Switcher
  and a public OBS procedure API for native plugin integrations.

- Added persistent right-side collapse carets for Preview/Monitor sections and
  individual scene-mask control cards.
- Persisted dock layout after floating moves/resizes and tab/split changes;
  prevented startup restoration and dock teardown from overwriting saved state.
- Restored the Titles and Graphics collapsed state across OBS sessions and
  added a synchronized toggle to the dock Settings menu.
- Preserved cue transport across title-library reloads and kept unchanged
  scene-mask media references alive while titles are edited.
- Hold edited Program titles until the next uncue/cue and blink the cue
  indicator red once per second while authored changes are pending.
- Use one GPU session for a shared Preview/Program source; separate sessions
  follow OBS's native Duplicate Sources behavior.
- Show off-air scene masks at pause/cue-end, start their animation on Program
  activation, and mirror Program in Preview when the same source is on air.
- Cache animation analysis, avoid audio transport work for silent/hidden
  sources, and retain the GPU graph on unrelated title-store revisions.
- Defer hidden-source GPU preparation until the source is actually shown.
  This fixes the repeated first-frame work reproduced with CECL Card while
  the same source rendered normally when visible in Preview or Program.
- Skip destination capture/compositing for hidden, out-of-range or fully
  transparent adjustment/blend layers, including on held uncued frames.

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
