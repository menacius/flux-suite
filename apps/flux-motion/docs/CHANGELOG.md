# 2026 - v0.8.19-2-alpha

## Development Version 420 — Unicode-safe Windows file access

- Fixed `.fxmproj`, `.fxmt`, JSON template and embedded-asset reads from
  Windows paths containing Greek or other non-ASCII characters.
- Standardized direct standard-library filesystem access on explicit UTF-8
  path conversion, including existence checks, binary reads and project reads.
- Preserved the existing project and title schemas; files require no migration.
- Added regression coverage and verified the original `ToNima.fxmproj` directly
  from its Greek parent directories.

# 2026 - v0.8.19-alpha

## Development Version 419 — native charts and end-to-end playback performance

### Welcome and first-run defaults

- Added the Flux Motion application logo to the Welcome screen branding.
- Changed the standalone Welcome screen from a full central stacked page into
  a centered popup card owned by the main application window. The dimmed
  workspace remains visible behind it, the card stays within and recenters with
  the main window, and opening the Editor hides the overlay without creating a
  second native application window.
- Restyled the Welcome popup with the Flux Installer visual language: layered
  dark surfaces, bordered rounded cards, purple accent/selection states,
  compact typography and a soft elevation shadow, while retaining Flux Motion
  project creation, browsing, recent-project search and thumbnails.
- Enabled caching/prerendering by default when no preference exists on first
  installation. Existing installations that have explicitly enabled or
  disabled caching keep their saved choice unchanged.

### Standalone title setup, canvas reliability, and cache playback

- Added a standalone New Title dialog for length, resolution, and frame rate.
  Titles created through the OBS plugin continue to inherit the active OBS
  canvas and frame rate without showing this dialog.
- Fixed initial Text layer presentation with caching enabled by retrying the
  canvas after asynchronous GPU shader readiness and compiling the text shader
  synchronously on the background prerender path.
- Made **Play after rendering** wait for the complete current title/graphic
  timeline to be prerendered before Play or Space starts transport.
- Added post-load layout settling and explicit GPU destination-rectangle refresh
  so opening, creating, and recent-project loading no longer leaves a blank
  offset above or to the left of the canvas.
- Simplified Playback and Cache actions to **Clear All Cache** and **Cache Entire
  Timeline**, removing the RAM/disk split clears, pause/resume, work-area cache,
  and Live Text Cue diagnostics from the dock.

### Chart Layer

- Added Chart as a first-class native layer with Bar, Horizontal Bar, Line,
  Area, Pie, Donut and Scatter charts, shared rendering between Flux Motion
  and the OBS plugin, normal transforms/parenting/effects/masks/blending,
  serialization and existing timeline/keyframe integration.
- Connected charts to the existing Flux data-provider architecture instead of
  introducing a parallel data system. Category, value, series, optional
  secondary values, metadata and optional color fields use the established
  provider/binding workflow and support multiple independently styled series.
- Reworked Chart properties into progressive, context-sensitive Chart, Data,
  Axes, Labels, Legend, Appearance and Animation sections using the existing
  Flux Motion collapsible panels, controls, icons, color assets, data-source
  dialog, undo/redo and theme infrastructure.
- Unified external-data configuration and field mapping between the standalone
  editor and the OBS dock, including the same provider settings and discovery
  behavior in both hosts.
- Renamed the internal/manual mode to **Static Data** and made static chart
  values optionally exposable in the OBS dock, so a value can remain authored
  in the graphic or be controlled live without changing data models.
- Added per-value colors for Static Data and an optional external color-field
  mapping with `#RRGGBB`, `#AARRGGBB` and integer color decoding.
- Added chart fills backed by the existing Flux gradient framework, including
  shared presets, multi-stop editing, linear/radial/conical modes and spread
  behavior; no chart-specific color or gradient editor was introduced.
- Made Static Data resolution deterministic during prerender/precache and added
  chart data, color and gradient state to cache identities, preventing
  wall-clock transitions from invalidating or changing cached animation frames.

### Playback and Adaptive Resolution

- Audited the complete editor playback path and corrected the misleading
  diagnostic that measured only CPU session preparation while the expensive GPU
  compositor and presentation happened later in the swapchain callback.
- Added persistent Performance/RenderDiagnostics instrumentation for requested
  and actual render-target dimensions, preview scale, CPU preparation, graphics
  context wait, render/submission, Qt preview upload, present/VSync wait, normal
  readback state, total frame processing and successfully presented FPS.
- Split the status display into meaningful **Render** and **Total frame** times,
  exposes the actual internal preview resolution, and provides the complete
  stage breakdown in its tooltip.
- Replaced the Auto quality decision based on preparation time with a
  hysteretic presented-frame governor. It steps through 100%, 75%, 50%, 37.5%
  and 25% when sustained cadence misses occur, avoids per-frame reallocations,
  and restores quality after sustained non-VSync workload headroom.
- Applied the reduced physical target to destination-aware blend/effect
  compositions as well as ordinary 2D/3D layers; their background snapshot is
  remapped into the same session target instead of disabling Adaptive quality.
- Propagated adaptive quality into actual 3D shadow-map targets. Lower quality
  modes now progressively reduce planar and point-light atlas work instead of
  sharing the same fixed draft size, while full-quality OBS/final/cache output
  remains unchanged.
- Removed a resolution-independent Qt bottleneck by retaining the editor helper
  overlay during transport playback instead of repainting and uploading a
  full-widget QImage for every animation tick; scrubbing, editing and playback
  stop still refresh the overlay at the correct playhead.
- Kept normal editor presentation GPU-resident and confirmed it performs no
  GPU-to-CPU readback; explicit inspection/export and prerender readback paths
  retain their existing asynchronous/staged contracts.

# 2026 - v0.8.18-alpha

## Development Version 418 — shared renderer and Vector Motion Blur performance

- Moved the canonical title compositor into Flux Suite Common Files. Flux
  Motion preview/final output and Flux Motion Plugin for OBS now compile the
  same rendering-engine source; OBS retains host adapters only.
- Centralized shutter sample planning and quality caps in the shared renderer.
- Audited Vector Motion Blur and confirmed the dominant cost was temporal draw
  multiplication: changing content rerendered complete layer/effect pipelines,
  while transform-only motion issued one full-canvas accumulation draw per
  sample. Image compatibility sampling also used 2.25 samples per pixel.
- Preserved the resident-texture transform fast path and bounded full-pipeline
  path, reduced compatibility image/vector density to 1.5/0.8 samples per pixel, retained
  midpoint shutter sampling, and kept authored samples as the minimum quality
  target within the output-specific safety cap.
- Preserved direction, centered/trailing shutter behavior, transform/parent/
  camera evaluation, frame-rate-derived exposure, premultiplied normalization,
  persistence bypass, and current-frame dry/wet resolution.

# 2026 - v0.8.17-alpha

- Moved OBS plugin logging controls out of the editor and into the plugin preferences.
- Added editor loading progress for new and opened titles.

# 2026 - 0.8.16-alpha

## Development Version 417 — multi-title projects and product naming

- Added the versioned `.fxmproj` project container with ordered independent
  titles/graphics, stable IDs, active composition metadata, atomic saves, and
  legacy single-title/packed-title/scene-array readers.
- Added the Project panel and staged cancellable project loading workflow.
- Extended the existing Asset Layer as a project-local Title/Graphic Layer and
  added cycle-safe insertion from the Project panel.
- Added OBS Project File and Title source properties with stable selection,
  timestamp reload, missing-title fallback, and the legacy empty-path workflow.
- Renamed the deliverables to Flux Motion and Flux Motion Plugin for OBS.

## Flux Suite synchronization

- Separates Flux Motion and Flux Motion Plugin for OBS preferences and distributions.
- Synchronizes Flux Motion, Flux Encoder and renderer package versions.

## Development Version 415 — Flux Encoder path override

- Added a temporary `FLUX_ENCODER_EXECUTABLE` path option with a file browser
  to the Editor Preferences page. The saved override takes priority over the
  environment variable and installer discovery, can be cleared to restore
  automatic detection, and refreshes Export as Media availability immediately.

## Development Version 414 — Flux Encoder media export

- Added File → Export as Media… as a Flux Encoder handoff. The action detects
  the Encoder at startup and whenever the File menu opens, stays visibly
  disabled when Encoder is unavailable, saves pending project changes, reuses
  an existing Encoder instance, and opens its export window through versioned
  local JSON IPC with explicit failure reporting.
- Kept all format, codec, destination, preset, queue, image-sequence and render
  settings out of Flux Motion. The extensible suite IPC contract is documented
  in `FLUX-ENCODER-IPC.md`.

## Development Version 413 — timeline chrome and keyframe indicators

- Removes the unused stretch before the Premiere-style timeline navigator and lets the controller consume the available horizontal space immediately after visible graph controls.
- Replaces the Satoshi/fallback-dependent Unicode keyframe diamonds in layer property rows with custom-painted 10.5 px geometric markers, using a filled state at the current keyframe and a clear outlined state otherwise.
- Keeps marker hover, locked/disabled state, click target and layer-aware color behavior independent of the active standalone or OBS font.
- Advances build and automated-suite metadata to Development Version 413.

## Development Version 412 — editor audio meter and timeline navigator

- Restores standalone editor audio metering by polling the runtime's linear peak levels when native OBS dB callbacks are unavailable, converting those peaks to the meter's dB scale and retaining the existing ballistics.
- Replaces the timeline's zoom-out/slider/zoom-in controller with a Premiere-style horizontal navigator whose body pans and whose end grips resize the visible range to zoom; wheel zoom and double-click fit are also supported.
- Synchronizes the navigator after playhead auto-scroll, Shift-wheel pan, Ctrl-wheel zoom, resize, Graph Editor pan and graph fitting.
- Reduces standalone tab height modestly and both scrollbar axes to seven pixels while leaving the OBS dock on its host-provided style and font.
- Advances build and automated-suite metadata to Development Version 412.

## Development Version 411 — Flux Motion visual identity

- Consolidates the standalone appearance into one Flux Motion theme, migrates legacy theme preferences, and uses `#7838f5` as its accent.
- Packages and registers the complete static Satoshi UI family for the Flux Motion Editor; the OBS dock deliberately inherits the host palette, style and font.
- Preserves the supplied SVG's original Satoshi typography and optical tracking while adding live public/development version and copyright text to the shared About/splash artwork.
- Moves startup progress into the artwork's unused upper area so it does not overlap the new metadata lines.
- Advances build, package and automated-suite metadata to Development Version 411.

## Development Version 410 — About artwork splash screen

- Uses the version-aware Flux Motion About artwork as the standalone Editor startup splash while initialization runs.

## Development Version 409 — unified GPU and resolution/time independence

- Replaces the standalone software compatibility renderer with the same libobs/D3D11 title compositor used by OBS live output.
- Adds an explicit render request carrying physical target extent and temporal sample duration; authored transforms remain in logical title space.
- Makes target scaling apply consistently to 2D transforms, projected 3D quads, GPU text/vector raster density and pixel-distance effects.
- Removes OBS-global frame-rate reads from the compositor and scopes motion blur, animated raster caches and video requests to the consumer's declared cadence.
- Maps video frames from continuous title/media time and media FPS, eliminating project-FPS-dependent duplicate/drop identities for equal timestamps.
- Stages libobs runtime dependencies and shader data for the Editor, carries the OBS bin directory into detached launches, and adds a real two-resolution GPU render smoke test.
- Advances runtime, build, package and automated-suite metadata to Development Version 409.

## Development Version 408 — preview performance and title format

- Adds persisted, per-title Resolution and Frame Rate controls to Title Properties.
- Applies the authored frame rate to standalone timecode, frame stepping, playback scheduling and video-frame sampling.
- Restores measured playback FPS in the standalone footer by counting QImage canvas presentations as well as OBS swap-chain presents.
- Makes Auto preview quality active during playback and renders reduced-quality frames at the reduced surface size before lighting/composition instead of downscaling a completed full-resolution frame.
- Keeps the OBS live-output renderer GPU-resident; the separated standalone editor remains on its software compatibility renderer while avoiding its largest redundant full-resolution cost.
- Advances runtime, build, package and automated-suite metadata to Development Version 408.

## Development Version 407 — world-space Z and standalone media preview

- Makes Automatic depth a scene-wide camera/world-space contract rather than a sequence of layer-list runs; layer order remains only the stable equal-depth tie-break.
- Restores standalone Video decoding through the shared asynchronous FFmpeg `FrameRuntime`, including refresh when a decoded frame arrives after a seek.
- Implements a standalone Qt Multimedia audio-preview session for Audio layers and linked Video audio streams with timeline seeking, trim/loop handling, playback speed, mute/solo and monitor routing.
- Adds a persisted Preview Audio output-device selector to Preferences > Editor and restarts only the editor preview session when its device changes.
- Stages the Qt Multimedia DLL and platform media plugin beside both development Editor layouts.
- Advances runtime, build, package and automated-suite metadata to Development Version 407.

## Development Version 406 — editor Z and render performance

- Removes unified XYZ, free-transform, camera and depth state from the transform-neutral layer-raster fingerprint, preventing Z-only editor manipulation from regenerating text/vector/effect content.
- Retains static standalone frames and content-addressed layer rasters across transport ticks and matrix-only model revisions.
- Replaces full per-pixel point/spot material evaluation with a smooth bounded lighting lattice while preserving per-pixel authored color and alpha.
- Keeps automatic Z sorting continuous across non-rendering Light, Audio and Empty rows.
- Advances runtime, build, package and automated-suite metadata to Development Version 406.

## Development Version 405 — Flux Motion identity and OBS-native dock

- Completes the Broadcast Graphics Live to Flux Motion rebrand without altering the supplied brand artwork.
- Uses the supplied Satoshi typeface for the dynamically synchronized About version.
- Renames title/graphic files to `.fxmt`, packed files to `.fxmp`, and related auxiliary formats to the Flux Motion extension family.
- Uses OBS-native action icons wherever the host exposes them, while retaining Flux Motion icons as safe fallbacks.
- Makes dock toolbar icons and compact action controls derive their dimensions from the active OBS/Qt style metrics.
- Makes the standalone Editor canvas render artwork with the same layer-local box, origin, parent hierarchy and animated transform used by its authoring overlays.
- Restores standalone canvas 3D projection, camera transforms, free-transform quads and backface culling through the shared Core transform implementation.
- Restores vector primitive and Pen-path geometry in the standalone canvas instead of painting every shape as a rectangle.
- Makes standalone canvas text consume the canonical rich-text document and shared immutable layout, restoring paragraph formatting, mixed character styles, scaling, decorations, gradients and strokes.
- Adds a standalone software preview for opt-in 3D material lights and light-authored soft shadows while leaving the OBS GPU lighting pipeline unchanged.
- Makes standalone 3D planes honor shared camera-depth ordering and material shading, including spatial point/spot falloff and specular response.
- Restores inline text-selection highlighting on the standalone QImage canvas instead of limiting the selection mask to the OBS swap chain.
- Runs the standalone layer effect stack on bounded intermediate rasters, including adjustment layers and supported blend modes, while retaining OBS shaders as the exact live-output implementation.
- Advances runtime, build, dependency, package, and audit metadata to `v0.8.15-alpha`, Development Version 405.

# v0.8.14-alpha — Development Version 404

## Development Version 404 — OBS 32.2.1 compatibility and runtime stability

- Rebuilds against the OBS Studio 32.2.1 and matching Qt ABI so the native plugin loads safely after the host update.
- Makes the Windows build helper resolve Qt 6.11.1 from the selected OBS SDK, pass its package directory explicitly, and reset a generated build tree when cached OBS/Qt ABI paths differ.
- Removes icons from editor tabs and makes lower-left and lower-right areas available as explicit dock targets.
- Moves the current public/development version to an always-visible header in the main plugin dock while retaining it in editor captions and the About dialog.
- Corrects cue/uncue cache-state gating so requested live state is published consistently.
- Removes the title-store lifetime mutex cycle that could block source activation, editor opening, and OBS shutdown.
- Makes real-time title presentation use non-blocking model acquisition during transient editor ownership.
- Creates the editor swap chain before render work and keeps large text blur on the bounded analytic SDF path, avoiding startup shader-compile stalls.
- Marks only the cached GPU overlay dirty when the canvas pointer changes, so both rulers track the cursor in real time without rerendering artwork.
- Consolidates current documentation into the thematic guides and advances runtime, build, dependency, package, and audit metadata to `v0.8.14-alpha`, Development Version 404.

# v0.8.13-alpha — Development Version 403

## Development Version 403 — background-aware threshold and waveform polarity

- Applies the composited-row foreground choice to layer numbers and the 2D/3D control.
- Raises the dark-foreground luminance threshold to `0.42`, preserving light UI content on medium-bright custom colors.
- Keeps blend-mode dropdown entries text-only.
- Uses the opposite semantic foreground polarity for audio and linked-video waveforms.
- Advances runtime/build/package metadata to Development Version 403.

# v0.8.13-alpha — Development Version 402

## Development Version 402 — MSVC clamp type fix

- Fixes the ambiguous `std::clamp` call in `title-assets.h` by explicitly normalizing `QColor::alphaF()` to `double`.
- Restores MSVC compilation for every translation unit including the shared header.
- Retains all Development Version 401 background-aware layer-list and timeline behavior.

# v0.8.13-alpha — Development Version 401

## Development Version 401 — background-aware layer rows and timeline strips

- Selects dark or light foregrounds from the active OBS palette using WCAG-style relative luminance and contrast against the actual composited background.
- Recolors layer-list text and row icons dynamically for both unselected translucent rows and opaque selected rows.
- Adds cached, background-aware layer-type icons to timeline strips and applies the same contrast model to waveforms, fades, locked-strip hatching and transition labels.
- Preserves native OBS popup surfaces and updates immediately after OBS palette/style changes.
- Advances runtime/build/package metadata to Development Version 401.

## Development Version 400 — themed layer-type icons and complete appearance colors

- Installs the supplied Text, Clock, Ticker, Shape, Image, Video, Audio, Empty, Adjustment, Color Solid, Camera and Light artwork as dedicated `currentColor` SVG assets.
- Uses the same icon mapping in the Add Layer menu, Light submenu, layer-list type cell and Camera/Light timeline owner rows.
- Rebuilds icon pixmaps when the OBS application palette/style changes and refreshes menu icons whenever the Add Layer menu opens.
- Adds persisted Preferences > Appearance colors for Video, Audio, Empty, Adjustment, Color Solid, Camera and Light rows, with distinct defaults and explicit layer-type mapping.
- Retains the Development Version 399 close Point-light text-shadow subdivision and all prior rendering behavior.
- Advances runtime/build/package metadata and the source-test manifest to Development Version 400.

# v0.8.13-alpha — Development Version 399

## Development Version 399 — close Point-light text-shadow projection continuity

- Subdivides close or high-angular-extent Text, Clock and Ticker casters into a reusable 32×32 UV grid for Point-light shadow-map rendering.
- Prevents a wide text plane from being projected as only two triangles across several cube faces, eliminating diagonal/triangular shadow gaps below roughly 300 px of light-to-caster plane distance.
- Leaves distant text, Shape/Image/Video casters, Spot/Parallel maps and normal receiver filtering unchanged, so the additional vertex work is restricted to the failing close-range path.
- Stores one grid per GPU render session and releases it during session destruction instead of allocating geometry per frame.
- Advances runtime/build/package metadata, GPU cache identities and the test-suite manifest to Development Version 399.

# v0.8.13-alpha — Development Version 398

## Development Version 398 — explicit planar shadows and continuous text coverage

- Replaces the Spot/Parallel shadow writer and receiver matrix path with one explicit light-space basis and linear depth projection shared by both stages.
- Removes backend-dependent matrix-layout and OpenGL/D3D clip-space assumptions that could publish a nominally valid but visually empty planar shadow map.
- Uses projection centre/span values for fitted Parallel maps and the authored cone span for Spot maps, with exact matching UV/depth reconstruction on receiving layers.
- Expands Text, Clock and Ticker conservative alpha sampling to a 4×4 projected footprint with half-texel dilation and a lower text-only cutoff, preventing interrupted glyph shadows after minification.
- Retains Point-light cube-atlas projection, ordinary non-text caster cost, the `0.1 px` Source Size floor and all Development Version 396 performance changes.
- Advances runtime/build/package metadata, GPU cache identities and the test-suite manifest to Development Version 398.

# v0.8.13-alpha — Development Version 397

## Development Version 397 — continuous text shadows and Source Size floor

- Adds conservative projected-footprint alpha sampling to the shadow-map writer for Text, Clock and Ticker cutout casters, preventing thin glyph strokes from disappearing on alternating shadow-map rows during minification.
- Leaves solid Shape, Image and Video casters on the existing single-alpha-sample path, limiting the additional texture work to text-like casters.
- Enforces `0.1 px` as the minimum light Source Size in both editor property-panel implementations, JSON deserialization/keyframes, runtime light evaluation and editor light overlays.
- Advances runtime/build/package metadata and the test-suite manifest to Development Version 397.
- Retains the Development Version 396 static effect-cache preflight and conservative no-op elision behavior unchanged.

# v0.8.13-alpha — Development Version 396

## Development Version 396 — effects performance regression audit

- Checks the resident static effect-output cache before resolving per-effect animation, acquiring effect-registry locks or performing shader lookup/queue checks.
- Eliminates GPU passes for conservative identity states such as zero-radius blur, zero-opacity shadow/glow/overlay, identity brightness/contrast and saturation, and zero-strength distortion/detail effects.
- Protects custom extensions from built-in no-op inference unless their extension ID maps back to a registered built-in effect.
- Adds `effect_output_cache_hits`, `effect_output_cache_misses` and `effect_noop_passes_skipped` debug counters.
- Keeps Motion Blur temporal sampling, transition blur composition, effect execution spaces and stack order unchanged.
- Advances runtime/build/package metadata to Development Version 396 and adds a dedicated regression contract.

# v0.8.13-alpha — Development Version 395

## Development Version 395 — performance audit and hot-path reduction

- Caches logging preferences/category state and writes through a persistent 64 KiB/250 ms buffer, removing repeated `QSettings` reads and per-line file open/close operations.
- Makes every logging macro lazy, so disabled categories no longer build large `QString::arg()` diagnostic payloads.
- Disables SourceTiming, SourcePresentation and RenderDiagnostics by default and avoids GPU-session diagnostic mutex snapshots unless those categories are explicitly enabled.
- Stops classifying routine `QEvent::LayoutRequest` events as dock-layout transitions, preventing per-frame transport/UI changes from stopping and restarting editor presentation pacing.
- Uses half-dimension cube faces for Point-light 3x2 atlases, reducing atlas pixel area and texture memory by 4x while leaving Spot/Parallel planar resolution unchanged.
- Advances runtime/build/package metadata to Development Version 395 and adds a dedicated performance-audit contract.

## Development Version 394 — v0.8.13-alpha release and consolidated README

- Promotes all current runtime, build, dependency and package metadata from `v0.8.12-alpha` to `v0.8.13-alpha` and advances the delivery identifier to Development Version 394.
- Rebuilds the top-level README around the complete feature set added since Development Version 281, including Trim Paths, the Motion Blur temporal pipeline, 3D lights/materials/shadows, extrusion and camera workflows, packed title files, rich document import, cue Preview and editor/UI improvements.
- Keeps historical changelog version labels unchanged and retains the complete Development Version 393 rendering behavior, including the Motion Blur/transition deadlock fix and Development Version 392 crash containment.

## Development Version 393

- Fixes the repeated `resource deadlock would occur` render failure triggered by Motion Blur combined with active text transitions.
- Removes the recursive acquisition of the non-recursive `TitleGpuRenderSession::mutex` from synchronous effect-registry lookups; the compositor already owns that mutex, while `TitleEffectRegistry` independently serializes `find()` and `compile()` through its own recursive mutex.
- Keeps effect shader compilation asynchronous and worker-safe without blanking or indefinitely deferring the editor/OBS frame.
- Extends temporal sample exception containment around the complete recursive sample render, so any future per-sample failure falls back to the authored current transition frame rather than invalidating the entire compositor frame.
- Retains the Development Version 392 Spot/Parallel shadow clip correction and compositor no-throw boundary.
- Bumps runtime/build metadata to Development Version 393.

## Development Version 392

- Fixes Spot and Parallel shadow casting on D3D11 by converting the Qt/OpenGL planar shadow projection from -W..W clip Z to D3D's 0..W clip range before rasterization; Point-light atlas projection remains unchanged.
- Falls back from failed full-pipeline Motion Blur samples to the authored current transition frame, preserving text transitions instead of dropping the layer or propagating an exception.
- Adds a compositor-level exception boundary that retains the last stable published texture, marks the frame for retry and records the exception without terminating OBS/Qt.
- Leaves the 4096 px shadowmap maximum and 2048 px default introduced by Development Version 391 unchanged.
- Bumps runtime/build metadata to Development Version 392.

## Development Version 391

- Removes the 8192 px shadowmap size option from Preferences > Advanced and clamps the persisted runtime preference back to the supported 256–4096 px set.
- Keeps the shadowmap default at 2048 px and continues persisting user-selected shadowmap size through the existing preferences store.
- Fixes Spot and Parallel light shadows by explicitly setting a full-size planar shadow-map viewport before drawing casters; Point-light atlas viewport handling is unchanged.
- Bumps runtime/build metadata to Development Version 391.

## Development Version 390

- Adds 8192 px shadowmap support and changes the default Advanced shadowmap size to 2048 px.
- Keeps the selected shadowmap size persistent through the existing preferences store and applies the 8192 px upper bound in the shadow renderer.
- Uses high-precision floating-point Motion Blur accumulation/coverage targets where available, with BGRA fallback, to remove visible 8-bit banding on moving shapes, images and video.
- Keeps GPU primitive raster entries valid while their first-use shader compile is pending and prevents empty same-key entries from being skipped after a title opens in the editor.
- Bumps runtime/build metadata to Development Version 390.

## Development Version 389

- Replaces the shader-compile canvas overlay with a full canvas suppression screen that only shows `Compiling Shaders N of M` centered until compilation completes.
- Adds Preferences > Advanced shadowmap size control and routes the selected resolution into the 3D shadow-map render pass/cache identity.
- Removes authored Falloff Start from the visible light controls; runtime falloff start now derives from Source Size.
- Updates new light defaults to Source Size 15 px and Falloff Distance 450 px.
- Bumps runtime/build metadata to Development Version 389.

## Development Version 388

- Moves first-use GPU shader/effect compilation out of the synchronous render/present path and into a session-local worker that compiles one queued shader at a time under the OBS graphics context.
- Presents the previous stable texture while compilation is pending, avoiding full OBS/editor stalls during first-use shader creation.
- Adds a canvas progress overlay showing the active shader label and aggregate compile progress.
- Covers core compositor shaders, effect registry shaders, GPU text, masks, adjustment layers, layer-copy/blend, temporal motion-blur composite and shadow-map shaders.
- Bumps runtime/build metadata to Development Version 388.

## Development Version 387

- Fixes the Dev386 first-editor-open freeze. Runtime diagnostics show the
  first frame stopping immediately after `depth-run-summary` on a zero-light
  title, isolating the stall to first-use compilation of the expanded common
  GPU layer effect.
- Removes manual four-neighbour bilinear comparison from every Point-shadow
  tap and restores one depth lookup per Poisson sample.
- Preserves receiver-matched texel-centre depth, the self-shadow acne fix and
  real-time shadow-map updates during direct manipulation.
- Invalidates the affected GPU effect and persistent raster namespaces and
  retains the complete Development Version 386 delivery otherwise.

## Development Version 386

- Rebuilds all active GPU shadow maps during transform-only editor updates,
  making shadows follow 2D/3D object and light drags in real time while layer
  rasters remain resident.
- Fixes Point-light self-shadow acne when the same planar surface accepts and
  casts shadows. Receiver-plane depth is now evaluated on the exact quantized
  texel-centre cube ray used by the writer.
- Adds manual tile-clamped bilinear PCF for final Point-shadow comparisons,
  blending the four neighbouring visibility tests without crossing into an
  adjacent 3 x 2 atlas face.
- Invalidates the affected GPU effect and persistent raster namespaces and
  retains the complete Development Version 385 delivery.

## Development Version 385

- Fixes Point-shadow caster fragments being clipped on D3D11 even though the
  shadow target and draw loop were reported as successful.
- Replaces the mixed Qt projection-Z/manual cube-face-W writer position with
  clip depth derived from the same signed cube-face depth used for atlas XY/W.
  Selected-face vertices now remain in D3D's `0 <= z <= w` range and Z24 depth
  remains monotonic from the light.
- Renames the map diagnostic result to `submitted` and adds receiver-binding
  diagnostics covering material opt-in, map state and comparison values.
- Invalidates the affected GPU effect and persistent raster namespaces and
  retains the complete Development Version 384 delivery.

## Development Version 384

- Restores visible Point, Spot and Parallel shadows for punctual lights. When
  `Source Size` is zero, the receiver now uses a direct 16-tap Poisson PCF
  resolve and cannot be classified as fully lit by a sparse PCSS blocker
  search.
- Keeps contact-hardening PCSS for finite emitters while separating manual
  Shadow Softness from blocker discovery, anchoring the umbra to the exact
  centre ray and treating out-of-map planar samples as clear far depth.
- Retries shadow targets in floating `RGBA16F` when a graphics backend cannot
  begin the preferred `R32F + Z24/S8` surface.
- Adds one-shot shadow diagnostics for target creation/begin, caster and
  receiver discovery, shader setup and successful map publication.
- Invalidates the affected GPU effect and persistent raster namespaces and
  retains the complete Development Version 383 delivery.

## Development Version 383

- Replaces the regressed Point/Spot/Parallel PCSS receiver with a stable
  multiscale Poisson filter. An 8-tap compact contact kernel preserves the
  umbra and a 16-tap outer kernel is introduced progressively from physical
  caster/receiver separation.
- Removes the Point blocker-search dependency on the shadow near plane. Search
  radius now follows the emitter's apparent size at the receiver and is safely
  bounded, preventing valid blockers from being scattered across unrelated
  cube faces and diluted into isolated artifacts.
- Uses one deterministic Poisson rotation per light rather than a different
  world-space hash at every pixel, eliminating the grain/crawling artifacts
  reported in Development Version 382.
- Keeps Source Size as the physical penumbra driver and Shadow Softness as the
  artistic addition, while retaining the proven six-face Point writer,
  receiver-plane self-shadow correction and real-time overlay scheduling.
- Invalidates the GPU effect and persistent raster namespaces and retains the
  complete Development Version 382 delivery.

## Development Version 382

- Restores Point-light shadows to the runtime-proven six-face 3 x 2 cube
  atlas after the focused projection introduced in Development Version 381
  could publish empty shadow maps on real scenes.
- Retains contact-hardening PCSS, rotated Poisson filtering, alpha-aware
  casters and the per-layer 3D Casts/Accepts Shadows controls, with the
  responsive 256 draft / 512 final per-face Point-map budget.
- Keeps the last complete shadow maps during direct manipulation and rebuilds
  them on the release/settle update, so canvas overlays remain real-time while
  a layer or light is dragged.
- Fixes the D3D11 `Invalid texture vertex size specified` runtime failure by
  packing the two scalar GPU-text vertex attributes into supported vec2
  streams. This prevents the immediate CPU glyph fallback seen in the supplied
  log and removes its editor-frame stalls.
- Retains the complete Development Version 381 delivery except for the
  regressed focused Point projection and its oversized map budget.

## Development Version 381

- Replaces fixed-radius 5 x 5 grid PCF with PCSS contact-hardening shadows for
  Point, Spot and Parallel lights: a 12-tap blocker search estimates the
  occluder depth, followed by a 16-tap adaptive filter only inside shadows.
- Derives physical penumbra from Source Size and blocker/receiver separation,
  so contact regions remain sharp and the shadow broadens with real spatial
  distance. Authored Shadow Softness remains an additive artistic control.
- Uses a stable rotated Poisson disk instead of a square grid, removing blocky
  bands without frame-to-frame crawling.
- Fits ordinary Point-light scenes into one focused perspective shadow map.
  This concentrates the full map resolution on the actual visual layers and
  reduces the caster pass from six draws to one; a cube atlas remains as the
  complete fallback when geometry surrounds the light.
- Raises Point shadow-map detail to 512 per draft face / 1024 per final face,
  while PCSS early-out reduces receiver sampling from 25 to 12 in unshadowed
  areas (28 only where a blocker exists).
- Applies the same depth-aware PCSS path to Spot and Parallel shadows, retains
  alpha-aware casting and receiver-plane self-shadow correction, and
  invalidates all affected GPU caches.

## Development Version 380

- Fixes Point shadows disappearing when the light is far from the objects.
- Computes the closest light-to-surface distance against both triangles of
  every finite 3D visual quad instead of relying on corner distances.
- Replaces the fixed `0.01` cube near plane with a conservative distance-aware
  bound, preventing remote casters and receivers from collapsing into the same
  Z24 values.
- Makes Point shadow bias distance-invariant by expressing it over the scene's
  occupied radial-depth span before normalizing by the far plane.
- Retains all Development Version 379 Point atlas projection fixes.

## Development Version 379

- Implements a complete Point-light cube projection in the shadow writer and
  uses the same projection body verbatim in receiver atlas lookup.
- Replaces the separate 24-matrix receiver mapping that could disagree with
  the writer after Qt-to-libobs shader upload, producing repeated diagonal
  streaks and large black cube-face regions.
- Uses signed per-face depth as clip `w`, so casters behind a face are clipped
  instead of mirrored into that face.
- Keeps R32F radial depth, receiver-plane PCF, alpha-aware silhouettes and all
  opt-in 3D cast/accept controls from Development Version 378.

## Development Version 378

- Replaces colour-packed Point/Spot/Parallel shadow depth with a native linear
  `R32F` render target, avoiding sRGB transfer and colour quantisation for
  numeric shadow distances.
- Corrects soft Point-light PCF on layers that both cast and receive shadows:
  each neighbouring sample ray is compared at its intersection with the
  receiver plane instead of reusing the centre ray's distance.
- Removes the large black self-shadow wedge reproduced by the supplied 3D test
  title while preserving the caster-derived shadow arc at the receiver edge.
- Retains the complete Development Version 377 delivery.

## Development Version 377

- Fixes Point-light shadows appearing as a long diagonal artifact unrelated
  to the visual caster. Receiver lookup now uses the exact six matrices that
  wrote the Point atlas instead of a separately reconstructed cube-face UV.
- Packs normalized shadow distance across RGB24, eliminating the 8-bit radial
  depth quantization that could produce receiver self-shadow streaks.
- Explicitly excludes Light, Empty, Audio, Adjustment and container layers
  from the caster pass; only eligible 3D raster layers with `Casts Shadows`
  contribute silhouettes.
- Retains the complete Development Version 376 delivery.

## Development Version 376

- Fixes MSVC `C2026: string too big, trailing characters truncated` in
  `gpu-effects-transitions.inc` by splitting the layer-copy/lighting effect
  across adjacent raw literals below 16 KiB.
- Adds a source contract that checks every embedded effect literal against the
  MSVC limit and reconstructs the complete concatenated shader.
- Retains the complete Development Version 375 delivery.

## Development Version 375

- Adds per-light omnidirectional shadows for Point lights. Each Point light
  renders six alpha-aware, depth-tested views into a portable 3×2 atlas and
  samples radial distance in the material shader.
- Restricts the shadow pass to directly authored 3D visual layers. A layer
  casts only when `Casts Shadows` is enabled and receives only when `Accepts
  Lights` and `Accepts Shadows` are both enabled.
- Includes every eligible raster visual layer as an independent caster and
  receiver across Point, Spot and Parallel lights, with fail-open behavior for
  missing shadow resources.
- Computes Point-light penumbra from world-space Source Size at each receiver;
  authored Shadow Softness remains additive.
- Retains the complete Development Version 374 delivery.

## Development Version 374

- Replaces the single global shadow map with one depth-tested, alpha-aware
  shadow map for each of the four GPU light slots. Spot and Parallel lights
  now shadow only their own diffuse/specular contribution, so one occluded
  light no longer darkens unrelated lights, ambient light or environment light.
- Converts each light's Source Size from scene units into a projected shadow-
  map radius and filters the result with a 5 x 5 PCF footprint. Authored Shadow
  Softness remains an additive artistic adjustment.
- Adds a per-layer `Z / Visibility` selector to 3D properties. `Scene Position
  (Z)` is the default and uses camera-space depth/hardware Z; `Layer Order`
  preserves the authored layer-list order.
- Retains the complete Development Version 373 delivery.

## Development Version 373

- Adds a type-aware Light Properties section to the selected Light layer with
  enabled/type, RGBA color, brightness, source size, falloff, spot cone,
  point-of-interest and shadow authoring controls.
- Adds `light_source_size` as a backward-compatible animated property with a
  zero default, JSON persistence, timeline exposure and cache invalidation.
- Routes finite source size into all four GPU light slots, broadening the
  diffuse terminator and specular lobe and contributing to supported shadow
  softness. The canvas helper visualizes non-zero source size while selected.
- Keeps the title-wide light controls on the same model and retains the full
  Development Version 372 delivery.

## Development Version 372

- Restores the Development Version 370 FX selected-state fill and layer-type
  icon appearance while retaining continuous color-backed rows and the square
  2D/3D toggle.
- Adds a click popup to the layer-type icon with sixteen 4x4 color swatches,
  Default Layer Color and a themed Custom Color dialog. The popup and its
  swatch-grid surface use the OBS theme menu background rather than inheriting
  the selected layer color.
- Persists custom editor colors on each individual layer inside its title/
  graphic, allowing same-type layers to use different colors, and routes them
  through the shared layer-color helper used by rows, timeline and canvas.
- Keeps editor-only color metadata out of the raster fingerprint and records
  changes through the existing title save and undo snapshot path.
- Retains the complete corrected Development Version 370 delivery.

## Development Version 370

- Replaces generic flat-list InternalMove outcomes with explicit Before, After
  and Into Group drop intents. Normal rows expose only insertion boundaries;
  only unlocked Groups accept an on-row drop.
- Preserves the existing hierarchy scope for boundary reorders. On-Group drops
  preserve world transforms while reparenting and rebuild canonical group order.
- Paints every layer row with its configured layer/timeline color, using full
  opacity for selected rows and a semi-transparent 72 alpha when unselected.
- Clears native Base, Window, AlternateBase and Button palette fills from
  embedded layer-row controls so that color forms one uninterrupted surface
  behind names, selectors and indicators. Dropdown popups remain theme-opaque.
- Keeps the 2D/3D control square and uses the current OBS theme highlight for
  its active state.
- Retains the complete Development Version 369 delivery.

## Development Version 369

- Removes Preview, Range Tools and Waveform rows from pure Audio Layer
  properties while retaining its directly editable Range row.
- Prevents the responsive inspector from collapsing the Volume dB and signed
  Pan inputs to zero width; both are fixed-width editors placed visibly beside
  their sliders in Audio Layer and Video Audio properties.
- Retains bidirectional slider/input synchronization and the complete
  Development Version 368 delivery.

## Development Version 368

- Moves Parent immediately before 2D/3D in the layer list and preserves every
  optional column's width so all row controls remain aligned with the header.
- Adds a grip control at the start of each draggable layer row and routes it
  through the existing hierarchy-aware internal move behavior.
- Enlarges the 2D Empty editor cross from 36 px to 56 px while retaining the
  compact 3D axes representation.
- Retains the complete Development Version 367 delivery.

## Development Version 367

- Adds synchronized signed dB Volume and signed Pan entry to Audio and Video
  audio properties, removes the duplicate inner title and completes its locale
  strings.
- Adds non-raster 2D/3D Empty transform-parent layers, drawn and selected only
  through their editor cross/axes object with no canvas bounding box.
- Removes pure Audio layers from canvas bounding-box geometry, preserves
  aggregate keyframe markers on collapsed strips and changes solid grey to
  `#4a4a4a`.
- Gives the Audio Editor meter theme-aware OBS green/yellow/red zones, peak
  markers and clip feedback.
- Retains the complete Development Version 366 delivery.

## Development Version 366

- Removes the fixed 240 px minimum width from Titles and Graphics list rows.
- Makes list rows follow the live viewport width with Adjust-mode relayout and
  no horizontal scrollbar as the dock resizes, floats or changes dock area.
- Retains the fixed card geometry used intentionally by icon/grid mode.
- Retains the complete Development Version 365 delivery.

## Development Version 365

- Moves the single read-only Cue Preview panel into the external Live Text Cues
  window while that window is open, including Canvas, status, Take and Cancel.
- Reparents the authoritative widget rather than copying it, so PreviewReady,
  GPU/media state and local/OBS routing remain synchronized automatically.
- Returns Preview to splitter position 2 on window close and protects its
  lifetime when the dock shuts down with the external window still open.
- Rejects every editing input path on the local Preview canvas and recursively
  locks duplicated OBS Preview items, including nested group children.
- Retains the complete Development Version 364 delivery.

## Development Version 364

- Renames Show Preview Before Cue to Select Row Before Cue and preserves its
  default-off, two-stage selection-to-Program behavior.
- Adds a separately persisted Show Preview child option. It is enabled in the
  menu only while Select Row Before Cue is active and controls both the local
  read-only Preview section and the OBS Duplicate Scene Preview route.
- Keeps PreviewReady row state, cache priming and Next/Previous Cue navigation
  active with visual Preview hidden, and migrates the legacy combined setting.
- Retains the complete Development Version 363 delivery.

## Development Version 363

- Reads `BasicWindow/SceneDuplicationMode` from OBS's modern user configuration
  first and uses the legacy global configuration only when no migrated user
  value exists.
- Replaces the pointer-inequality fallback with an isolated private duplicate of
  the active OBS Preview scene, installs it with the frontend Preview API and
  restores the previous Preview scene on exit. Program is never modified.
- Routes Next Cue and Previous Cue hotkeys through the dock. With Show Preview
  Before Cue enabled, PreviewReady becomes their navigation base and the next or
  previous row is rendered frozen in Preview without taking it to Program.
- Retains the complete Development Version 362 delivery.

## Development Version 362

- Removes the unsupported `obs_sceneitem_set_selected()` call that caused
  MSVC C3861 while compiling `title-dock.cpp` against the OBS SDK.
- Retains the supported locked scene-item state, so the private OBS Preview
  overlay remains non-editable.
- Retains the complete Development Version 361 delivery.

## Development Version 361

- Moves the local cue Preview into a separate read-only splitter section that
  cannot select, edit, focus or accept drops and does not move with the Live
  Text Cues editing pop-out.
- Uses OBS `BasicWindow/SceneDuplicationMode` as the routing authority: with
  Duplicate Scene off (or Studio Mode off), the local section is used; with it
  on, the frozen private source is placed at the top of the isolated OBS
  Preview scene and stretched to the base canvas without touching Program.
- Reconciles the route while PreviewReady, removes its temporary scene item on
  Cancel/route changes/shutdown, and renders the immutable cue snapshot through
  the OBS source's dimensions, render, media-duration and seek callbacks.
- Retains the complete Development Version 360 delivery.

## Development Version 360

- Adds the optional, default-off Show Preview Before Cue state and a private,
  frozen GPU/media preview path with deterministic Pause → loop start → frame 0
  selection and no Program, timeline, event or audio side effects.
- Keeps Preview resident after Take, marks Preview-only rows green and rows that
  are simultaneously in Preview and Program red.
- Moves the complete Live Text Cues pop-out action into Live Text Settings.
- Adds Cue to Program, Uncue and Cue Last hotkeys to every title's FXM Hotkeys
  section.

## Development Version 359

- Expands the Live Text Cues pop-out from the table alone to the complete panel:
  header, live cue timer, cue table, row controls, data-source controls,
  playlist/countdown, persistence, settings and external refresh controls.
- Moves those authoritative widgets themselves into the non-modal window and
  restores the same widgets in their original dock order when it closes, so
  state, signals, menus and button availability remain identical.

## Development Version 358

- Keeps the Live text cue Cache Status column hidden whenever caching is
  disabled, including after restoration of a saved movable-header layout.
- Adds a non-modal, resizable editing window for the complete Live text cue
  table. The window detaches the authoritative table widget itself and returns
  it to the dock when closed, eliminating duplicate state and synchronization
  drift across cue status, text, image, color and external-data controls.
- Keeps the detached editor live while the selected title, cue state, cache
  state or external data changes in the dock.

## Development Version 357

- Keeps every outgoing live-cue row yellow for the complete authored outro,
  both after an explicit uncue and while another row is pending during a
  cue-to-cue hand-off.
- Uses one ending-state predicate for cue icons, row borders and full-row
  backgrounds so every dock refresh path presents the same state.

## Development Version 356

- Fixes both editor restart-loop paths: direct preview `Mode: Loop` and
  `Mode: Playback Mode` when the title's authored Playback Mode is Loop.
- Marks only a restart wrap as a transport discontinuity, prevents the stale
  pre-wrap audio clock from replacing the wrapped playhead, and explicitly
  seeks/resumes the private editor audio source at the loop start.
- Leaves ping-pong playback unchanged because its playhead remains continuous
  and only its direction reverses.

## Development Version 355

- Adds a complete four-channel ARGB Stroke Color animation track to Appearance,
  including previous/next navigation, Delete All Keyframes, timeline exposure,
  persistence, cache invalidation and editor/source rendering.
- Moves Emissive Color directly after Fill whenever material controls are
  available and unifies all Properties/toggle labels with the 3D Camera label
  typography while deriving their colors from the active OBS palette.
- Corrects editor transport behavior across Start, preview Mode, authored
  Playback Mode and A/V cadence: From Beginning is timeline zero, Play Every
  Frame advances deterministically, stale reverse state is cleared on mode
  changes, Pause titles can resume their outro from Current Time, and cached-only
  playback presents its terminal frame before stopping.

## Development Version 354

- Removed the trailing elastic Transform-grid column so Position, Scale/Size, Anchor, Rotation and Orientation keyframe navigation groups are anchored at the actual right edge.
- Places dock tabs in every dock area on the upper edge and explicitly keeps editor-owned tab widgets at the top.
- Replaced the Effect Settings complete-stack visibility toolbutton with the shared OBS-theme-aware `Effect Stack` toggle switch.
- Retains all Development Version 353 responsive layout and locked-dock header behavior.

## Development Version 353

- Rebuilt the 3D Orientation row on the same responsive Properties grid as Position, Scale, Anchor and Rotation, so X/Y/Z no longer extend beyond the inspector width.
- Removed the elastic width from Lock Scale, keeping the label and switch together and leaving spare row width after the Scale options.
- Moves the functional 2D Size/aspect lock into the same Scale Stroke / Scale Corners row and presents it as Lock Scale; 3D continues to use the true XYZ Scale lock in that position.
- Locked docks no longer expose a close button. Tabbed dock headers are hidden completely while locked, as are the standalone Sidebar and Editor Audio headers; normal headers return on unlock.
- Retains all Development Version 352 responsive Properties styling.

## Development Version 352

- Fixed the hidden dynamic layout path that moved Rotation Z from the XYZ column to column 4; 3D Rotation is now always X/Y/Z in columns 1/2/3, while 2D Rotation spans the full field width.
- Moved Lock Scale out of the Scale value row and into the Scale Stroke / Scale Corners options row.
- Removed the 130 px Transform/Shape/Image field minima and the 50 px keyframe-column minima that forced a wide Properties dock.
- Reduced the Layer Properties panel minimum width from 260 px to 180 px and made its input size hints responsive without changing other inspectors.
- Applied a direct Camera-style, OBS-palette-derived stylesheet to the complete Layer Properties content tree.
- Retains the compact Development Version 351 keyframe carets.

## Development Version 351

- Rebuilt Transform, Shape Size and Image Box Size rows in the 3D Camera visual pattern: right-aligned property labels, separate XYZ/WH labels and uniformly filled, expanding fields.
- Applied the Camera inspector's compact margins, row spacing and field-growth behavior to every collapsible Properties section.
- Removed remaining fixed-width constraints from normal Properties inputs so fields fill their available row width consistently.
- Replaced remaining hard-coded Properties accent colors with the active OBS palette highlight and text colors.
- Reduced Previous/Next keyframe caret controls from 16×18 px to 10×14 px while retaining the 18 px diamond and all navigation behavior.
- Retains all Development Version 350 theme-aware audio meter and linked text-box maximum behavior.

## Development Version 350

- Moved keyframe navigation groups to the right edge of property rows and removed the borders from diamonds and Previous/Next caret controls.
- Matched Properties section geometry, field metrics, labels, colors, margins and padding to the compact 3D Camera inspector styling.
- Reworked Editor Audio as a narrow Premiere-style stereo dB meter with dual channels, peak hold, bottom mute control and a red X over muted headphones.
- Made the audio meter derive its background, scale, frame, text and level gradient from the active OBS `QPalette`, so it follows every OBS theme.
- Linked Max Text Box Width/Height to the authored Size values until the user explicitly enters a different maximum, including serialization and canvas resizing.
- Retains all Development Version 349 compiler and keyframe-color fixes.

## Development Version 349

- Fixed the MSVC `C2065: C_KF_DOT undeclared identifier` failure emitted from `hierarchy-model.inc` in every translation unit that includes the shared editor internals.
- Moved the keyframe yellow into the common modern-controls API so timeline keyframe shapes and inspector diamond assets use the same maintained color source.
- Retains all Development Version 348 editor UI fixes.

## Development Version 348

- Removed the redundant Properties title row and its duplicate Undo/Redo buttons from the layer inspector.
- Unified every keyframe diamond on the shared active/outlined/inactive SVG asset and compact 18 px control metric.
- Added property-track Previous/Next keyframe carets around inspector, 3D Camera, Effects, text animator, stroke-offset and free-transform diamonds; unavailable directions stay disabled.
- Added or retained Delete All Keyframes context menus for the unified diamond controls, including camera, audio, extension-effect and free-transform tracks.
- Normalized editor input fields to the compact 3D Camera inspector height, typography, padding, borders and focus style.
- Replaced ordinary editor checkboxes with the shared theme-aware toggle switch control.

## Development Version 347

- Fixed imported Photoshop text becoming effectively invisible because EngineData scale `1.0` was incorrectly interpreted as 1% instead of 100%.
- Added dual scale normalization for standard ratio values and percentage-style PSD producers.
- Reasserts descriptor-decoded UTF-8 text as the canonical rich-text `plain_text` during final layer conversion, preventing synchronization from emptying a valid Text layer.

## Development Version 346

- Filters disabled Photoshop effects out of imported native FXM effect stacks for object-based `lfx2`/`lmfx`, legacy `lrFX` and the final layer conversion boundary.
- Decodes Photoshop `StyleRun` and `ParagraphRun` arrays into canonical rich-text character and paragraph ranges with UTF-16-to-UTF-8 offset conversion.
- Preserves Photoshop font, size, bold/italic, underline, strike, tracking, baseline, horizontal/vertical scaling, caps, fill, stroke and paragraph layout where represented by EngineData.
- Initializes every imported SVG, PSD, XCF and FXM title Group as collapsed, including nested source groups.

## Development Version 345

- Collapsed File > Import into one direct action and unified runtime-aware filter for Qt-supported images, SVG, PSD, XCF and FXM title/graphics formats.
- Added recursive SVG `<text>`/`<tspan>` parsing with UTF-8 rich-text ranges and per-run font, size, weight, style, stretch, decoration, tracking, baseline, fill/gradient and stroke data.
- Added local/data-URL `@font-face` registration and retained orthogonal rotate/scale transforms on editable SVG Text layers.
- Added Photoshop EngineData rich-text decoding for font sets, style/run arrays, fill/stroke colors, font metrics and justification.

## Development Version 344

- Extended SVG import with CSS/presentation colors, `rgb`/`rgba`/`hsl`/`hsla`, `currentColor`, inherited font family/size/weight/style, tracking, text anchoring, native editable text layers, fill/stroke gradients, intermediate stops, angle/center/focal/scale and spread modes.
- Added PSD `lfx2`/`lmfx` descriptor and legacy `lrFX` decoding, mapping Photoshop shadows, glows, overlays, strokes and bevel/emboss settings to native FXM effects.
- Expanded Photoshop mode-family mapping for darken/lighten, burn/dodge, soft/hard/vivid/linear/pin light and hue/saturation/color/luminosity variants.
- Changed 2D Flip Horizontal/Vertical to reflect the complete world transform around the layer anchor in canvas space and convert the result back through its group/transform-parent basis.

## Development Version 343

- Corrected the incomplete Development Version 342 include relocation: `commands-docks.inc` and the later editor modules also continue scopes opened by earlier `.inc` files.
- Moved `import-documents.inc` after the complete pre-existing editor implementation chain, so its namespace and all importer methods begin at genuine translation-unit file scope without interrupting `build_ui()` or any later member function.
- Added a complete include-chain regression contract covering every editor `.inc` module and the final importer position.

## Development Version 342

- Moved the document-import implementation include after `panels-colors.inc`, whose opening UI method intentionally continues across the module boundary. The importer namespace now begins at true translation-unit file scope under MSVC.
- Prevents the resulting namespace-scope parse failure, cascading `kPi`/`QTransform::shear` diagnostics and MSVC internal compiler error while retaining the complete SVG/XCF/PSD import implementation from Development Version 341.

## Development Version 341

- Added **File > Import** with Vector, GIMP and Photoshop document importers.
- SVG documents are parsed into editable Shape layers; multiple SVG shapes are placed in an automatically created Group while fill, stroke, opacity, transforms and Bézier geometry remain editable.
- GIMP XCF and Photoshop PSD imports offer a pre-import choice between one merged bitmap and separate document layers.
- Separate-layer import preserves document order, hierarchy, names, visibility, opacity and supported blend modes, maps groups/text/vector-shape/solid/adjustment/pixel content to corresponding FXM layer types, and retains lossless source channels for pixel layers.
- PSD and XCF decoding runs inside the plugin and does not require external graphics software.

## Development Version 339

- Reverted reusable Asset Library persistence to the pre-338 data-store workflow; **Save as Asset** no longer creates or maintains `.fxmp` packages.
- Removed packed-asset package creation, explicit-save replacement and deletion cleanup.
- Kept the independent asset clock, paused/stopped editor refresh, GPU recomposition fix and media-only asset animation detection from Development Version 338.
- Packed `.fxmp` title/template import and export remain available and unchanged.

## Development Version 338

- Reusable Asset Library entries are now written as complete `.fxmp` packages with images, video/audio and fonts packed by default.
- Explicit Save of an edited asset atomically refreshes its package, while asset deletion removes the managed packed file.
- Independent animated assets now use the editor playback cadence even when the containing title is paused or stopped.
- Video- or audio-only assets are now recognized as animated timeline content and expose synchronized/independent playback controls.
- The GPU compositor treats the independent asset clock as a live frame dependency, preventing transform-, opacity- and other composition-only animation from freezing while the parent playhead is held or crosses a loop boundary.
- OBS source, editor canvas and cache scheduling now share the same independent-playback predicate.

## Development Version 337

- Added `.fxmp`, a separate binary packed-title container with a versioned manifest and streaming LZ4 block compression.
- Exporters can independently pack images, video/audio media and fonts; disabled categories stay as external references.
- Added atomic container writes, per-entry SHA-256 validation, bounded block decompression and safe extraction-path validation.
- Packed imports resolve resource URIs to persistent per-package files and register embedded fonts before title diagnostics/rendering.
- Standardized editable Flux Motion title and graphic documents on `.fxmt`.

## Development Version 336

- Replaced geometry-dependent Rotate dragging with a linear screen-space scrub after ring hit-testing.
- Locks the dominant horizontal or vertical mouse direction after the first three pixels, then maps right/up to positive and left/down to negative rotation at 0.5 degrees per pixel.
- Removes all ray-plane, projected-tangent and polar-angle drag singularities, so a ring remains interactive when its plane is exactly perpendicular to the screen.
- Preserves Local/Parent/World axis selection, complete XYZ Euler reconstruction, multi-selection and Shift 15-degree snapping.

## Development Version 335

- Added a projected-ring tangent fallback for Rotate drags when an X/Y rotation plane is edge-on to the active camera.
- Rejects near-parallel ray/plane intersections whose hit lies many visible-ring radii away, preventing an almost-zero angular response before the layer has any Z rotation.
- Rebinds the Layer Properties and Effects panels to the newly restored Layer objects after every Undo/Redo snapshot replacement, even when the selected layer ID is unchanged.
- Refreshes restored property values, widget state and keyframe diamonds immediately instead of waiting for a selection or playhead change.

## Development Version 334

- Applied the authored-only rule to every property track in the shared Layer List/Timeline model, including camera properties, legacy title lights, camera assignment and Camera Switches.
- Restored Double-sided behavior for Text, Clock and Ticker by removing the typography-only front-face override from the shared culling predicate.
- Corrected local transform composition to apply Orientation as the base axis frame before Rotation XYZ.
- Rebuilt rotation-ring axes from scale-free orthonormal Local and Parent bases; World remains canonical canvas XYZ.
- Replaced screen-polar rotation dragging with world-ray/rotation-plane intersection and signed 3D angles.
- Applies Local, Parent and World rotation deltas through the appropriate conjugated basis, decomposes the result back to stable ZYX Euler channels and authors all grouped Rotation XYZ channels together.
- Advanced the GPU renderer/cache ABI to v53.

## Development Version 333

- Added inspector diamonds for every animated camera property: Projection, Position, Target, Orientation, Rotation, Focal Length, Field of View, Zoom, Near Clip and Far Clip.
- Made grouped XYZ diamonds author and remove their complete camera tracks, while Projection keys retain discrete Hold interpolation.
- Rebuilds the Layer List immediately after camera inspector keyframe edits, so the default camera row exists exactly while it owns authored property keyframes.
- Removed the always-authorable material-track exception; Material Options now appear in the Layer List and Timeline only for properties with keyframes.

## Development Version 332

- Excluded Light controls from root and group visible-artwork composition while retaining their lighting and shadow participation.
- Changed the defensive Light branch in the GPU layer renderer from a successful no-op to non-renderable, so reusable target storage cannot be mistaken for newly rendered artwork.
- Eliminated the untransformed stale 3D copy that appeared at canvas centre immediately after adding a Light layer.
- Bumped the renderer cache ABI for the corrected controller/artwork boundary.

## Development Version 331

- Routed every compatible 3D run through the native hardware-depth camera path, including scenes and groups containing only one raster layer.
- Removed the single-layer-only projective fallback that caused Y rotation to behave differently—and horizontally mirror content—until a second layer was added.
- Replaced shader-based stable-frame publication with a validated, exact GPU resource copy, ensuring transparent or back-face-culled frames erase the previously published image.
- Retained the existing one-time canvas-Y/libobs coordinate conversion and bumped the renderer cache ABI.

## Development Version 330

- Froze the current planar 2D/3D renderer as the compatibility baseline for the mesh-era rebuild.
- Disabled the legacy repeated-alpha-slice extrusion/bevel renderer and removed its authoring panel; serialized fields remain readable and writable strictly for migration.
- Removed legacy extrusion from animation, cache, raster-selection and hardware-depth scheduling decisions.
- Made stable-frame publication an explicit two-target transaction with Free, Rendering, Complete and Published states plus a monotonic publication generation.
- Restricted normal OBS presentation and prerender readback to the validated Published target, so partial or failed replacement frames cannot escape.
- Restored canonical GPU target state before every clear and changed stable publication to exact full-frame replacement, preventing negative rotations from retaining an old Text raster or shifting unrelated artwork.
- Required editor frames to match the current model revision so stale artwork cannot be shown under current transform overlays; live OBS retains its complete-frame fail-safe.
- Corrected the hardware-depth camera-space Y convention so positive/downward canvas movement is no longer rendered in the opposite direction.
- Hardened every reusable off-screen target, alias-prone ping/pong composition and 3D cached-prefix boundary against retaining the initial layer position.
- Kept reused Text, Clock and Ticker glyph rasters front-facing so flipped layers do not expose mirrored typography.
- Bumped the renderer cache ABI to invalidate prerenders produced under the retired geometry path.

## Development Version 329

- Moved the destructive reset from title Source Properties to **Preferences > Advanced**.
- Added a red danger-zone button and two-stage confirmation before any deletion.
- Clears FXM QSettings in native and INI formats, including the complete Windows `HKEY_CURRENT_USER\\Software\\FluxMotion` registry subtree.
- Deletes FXM plugin configuration and user files, including titles, layouts, templates, presets, palettes, installed effects, frame/proxy/optical-flow caches and FXM logs, while refusing to recursively remove shared application-data/cache roots.

## Development Version 328

- Fixed Motion Blur flicker during Background Persistence by collapsing the shutter for held layers and rendering their frozen state once.
- Incoming and otherwise non-persistent cue layers retain their authored Motion Blur.
- Applied the same rule to live GPU rendering and compatibility/cache rendering.
- Added a targeted render diagnostic whenever persistence bypasses Motion Blur.

## Development Version 327

- Fixed Background Persistence using different clocks for retained pixels and GPU transforms during cue-to-cue playback.
- Persistent layers inside Asset hierarchies now inherit the frozen root hold time before resolving their asset-local timeline.
- Added persistence-layer diagnostics to source render logs.
- Built from the clean Development Version 313 logging/source-diagnostics baseline; the unrelated 325 and 326 experiments are not included.

## Development Version 313

- Reorganized logging options into logical groups: Core and application, OBS source, Editor and interface, Title model and animation, Rendering, and Cache and media.
- Added dedicated source logging categories for lifecycle/visibility, timing and animation, frame presentation, flicker/frame consistency, source scene masks, and source audio.
- Added source tick-cadence diagnostics and correlated tick/render serials, playhead, cue phase, dirty/first-frame state and presentation generation data.
- Added diagnostics for every skipped source draw, successful GPU frame publication, deferred or missing frames, stale model revisions and visible-frame gaps that can manifest as flicker during animation.
- Kept the highest-volume Source Flicker, Cache Playback and Performance categories disabled by default; they can be enabled only while reproducing a fault.

## Development Version 312

- Rebuilt the editor default dock workspace: Sidebar at the far left, Editor Audio immediately beside it, all utility panels in one tabbed column, Timeline at the bottom, and Layer Properties at the right.
- Increased the editor layout schema so installations with stale or collapsed dock state automatically receive the corrected workspace.
- Added a destructive “Reset All Flux Motion Settings…” action to the OBS source Properties panel, with two confirmation dialogs. It clears FXM settings and removes the plugin application-data and cache folders.

## Development Version 311

- Invalidated the corrupted Development 309/310 dock-state schema so the editor rebuilds the new default workspace once on upgrade.
- Checked panel actions now recover docks that have no dock area, have been restored off-screen as floating windows, or are trapped in a zero-size splitter.
- Preserves legitimate on-screen floating panels and normal user-resized dock dimensions.

## Development Version 310

- Fixed checked dock menu items that did not bring tabified docks to the front.
- Panel locking now disables movement and floating without invalidating dock areas or freezing splitter dimensions.
- Clears the width/height clamps introduced by Development Version 309 so previously hidden or collapsed docks recover automatically.

## Development Version 309

- Fixed the Windows linker failure by adding the missing `TitleEditor::set_panels_locked(bool)` implementation.
- The dock-panel lock action now updates dock features immediately and persists the lock state in the editor layout settings.

# v0.8.12-alpha — Development Version 308

## Development Version 308 — motion blur resolve, default workspace, cross-session clipboard

- Motion Blur now resolves the normalized shutter exposure directly instead of applying an optical-density alpha lift, preventing opaque stacked afterimages.
- Transform-only temporal sampling uses a denser editor-safe budget to reduce visible sample stepping.
- The editor default dock workspace is reorganized into a compact creation/library column, a logical inspector/effects column, and a full-width timeline.
- Layer copy/paste now uses a canonical JSON MIME payload on the system clipboard, allowing paste between editor windows and separate editor sessions while preserving groups, animation, effects, rich text, media bindings, and assigned cameras.

# v0.8.12-alpha — Development Version 307

## Development Version 307 — correlated render diagnostics

- Added the dedicated `RenderDiagnostics` logger category.
- Correlated editor playhead requests, canvas scheduling, GPU session updates, render attempts and stable-frame publication through monotonically increasing serials.
- Added scalar snapshots for session/published time, model revisions, dirty/deferred state, raster readiness, active extrusion/light counts, hardware-depth runs and executed extrusion passes.
- Added rate-limited canvas stall warnings when playback repeatedly prepares the same playhead.
- Added structured geometry-authoring logs for extrusion and bevel controls.
- This release is diagnostic-only and intentionally does not change render, cache or serialization decisions.

# v0.8.12-alpha — Development Version 306

## Development Version 306 — single-layer extrusion and transactional GPU publication recovery

- Made enabled Text, Clock, Ticker and Shape extrusion a self-contained hardware-depth pass, including scenes where the only other 3D object is a non-raster Light layer.
- Routed extruded text through the exact compatibility raster before depth-shell composition so GPU glyph-buffer allocation is not a prerequisite for extrusion.
- Added an immediate CPU fallback raster for first GPU-text publication failures.
- Replaced optional-raster fail-open with transactional defer: an older text/vector texture can remain visible only as the last complete frame while the editor immediately rebuilds the exact current model.
- Exposed deferred draw state to the canvas and replaced repaint-only recovery with a forced model refresh, preventing stale frames from persisting until a light or other scene property changes.
- Advanced development and GPU text/cache identities to 306.

# v0.8.12-alpha — Development Version 304

## Development Version 304 — unified 3D inspector, responsive Z interaction and extrusion repair

- Added a dedicated dock for 3D Camera, Lights and Environment controls.
- Standardized Material and Geometry inspector sections, including keyframe controls and numeric label scrubbing.
- Made Emissive Color an Appearance swatch with animated ARGB channels, serialization, timeline tracks and cache dependencies.
- Suppressed artwork boxes/resize handles for Light layers while retaining light-object overlays and the shared 3D gizmo.
- Restricted Light-layer hit testing to the projected object overlay, eliminating invisible resize/move regions.
- Restored the transform-only GPU path for unified XYZ movement by copying `position_3d` and its authority flag into the render-session snapshot.
- Removed the obsolete 3D full-refresh gate so interactive XYZ/orientation/camera changes use resident GPU rasters until the settled rebuild.
- Promoted enabled text/shape extrusion to 3D depth rendering automatically and migrated Development 300–303 saved states.
- Improved extrusion with adaptive depth-shell density and per-shell world-space lighting planes.

# v0.8.12-alpha — Development Version 299


## Development Version 299 — Windows build fixes for 3D lighting

- Split the large embedded layer-compositor effect into adjacent raw string literals so MSVC no longer raises C2026 while preserving one identical shader source at runtime.
- Renamed the local `slots` array to avoid collision with Qt's `slots` preprocessor macro, which caused the parser cascade in `gpu-presentation-readback.inc`.
- Corrected the material-field factory lambda to capture `this`, allowing access to `three_d_controls_` under MSVC.
- Removed a duplicate `clipAlpha` declaration found while validating the shader source.
- Advanced development and GPU pipeline identities to 299 and added regression checks for these Windows-specific failures.

## Development Version 298 — 3D lighting, materials and planar shadows

- Integrated lighting into the existing depth-aware GPU layer compositor rather than introducing a second renderer or a post-process approximation.
- Added title-level **Ambient**, **Point**, **Spot**, **Parallel** and **Environment** lights with animated color, intensity, position, point of interest, distance falloff, cone, environment rotation and shadow properties.
- Added an explicit default-light compatibility policy. Existing titles keep their previous pixels because layer **Accepts Lights** is off by default; adding the first authored light disables the compatibility light.
- Added opt-in per-layer Material Options: Accepts Lights/Shadows, Casts Shadows, reflection participation, ambient, diffuse, specular, shininess, metallic, roughness, reflection intensity, emissive color and emissive intensity.
- Added per-pixel planar shading derived from the existing world matrix, transformed normal and active camera position. Four explicit light slots avoid backend-dependent dynamic uniform arrays across OBS D3D11/OpenGL/Metal shader translation.
- Added normalized None/Linear/Inverse-square distance attenuation, Spot cone feathering, metallic/dielectric Fresnel response, roughness-shaped highlights, environment-color lighting and premultiplied-alpha-safe emission.
- Added the first real shadow pass: the first enabled shadow-casting Spot or Parallel light renders alpha-tested 3D planar casters into a 512px draft/1024px final color-depth target, then receivers use a configurable 3×3 PCF lookup with darkness, softness and bias.
- Added Light owner rows to the common Layer List/Timeline/Graph Editor, including Position, Point of Interest, Color, Intensity, Falloff, Cone, Shadow and Environment Rotation tracks. Material scalar tracks are authorable from expanded 3D layer rows.
- Added canonical serialization, duplicate-ID recovery, opaque unknown-field passthrough, animation detection, frame bounds, visual/content hashes and cache invalidation for every new light/material pixel input.
- Added editor controls under **3D Lights & Environment** and layer **Material Options**, including color swatches, numeric drag behavior, type-dependent enablement and safe reset defaults.
- Advanced title serialization development identity to 265 and GPU/cache identities to Development Version 298.
- Added an executable Development Version 298 contract covering the data model, serialization, shader binding, shadows, editor/timeline authoring, cache dependencies and compatibility defaults.

### Deliberate scope limits in this development build

- Point lights illuminate correctly but do not yet cast cube-map shadows.
- One shadow map is evaluated per title: the first enabled shadow-casting Spot or Parallel light.
- Environment lights currently use authored color/intensity for diffuse/reflection contribution; HDRI texture sampling and importance-filtered image-based lighting are not yet implemented.
- The implementation shades existing planar 3D layers. Extruded/beveled text and shape geometry, imported glTF/GLB meshes, normal maps, transmission/refraction and camera depth of field remain separate renderer phases.

## Previous: Development Version 297

# v0.8.12-alpha — Development Version 297

## Development Version 297 — Temporal-occupancy Motion Blur alpha resolve

- Replaced the Development Version 296 direct max-alpha output that made every historical shutter position fully opaque.
- Keeps normalized full-shutter RGB and does not composite a separate sharp current-frame color at 100% effect strength.
- Uses maximum sample alpha only as an authored-coverage ceiling. Final wet alpha is based on temporal occupancy (`exposure alpha / coverage alpha`) with a smooth optical-density lift.
- Fully or strongly overlapped pixels retain the layer's authored opacity, while low-occupancy outer trails fade progressively instead of becoming opaque copies.
- Preserves authored translucent fills, shadows, glows and antialiased edges because resolved alpha can never exceed the strongest alpha actually present at that pixel in any shutter sample.
- Applies the identical resolve to complete GPU effect/transition sampling, transform-only GPU sampling, GPU readback and Cairo compatibility rendering.
- Leaves effect ordering, Noise/Grain, Trim Paths, transitions, sample-density rules, source budgets and editor performance changes untouched.
- Advances the effect-output, visual-renderer and GPU text-pipeline cache identities to prevent reuse of Development Version 296 output.

## Previous: Development Version 296

# v0.8.12-alpha — Development Version 296

## Development Version 296 — Opaque temporal-coverage Motion Blur resolve

- Keeps the Development Version 295 full-shutter temporal color: at 100% effect opacity no separately reinforced sharp current-frame RGB is composited.
- Adds an independent max-alpha envelope accumulated from the exact same shutter samples. Opaque artwork therefore remains opaque instead of becoming translucent when its normalized exposure is spread over motion.
- Resolves exposure straight color against temporal max coverage, preserving premultiplied output.
- Uses maximum—not additive or source-over—coverage, so authored translucent fills, shadows, glows and antialiased edges do not gain opacity from overlapping samples.
- Applies the same contract to complete GPU effect/transition samples, transform-only GPU sampling, GPU-readback acceleration and the Cairo compatibility path.
- Leaves effect ordering, Noise/Grain sampling, Trim Paths, transitions, sample-density rules and editor/source performance budgets unchanged.

## Previous: Development Version 295

## Development Version 295 — Full-shutter Motion Blur resolve

- Removed the separately reinforced current-frame silhouette from full-strength Motion Blur. A moving layer now resolves to the normalized shutter exposure without a separately reinforced sharp current frame.
- Replaced the Development Version 294 strongest-alpha resolve with a standard premultiplied RGBA dry/wet interpolation. At 100% effect opacity the output is exactly the temporal exposure; lower effect opacity intentionally mixes the unblurred current frame with that exposure.
- Preserved normalized sample weighting, so overlapping samples of semi-transparent fills, shadows, glows, antialiased edges and effect output do not gain opacity through additive or source-over accumulation.
- Applied the same resolve contract to transform-only GPU Motion Blur, complete temporal rendering with effects/Trim Paths/transitions, GPU readback and Cairo compatibility fallback.
- Kept the complete effect-stack sampling and quality/performance budgets from Development Version 294 unchanged.
- Advanced the effect-output, visual-renderer and GPU text-pipeline cache identities to prevent reuse of pre-295 resolved frames.

# v0.8.12-alpha — Development Version 294

## Development Version 294 — Motion Blur quality and complete effect-stack exposure

- Restored the pre-regression distance-adaptive sampling rule: authored `Samples` is again a minimum quality request and fast motion may increase resident-texture samples up to a path-specific cap.
- Removed the dev289 2–6-sample throttle from the cheap transform-only GPU path; CPU-raster temporal work retains a strict source budget.
- Replaced sharp-frame source-over resolution with an opacity-preserving temporal color resolve. Noise, Grain, shadows, glows and internal texture detail now participate in the blur inside opaque silhouettes, while authored alpha remains unchanged.
- Animated procedural effects and every native effect property now trigger temporal reevaluation during the shutter interval; future effects using the generic animated flag inherit the same behavior automatically.
- Increased the GPU-only complete temporal budget for animated effects while keeping animated Trim Paths/text geometry on the lower CPU-raster budget.
- Bumped the effect-output cache identity to prevent reuse of pre-dev294 Motion Blur/effect results.
- Kept the Development Version 293 editor GUI-thread and playback performance audit unchanged.

# v0.8.12-alpha — Development Version 293

## Development Version 293 — Editor playback and UI-event performance audit

- Audited the editor path separately from OBS source rendering after reproducing playback slowdown while moving a high-polling-rate mouse across inspector controls.
- Split playhead consumers by cost. The canvas still receives every transport frame, transport feedback such as the timeline/timecode is monitor-capped to at most 30 Hz, and heavyweight Layers/Properties/Effects/Graphic Properties/sidebar evaluation is capped at 10 Hz during playback. Scrubbing, stepping and the final stopped frame still force an exact full UI refresh.
- Stopped reevaluating hidden inspector docks. This removes repeated animated-property evaluation, keyframe-row traversal, control updates, canvas-handle publication and color-swatch restyling for panels that cannot currently be seen.
- Added playback-time passive pointer filtering: no-button `MouseMove`/`HoverMove` events over ordinary inspector widgets are coalesced to at most 30 Hz before QSS/custom-widget processing. Canvas and Timeline retain continuous pointer input, while clicks, drags, wheels, tooltips and Enter/Leave hover changes remain intact. Future controls can opt in with `fxmContinuousPointerDuringPlayback`.
- Prevented editor audio preview duplication for titles that contain no audio tracks, including recursive Asset checks. Audio-capable titles now publish their title snapshot only on creation, discontinuities or actual model/audio changes instead of marking the private source dirty on every playback frame.
- Avoided repeated media play/pause calls and per-frame audio debug formatting when transport state is unchanged. The editor audio meter no longer repaints identical levels, uses a coarse timer, and suspends that timer while its dock is hidden.
- Removed the unconditional 10 Hz title-bar rewrite from the dynamic Clock/Ticker timer, changed the background diagnostics timer to a coarse idle-only timer during playback, and guarded title, diagnostics and swatch setters against unchanged values.
- Made Debug/Trace logging lazy so disabled high-frequency render diagnostics no longer construct `QString` payloads before the logger rejects them.
- Removed stale hard-coded development numbers from the automated-suite runner; manifest validation and JSON reports now follow the current CMake development version, avoiding false failures on future deliveries.
- Kept the GPU text/base-raster cache revision at 292 because this audit changes editor scheduling and auxiliary source lifecycle, not rendered-pixel or cache-key semantics.

# v0.8.12-alpha — Development Version 292

## Development Version 292 — Coverage-preserving Motion Blur alpha resolve

- Reworked Motion Blur compositing so semi-transparent fills, shadows, glows, antialiased edges and effect output no longer gain opacity when temporal samples overlap.
- Replaced the source-over alpha union with a premultiplied, coverage-preserving resolve: source-over color is normalized to `max(sharp alpha, trail alpha)`. Opaque current pixels remain opaque, authored translucent coverage remains stable, and trail-only pixels remain visible.
- Routed the transform-only OBS source fast path through the same trail/sharp resolve used by the complete effects, Trim Paths and transition path, removing divergent alpha behavior between optimization branches.
- Applied the same contract to GPU readback and Cairo fallback rendering, including cached-raster samples.
- Kept normalized sample weighting, source frame budgets, Trim Paths shadow alignment and nested temporal target isolation from Development Versions 287–291.
- Bumped the GPU text-pipeline cache revision to 292.

# v0.8.12-alpha — Development Version 291

## Development Version 291 — Trim Paths shadow alignment under Motion Blur

- Fixed an OBS-source-only offset of legacy shadows and layer-space Drop Shadow/Long Shadow while Trim Paths is animated under Motion Blur.
- Coordinate-sensitive temporal samples now keep a 1:1 local raster so the shadow offset remains anchored to the trimmed geometry.
- The full-resolution override is limited to the Trim Paths + shadow combination; other source Motion Blur samples retain the Development Version 289 real-time budget.
- Bumped the GPU text-pipeline cache revision to 291.

# v0.8.12-alpha — Development Version 290

## Development Version 290 — Animated vector property compile fix

- Fixed MSVC error C2440 in `source-runtime.inc`: `Layer::origin_prop` is an `AnimatedVectorProperty` (`AnimatedVec2Property`) and cannot be stored in an `AnimatedProperty*` scalar array.
- Motion Blur shutter-interval detection now evaluates `origin_prop` with `animated_vec2_property_changes_during_interval()`, together with `size` and `image_size`.
- Bumped the GPU text-pipeline cache revision to 290.

## Development Version 289 — OBS source Motion Blur frame budget

- Audited the OBS source path separately from the editor and found that authored Motion Blur samples were still treated as a minimum: fast motion could silently escalate an 8-sample effect to 32–40 GPU draws, while animated Trim Paths/text geometry triggered several full CPU raster-and-upload cycles on the OBS graphics thread.
- Added an explicit `realtime_output` contract to live title-source and Stinger render sessions. Editor, cache and offline sessions retain their authored quality behavior; only real-time output is constrained by the graphics-thread budget.
- Changed adaptive exposure selection from hidden escalation to a hard authored ceiling: useful motion density may reduce the sample count but can no longer raise it above the configured Samples value.
- Added resolution-aware source limits. Transform-only exposure is capped at 6/4/3 samples for ordinary/large/UHD canvases; complete GPU temporal passes are capped at 4/3/2; CPU-raster temporal passes such as animated Trim Paths are capped at 3 samples below 3 MP and 2 samples above it.
- Historical source-raster samples now render at 0.625× below HD-class canvases, 0.5× at HD/QHD, and 0.375× at UHD. The authored current frame remains full-resolution and is still composited sharply over the normalized trail.
- Replaced the global “has any keyframes” temporal predicate with shutter-interval change detection. Properties animated elsewhere in the title no longer force a complete per-sample rerender during a locally static exposure.
- Applied the same authored-ceiling and real-time sample-budget rules to the compatibility Motion Blur path.
- Bumped the GPU pipeline cache revision so pre-289 resident payloads cannot bypass the corrected source contract.

# v0.8.12-alpha — Development Version 288

## Development Version 288 — Motion Blur render-time performance audit

- Traced the sudden OBS **Average time to render frame** increase to the Development Version 286/287 Motion Blur path: ordinary position/scale/rotation motion recursively rendered the complete layer, effects and transitions for every shutter sample and then performed another full-canvas accumulation pass per sample.
- Added a transform-only GPU path that evaluates the already effected local texture once and reuses it across shutter samples while still sampling hierarchy, 2D/3D transform, camera, visibility, wipe and general-transition state at each shutter time. The trail is drawn first and the sharp frame source-over directly in the caller target, eliminating auxiliary sharp/trail targets and a fullscreen resolve for this common path.
- Restricted complete temporal rerendering to content that actually changes below Motion Blur, including animated Trim Paths/source geometry, active text transitions, ticker output, active transition blur and screen-space effects. Dormant in/out transition descriptors no longer force the expensive path throughout the title.
- Reduced the complete-pipeline budget to 6–10 samples depending on content/projection, with a maximum of 4 in editor draft mode; the larger adaptive budgets remain available only to the inexpensive transform-only path.
- Replaced read-previous/write-next full-canvas ping-pong accumulation with normalized in-place additive accumulation in one persistent render target, while retaining the Development Version 287 trail-under-sharp resolve and premultiplied-alpha safeguards.
- Kept video transform Motion Blur on the current decoded frame unless another active temporal effect requires full sampling, avoiding repeated decode/raster work inside one OBS frame.
- Advanced runtime/build and GPU cache identity to 288 and added regression contracts for fast-path selection, active-transition gating, bounded dynamic sampling and in-place accumulation.

# v0.8.12-alpha — Development Version 287

## Development Version 287 — normalized Motion Blur trail resolve

- Replaced the Development Version 286 additive sharp-plus-trail accumulation with a two-stage resolve: shutter samples first form a normalized premultiplied-alpha exposure, then the untouched current frame is composited over it.
- Prevented overlapping temporal samples from increasing RGB or alpha on the current object, eliminating the visible stacked-copy/brightening artifact while preserving the authored fill, stroke and effect appearance.
- Retained complete per-sample evaluation of Trim Paths, text/general transitions, layer-space effects, projected/screen-space effects, hierarchy and camera movement.
- Applied the same normalized trail-under-sharp contract to the GPU session path, GPU readback compatibility path and Cairo fallback path.
- Advanced runtime/build and GPU base-raster cache identity to 287 and added regression coverage for normalized weights, source-over resolve ordering and non-additive compatibility compositing.

# v0.8.12-alpha — Development Version 286

## Development Version 286 — opaque post-effects Motion Blur

- Replaced the main GPU Motion Blur crossfade with full-strength sharp-frame retention plus a separately weighted temporal trail, preventing opaque layers from becoming semi-transparent when Motion Blur is enabled.
- Moved temporal accumulation after the complete per-layer visual pipeline: every shutter sample now evaluates layer-space effects, projected/screen-space effects, general transitions, visibility/opacity and transform/camera state before it is accumulated.
- Added time-sampled base-raster regeneration for geometry/source changes, including animated Trim Paths and transition-managed Text Animators, so reveals and text transitions produce their own motion exposure instead of blurring one frozen raster.
- Added isolated nested temporal targets for source-aware effects, preventing an effect-source layer with Motion Blur from clearing an outer layer's in-progress accumulation.
- Preserved premultiplied-alpha constraints in the temporal composite shader and advanced the runtime/build and GPU text/base-raster cache revision to 286.
- Added a Development Version 286 regression contract covering opacity retention, post-effect ordering, Trim Paths/text-transition sampling and nested target isolation.

# v0.8.12-alpha — Development Version 285

## Development Version 285 — Trim Paths base-raster live invalidation fix

- Fixed the remaining stale Trim Paths editor/live output by preserving Trim Paths effects in the reusable GPU base-raster cache identity.
- Ordinary post-raster pixel effects are still removed from the base-raster key, while the geometry-stage Start, End, Trim Offset, multiple-shapes mode, enabled state and animation timing now invalidate the exact stroke raster immediately.
- Restored dynamic-raster classification for animated Trim Paths properties, including updates at the current playhead without requiring a layer move, effect toggle or reload.
- Advanced the runtime/build development version and GPU text/base-raster cache revision to 285.
- Added a regression contract preventing Trim Paths from being removed by the base-key effect filtering step.

# v0.8.12-alpha — Development Version 284

## Development Version 284 — Stroke Offset popup MSVC capture fix

- Fixed the Stroke Options popup build failure on MSVC by explicitly capturing the shared `local_time` callback in the outer popup lambda.
- Preserved the nested Stroke Offset value-change and keyframe callbacks, which now access the captured timeline-time provider legally on all supported C++17 compilers.
- Added a source contract check preventing this lambda-capture regression.
- Advanced the runtime/build development version to 284.

# v0.8.12-alpha — Development Version 283

## Development Version 283 — Trim Paths live refresh and general Stroke Offset

- Fixed stale Trim Paths previews/output by adding evaluated Start, End, Trim Offset and Trim Multiple Shapes values to the rendered effect-layer cache key.
- Moved Stroke Offset out of the Trim Paths effect and into the layer's general stroke settings.
- Made the general Stroke Offset animatable, serializable, preset-aware and available in the stroke options popup and timeline.
- Applied Stroke Offset geometrically before optional Trim Paths processing for shapes, plain/rich text and text-background strokes.
- Added automatic migration of Development Version 282 Trim Paths Stroke Offset values/keyframes into the layer-level setting.
- Updated cache fingerprints, animation detection and render/live-cue bounds for static and animated stroke offsets.
- Advanced the runtime/build development version and GPU text cache revision to 283.

# v0.8.12-alpha — Development Version 282

## Development Version 282 — Trim Paths and geometric Stroke Offset

- Added a built-in **Trim Paths** effect with animatable Start, End, Trim Offset and Stroke Offset properties plus Simultaneously/Individually subpath processing.
- Implemented arc-length path slicing with wrap-around and adaptive Bézier flattening before stroke rasterization rather than masking already-rendered pixels.
- Added geometric positive/negative stroke-path offsetting for open, closed and compound paths, including winding-independent outward detection for closed contours.
- Applied the geometry modifier to shape outlines, ticker/plain text outlines, exact rich-text glyph strokes and text-background strokes while preserving original fill and inside/outside clipping geometry.
- Routed affected layers through the exact compatibility path so editor preview, live output and cached frames share the same generated geometry.
- Added full keyframe, Undo/Redo, preset, serialization, bounds-expansion and cache-key support.
- Advanced the runtime/build development version and GPU text cache revision to 282.

# v0.8.12-alpha — Development Version 281

## Development Version 281 — continuous static strokes and restored text transitions

- Removed logical cluster-advance clipping from ordinary exact rich-text glyphs, so italic bearings, swashes, accents and outside strokes are no longer cut by an invisible neighbouring character box.
- Composed all unclipped glyph outlines that share a stroke style into one painter path, eliminating dark overlap seams and hard vertical cuts between adjacent static glyph strokes.
- Retained bounded, stroke-expanded clipping only for genuine split ligatures or mixed-paint clusters where one shaped outline must be divided across independent source styles.
- Restored transition-managed and manually animated stroked text to the unified GPU glyph animator pipeline instead of the flattened compatibility adapter, preventing neighbouring glyph pixels from being captured and transformed by overlapping rectangular unit crops.
- Added an exact isolated-unit compatibility adapter for backend/font fallbacks, so each animated cluster is rerendered from its own immutable glyph paths and retains exact H/V Scale without borrowing pixels from adjacent characters.
- Advanced the runtime/build development version and GPU text cache revision to 281.

# v0.8.12-alpha — Development Version 280

## Development Version 280 — continuous stroked-text fallback

- Routed text layers with any evaluated rich-text stroke back through the compatibility text raster instead of the per-glyph GPU SDF raster.
- This removes the remaining visible cuts at glyph boundaries on thick or adjacent strokes, because the compatibility path composes the shaped text before applying the outline.
- Kept the Development Version 279 GPU text performance improvements for non-stroked text, so ordinary text, clock and ticker layers still use the fast GPU pipeline.

## Development Version 279 — stroke-safe text rendering, adaptive performance audit and documentation consolidation

- Separated fill and stroke clipping so outside strokes are not cut at glyph, ligature or rich-text style boundaries.
- Expanded text surfaces and text-box clip envelopes with a dedicated stroke sampling guard.
- Reused thread-local Euclidean distance-transform workspaces for glyph SDF generation.
- Made adaptive preview reuse the bounded immutable layout cache and stopped reduced-resolution timing from overwriting the full-quality Auto reference.
- Removed the fixed 100 ms adaptive editor cadence.
- Added adaptive glyph-atlas coverage and a bounded 512-pixel coverage envelope for very large full-resolution glyphs while preserving logical geometry and SDF stroke units.
- Changed paint-slice lookup from a complete paint-run scan per cluster to an ordered overlap search.
- Consolidated the Development Versions 276–278 text audits into the canonical guides.
- Updated README for changes since Development Version 239, started `v0.8.12-alpha`, and advanced the GPU text cache revision to 279.

# v0.8.11-alpha — Development Version 278

## Development Version 278 — post-scale text alignment and manual auto-size override

- Rebased center and right alignment on the final canonical line width after rich-text H Scale.
- Implemented post-scale terminal alignment for Justify Last Left/Center/Right and post-scale whitespace expansion for Justify All.
- Kept visible glyphs, caret, selection and run clipping on the same aligned immutable layout.
- Made manual canvas bounding-box width/height edits disable only the corresponding text-box auto-size axis.
- Advanced the GPU text cache revision to 278.

# Development Version 276

- Audited the complete text model, editor adapter, Undo/Redo, shaping, atlas, GPU geometry, clipping and cache pipeline.
- Removed character H/V Scale from `QFont` matching in the canonical GPU path.
- Added deterministic per-cluster horizontal geometry and per-glyph vertical geometry for ordinary and multistyle text.
- Fixed Horizontal Fit so it scales emitted glyph quads together with advances, clusters, cursors and clips.
- Preserved scale-only rich-text boundaries and mapped glyphs to canonical UTF-8 clusters.
- Enforced canonical adapter provenance so auto/evaluated formatting cannot become manual ranges.
- Made UTF-8 conversion explicit at text model boundaries.
- Consolidated text-property history into title-level transactions and adapter replacement on restore.
- Advanced the GPU text cache revision to 276.

# v0.8.11-alpha — Development Version 275

## Development Version 275 — canonical multistyle rich text and Undo/Redo

- Made the normalized `RichTextDocument` the only authored static source for text properties; layer scalar fields are compatibility mirrors.
- Split GPU shaping extraction by canonical effective style span, so rich-text H/V Scale and all concurrent properties reach the correct glyphs even when Qt coalesces `QGlyphRun` objects.
- Preserved overlapping/multiple sparse properties within one text box.
- Prevented stale inline QTextDocument state from overwriting restored Undo/Redo snapshots.
- Added inline history routing: native typing undo first, title snapshot history when the local document has no matching step.
- Advanced GPU text pipeline cache revision to 275.

# v0.8.11-alpha — Development Version 274

## Development Version 274 — unified GPU H/V Scale route and 90% threshold fix

- Removed the transition-managed text branch that sent otherwise supported text through the `QTextDocument` compatibility raster.
- All Text, Clock and Ticker layers now use the same immutable-layout/SDF glyph path whenever the GPU backend is available, including transition-managed Text Animator stacks.
- Preserved independent per-glyph H/V Scale for every effective rich-text range, including multistyle text boxes.
- Expanded GPU text allocation and clipping by the actual anisotropic glyph overhang, preventing V Scale above 100% from being clipped to the unscaled text-box envelope.
- Removed the compatibility raster's `H/V -> QFont::stretch` conversion that triggered platform font-width matching jumps around values such as V Scale 89%/90%.
- Added a continuous painter-space X residual for uniform H/V ratios in the compatibility path and a GPU text pipeline cache revision to invalidate stale rasters.

# v0.8.11-alpha — Development Version 273

## Development Version 273 — GPU text H/V Scale pipeline rewrite

- Effective H/V Scale is resolved at each glyph's logical rich-text cluster and stored in the immutable glyph record.
- The persistent atlas now stores one authored-size glyph outline; scale is no longer baked into or keyed by a potentially coalesced Qt glyph run.
- GPU quads apply H and V Scale directly to baseline-relative bearings, glyph dimensions and SDF padding.
- H Scale therefore changes actual glyph width as well as shaped advances; V Scale changes actual glyph height above and below the baseline.
- SDF padding is cropped independently on X and Y after anisotropic scaling.
- Multistyle text boxes can mix different H/V values inside one Qt glyph run without losing scale.
- The QTextDocument compatibility and editor-overlay routes use compensated vertical font sizing and H/V stretch, preserving correct output if the GPU route is unavailable.

# v0.8.11-alpha — Development Version 272

## Development Version 272 — true per-glyph H/V Scale on canvas

- Removed the font-size/compensating-stretch workaround that made H Scale look like tracking and corrupted V Scale above 100%.
- H Scale now controls shaped advances directly, while both H and V Scale are applied to each glyph outline around its baseline.
- The GPU atlas rasterizes transformed vector glyph outlines, preserving correct bearings and crisp output at extreme values such as H 70% / V 413%.
- Line envelopes, vertical alignment and split-ligature clipping now use the scaled glyph bounds.
- Effective scale remains per rich-text run, so multistyle text boxes can mix different H/V values without cross-run distortion.

# v0.8.11-alpha — Development Version 271

## Development Version 271 — exact anisotropic H/V text scaling on canvas

- Routed visible text runs whose effective H Scale and V Scale differ through the same Qt rich-text raster path used by the editor preview, eliminating backend-dependent QRawFont/SDF geometry mismatches on the canvas.
- Added per-range effective-format detection at all rich-text boundaries, so mixed-scale multistyle text boxes select the exact path whenever any visible style run uses independent H/V scaling.
- Kept the GPU glyph-atlas path for text whose H/V scale is uniform, preserving normal GPU rendering performance where its geometry is deterministic.
- Covered the reported 70% H Scale / 413% V Scale case and synchronized CMake/runtime metadata to Development Version 271.

# v0.8.11-alpha — Development Version 270

## Development Version 270 — canvas and multi-style character scaling fix

- Replaced backend-dependent `QRawFont::alphaMapForGlyph()` transform scaling with explicit horizontal resampling of normalized glyph coverage before SDF atlas generation.
- Applied the same rounded `QFont::stretch` ratio to per-run glyph ink bearings and widths, keeping atlas placement aligned with QTextLayout advances.
- Kept H/V scaling isolated per rich-text shaping run so multi-style text boxes can mix independent character scales without overlap or geometry leakage between runs.
- Preserved the unified Text Properties/Text Style editor controls introduced in Development Version 269.
- Synchronized CMake and runtime build metadata to Development Version 270.

# v0.8.11-alpha — Development Version 269

## Development Version 269 — unified text properties and corrected glyph scaling

- Replaced the separate Text Style edit form with the same `PropertiesPanel` implementation used for text layers.
- Unified Character, Type, Paragraph, Fill and Stroke controls in appearance and behavior, including drag-label numeric editing.
- Added local undo/redo history to Text Style editing through the shared Properties history controls.
- Enlarged the Text Style preview and made it resize with the dialog splitter.
- Rasterized independent H/V character scale directly into GPU atlas glyphs, so H Scale changes glyph width rather than behaving like tracking and V Scale remains stable above 100%.
- Synchronized CMake and runtime build metadata to Development Version 269.

# v0.8.11-alpha — Development Version 268

## Development Version 268 — MSVC text-style stroke clamp build fix

- Fixed two MSVC C2672 build errors in `style-presets.cpp` by converting both stroke-alpha expressions and clamp bounds to an unambiguous `double` type.
- Retained all Development Version 267 text-style, stroke, glyph-scaling and Properties history changes.
- Synchronized CMake and runtime build metadata to Development Version 268.

# v0.8.11-alpha — Development Version 267

## Development Version 267 — Editable text styles, stroke, character scaling and Properties history

- Added stroke data to text style presets, including enable state, width, opacity, alignment, join, front/back order, antialiasing, solid color and gradient settings.
- Added an Edit action for text styles. The editor exposes character, paragraph, text-box, fill and stroke properties with a live preview and preserves the preset identity when saving changes.
- Applied text styles consistently to rich-text character and paragraph formatting, including stroke and the extended OpenType/text options.
- Corrected GPU glyph raster geometry so H Scale changes glyph width instead of behaving like tracking, and V Scale remains independent above and below 100%.
- Added Undo and Redo controls to the Properties panel, synchronized with the editor’s canonical undo stack.
- Synchronized CMake and runtime build metadata to Development Version 267.

# v0.8.11-alpha — Development Version 264


## Development Version 264 — Play Every Frame audio varispeed and cache UI hiding

- Changed `Play Every Frame` so editor audio remains audible and is varispeeded to the visual-frame cadence instead of being muted/skipped.
- Kept `Skip Frames` as the default realtime audio-master mode.
- Hid cache-only controls, buttons, status, and diagnostics from Playback and Cache when cache is disabled.
- Synchronized CMake, runtime build info and serialization development metadata to Development Version 264.

## Development Version 263 — Editor Audio Sync and GUI Gap Audit

- Removed the elastic bottom spacer from the headphones-only Editor Audio dock so the stereo dB meter consumes the available dock height without a blank tail below the levels.
- Audited comparable dock/panel bottom stretchers and removed the matching bottom spacer from Playback and Cache; horizontal row spacers and intentional toolbar/list stretchers were retained.
- Added an A/V sync dropdown to Playback and Cache: `Skip Frames` (default) and `Play Every Frame`.
- In `Skip Frames`, editor playback can follow the live editor-audio media clock so slow visual rendering skips frames rather than letting audio drift.
- In `Play Every Frame`, the editor shows every visual frame and pauses/skips editor audio monitoring/levels to prevent slow-frame audio desync.
- Synchronized CMake, runtime build info and serialization development metadata to Development Version 263.

# v0.8.11-alpha — Development Version 262

## Development Version 262 — Scene Mask Shape Preview and Audio Meter

- Fixed Shape/SolidRect/ColorSolid scene-mask editor previews so the fill placeholder appears immediately when Scene Mask is enabled, without requiring a later effect edit to invalidate the raster cache.
- Kept the placeholder restricted to the mask fill while compositing the authored shape stroke into the same preview raster, so the stroke remains visible and the layer effect stack processes fill and stroke together.
- Added a visual stereo dB meter to the Editor Audio dock, fed by the private OBS editor-preview source audio levels while keeping the control itself headphones-only.
- Synchronized CMake, runtime build info and serialization development metadata to Development Version 262.

# v0.8.11-alpha — Development Version 261

## Development Version 261 — Editor Scene Mask, Clock Presets and Audio Monitor Dock

- Restricted editor scene-mask placeholders to the mask fill/silhouette only, so authored strokes stay out of the placeholder preview and scene-mask toggles repaint immediately through the editor GPU preview path.
- Composited scene-mask strokes into the matted scene before applying the mask layer's effect stack, so the same effects now affect both the scene fill and the stroke.
- Simplified Add Blank Title by removing the graphic-type chooser; blank titles now start as element-defined graphics, with Stinger setup remaining in Graphic Properties.
- Removed the redundant Character section title for Clock and Ticker layers, added Clock date/time format presets with a Custom mode, and exposed the selected canvas background in the Background button text.
- Added an Editor Audio Monitor dock with a headphones-only control backed by the private OBS editor-preview source monitoring path.
- Synchronized CMake, runtime build info and serialization development metadata to Development Version 261.

# v0.8.11-alpha — Development Version 260

## Development Version 260 — Effect Library Cleanup and Textured Damage Effects

- Removed duplicate effect/preset entries at catalog load time by normalizing category paths and display names before inserting items into the Effects & Presets tree.
- Emptied the Animation Presets root so shipped animation preset entries no longer appear in the editor library.
- Rebuilt Film Distortion, Analog Distortion and Digital Distortion around packaged artifact texture maps and bound those real textures to the GPU shader at render time.
- Synchronized CMake, runtime build info and serialization development metadata to Development Version 260.

# v0.8.11-alpha — Development Version 257

## Development Version 257 — Scene Masks on All Visual Layers

- Restored Scene Mask availability for all visual layer types, including Text, Clock, Ticker, Image, Video, Shape, SolidRect, ColorSolid, Group/Asset and TransitionInput layers, while keeping non-visual Audio/Adjustment rows out of the scene-mask contract.
- Replaced scattered Image/Video/Video-only exclusions with a single `layer_type_can_be_scene_mask()` helper used by serialization, editor UI, dock status, source properties and the OBS render path.
- Preserved legacy and newly-authored Image/Video scene-mask flags on load instead of clearing them during deserialization or title-type detection.
- Added decoded-frame cache invalidation for Video scene-mask auxiliary rasters so video mattes update frame-accurately in live output.
- Reworked editor scene-mask placeholder rendering for Text, Clock, Ticker and bitmap/video-like layers so the placeholder texture is masked by the object's actual alpha silhouette rather than a rectangular layer box.


## Development Version 254 — Live Properties Layout and Defaults Icon Cleanup
- Removed duplicate switch labels from Live Properties rows; the form label is now the only row label.
- Added spacing around Appearance swatches so Fill/Stroke color chips are not visually attached to labels.
- Made Fill/Stroke exposure participate in live cue columns and added color-chip/reset controls for color-only cue columns.
- Scene Mask layers no longer offer Fill exposure, and enabling Scene Mask clears existing Fill exposure state.
- Converted panel header Defaults controls to icon-only buttons with a mono reset glyph.
- Hardened property combo popup styling so dropdowns are opaque and aligned.

## Development Version 253 — Properties Defaults, Live Cue and GUI Fixes

- Moved visual-effect Defaults to the effect header menu and disabled duplicate generic Defaults buttons inside effect panel headers.
- Converted panel-header Defaults to compact icon buttons and kept reset handlers wired for Transform, Shape, Live, Image/Video, Audio, Asset and other property sections.
- Added clear actions for Image/Video/Audio source rows and live image cue rows.
- Removed Scene Mask availability from Image layers and invalidated legacy Image scene-mask flags on load.
- Added Fill/Stroke live-dock exposure toggles, optional single-value toggles, color selector chips and reset buttons in live cue rows.
- Fixed property combo popup styling so menus are opaque, padded and correctly selected instead of visually overlapping adjacent controls.
- Stabilized the Libraries dock width when resizing outer dock edges and forced guide overlays to invalidate/repaint while dragging.

## Development Version 251 — GPU Image Filtering and Box Size UI

- Applies image/video filtering in the GPU layer-copy shader for direct image/video layers, including layers without effects.
- Avoids CPU resample/texture-upload churn during interactive Box size changes by keeping direct source textures resident and updating only layout metadata.
- Moves Box size into the Image Source/Video Source section and labels it “Box size”.

# v0.8.11-alpha — Development Version 247

## Development Version 247 — Editor Video Decode Cadence Fix

- Fixed the initial blank Video layer preview/bounding-box behavior by refusing to cache an empty async video raster under the requested GPU layer key.
- Fixed stepped editor video playback where the render loop could cache the previous decoded frame as if it satisfied the current playhead frame. Video GPU rasters are now keyed by the delivered decoded frame number.
- Added explicit `requested_frame_number` / `exact_requested_frame` metadata to decoded Video frames so the editor can distinguish exact hits from safe stale fallback frames.
- Restored a steady-playback editor prefetch window while preserving small request-coalesced prefetch for scrubbing, jumps, reverse movement and freeze sections.
- Retained the 246 rollback of preview-sized decode: editor video remains full-resolution and software-first when Auto hardware decode would require readback.

## Development Version 246 — Editor Video Decode Stability Fix

- Reverted the 245 preview-sized video decode approach that degraded editor preview quality and could make scrubbing worse by changing decode dimensions.
- Editor preview now marks its render session as the Editor video decode client while preserving full-resolution decoded frames and the existing image/video raster quality path.
- When hardware decode is set to Auto, editor preview now uses software decode first because the current QImage upload path requires CPU readback from D3D11VA/DXVA/VAAPI/VideoToolbox frames; live output keeps the hardware Auto path.
- Reduced editor prefetch to a small nearest-frame window so a single FFmpeg worker prioritizes the newest playhead request instead of decoding stale look-ahead/look-behind frames during scrubbing.
- Retained the Development Version 244 serialization/migration audit and MSVC hotfix.

## Development Version 244 — Serialization and Migration Audit

- MSVC hotfix: declared the animated scalar default helper before the 244 schema migration routine so translation units including `title-serialization-schema.h` compile cleanly.
- Audited the persisted envelope for layer effects, external plugin effects and Video layers. Effects now round-trip stable `effect_id`/`extension_id`, schema/runtime versions, parameters, keyframes, presets, plugin identifiers, opaque binary state and missing-plugin placeholder state.
- Missing external plugins now reopen as inert placeholders instead of deleting plugin state; reinstalling the plugin can recover the preserved parameter/keyframe/binary payload.
- Video layers now round-trip authored relative paths, resolved absolute paths, media roots, fingerprints, stream selection, color interpretation, proxy references, timeline trims, time-remap state, audio stream choices and decode/cache settings. Offline media remains diagnostic-only and never blocks title load.
- Built-in Glow/Noise and other older built-in effect records no longer reset to current defaults just because descriptor schema versions changed. Revised implementations are used only after explicit successful migration.
- Added a persistence safety guard so previous-version projects are written in the new format only after the title store has loaded or cleanly initialized successfully.
- Synchronized build metadata, migration continuity and source contracts to Development Version 244.

## Development Version 243 — Video Time Remapping and Frame Interpolation

- Added Video source-time as a first-class keyframeable animation track with Hold, Linear and Bezier timing, speed-ramp support, reverse/freeze sections and loopable source segments.
- Exposed source-time in the existing Graph Editor with source-time and speed views; negative speed is surfaced as an explicit reverse-time indication instead of being hidden behind ordinary timeline playback.
- Updated the Video runtime to resolve decoded media frames through the authored time-remap curve, preserving continuous speed-ramp boundaries and avoiding redundant decode work for freeze sections.
- Added frame interpolation modes: nearest frame, frame blend and optional motion-compensated interpolation. Motion-compensated mode uses background-only optical-flow analysis/cache manifests and falls back to draft preview when analysis is unavailable.
- Connected linked Video audio to the selected remap audio policy: preserve linear clip audio, follow source time, or mute reverse/freeze regions.
- Invalidate optical-flow analysis only when source media or the time-remap/interpolation fingerprint changes; transform-only and non-baked-effect edits do not poison the analysis cache.
- Synchronized build metadata, migration continuity and source contracts to Development Version 243.

## Development Version 242 — Video Proxy, Decode Cache and Hardware Acceleration

- Integrated Video layers with the runtime proxy/cache pipeline through source-media fingerprints, disk-only proxy sidecar manifests, automatic proxy relink and source-only invalidation. Transform changes and non-baked effects no longer poison proxy identity.
- Added background proxy generation using external ffmpeg when available, with per-layer progress, per-title aggregate progress, pause/resume, cancel, alpha/HDR-capable proxy profiles and audio stream copy preservation.
- Reworked the decoded-frame cache around the playhead with separate editor/live budgets, active request priority, cache-aware scrubbing and forward/reverse prefetch. Layer/media replacement, deletion, title cache removal and shutdown release runtime decode/proxy state.
- Added a platform hardware-decoding abstraction with safe software fallback, codec/profile fallback, hardware-frame transfer to the existing QImage upload path and non-fatal decoder failure handling.
- Synchronized build metadata, migration continuity and source contracts to Development Version 242.

## Development Version 240 — Inspector Widget Unification

- Added shared Transform-panel-derived control styling for all editor inspector/effects sections, including numeric fields, combo boxes, text fields, buttons, tool buttons and sliders.
- Normalized common panel widgets to the Transform panel's compact 20 px control height, 10 px font sizing, 2 px border radius, 12 px spin-button width, theme-aware hover/focus colors and consistent icon-only button sizing.
- Applied the shared metrics automatically when any collapsible inspector panel or effect/audio-effect panel section is created, including dynamically generated effect settings and header actions.
- Updated Properties and Effects panel control-style helpers to use the same source of truth instead of separate per-panel QSS fragments.
- Synchronized build metadata, migration continuity and source contracts to Development Version 240.

## Development Version 239 — Motion Blur Posterization Fix and Organic Damage Effects

- Fixed Motion Blur posterization on Image and Video layers by giving bitmap/video shutter accumulation a much denser temporal sample budget while keeping text/vector paths on the lower real-time budget. This keeps moving footage from breaking into separated ghost frames instead of a continuous exposure.
- Rebuilt the Film/Analog/Digital Distortion shader as layered organic artifact logic rather than a simple noise-style overlay. Film now layers gate weave, flicker, vertical scratches, hair/fiber lines, dust blobs, emulsion grain and blotches.
- Reworked Analog Distortion around VHS/TV-style YIQ chroma bleed, tracking drift, head-switching bottom warp, interlacing, scanlines, row dropouts and fine RF/static noise.
- Reworked Digital Distortion around macroblock quantization, block/row packet jumps, ringing around block boundaries, corrupted packet coloring and sparse sparkle noise.
- Added missing animatable metadata for damage direction/aspect and offset controls, keeping serialized effect IDs stable and preserving the existing fast Gaussian blur backend for all blur effects.
- Synchronized build metadata, migration continuity and source contracts to Development Version 239.

## Development Version 238 — Effect Pipeline Audit and Unified Damage Effects

- Audited the FXM effect path from descriptor/serialization through runtime parameter resolution, GPU shader compilation and compatibility surface rendering.
- Removed the broken dedicated `DamageMap` / `DamageComposite` branches that could be skipped or alias ping-pong render targets, and moved Film Distortion, Analog Distortion and Digital Distortion into the shared GPU technique selector as real artifact-composite `Draw` passes.
- Replaced the damage shader and embedded fallback with a single artifact shader that directly models film scratches/dust/weave/burn, analog scanlines/chroma tear/dropouts and digital macroblock/packet/quantization damage.
- Preserved the existing fast Gaussian blur backend for Blur/Glow/Drop Shadow/Bloom/Halation/Glare and sharpen low-pass variants; no blur path was replaced by a slow generic multi-pass implementation.
- Synchronized build metadata, migration continuity and source contracts to Development Version 238.


## Development Version 237 — Video Decode Look-Ahead and Damage Multi-Pass Effects

- Improved Video layer playback performance by removing decode queue side effects from frame-cache-key generation and adding a bounded forward look-ahead decoded-frame cache that aborts as soon as scrubbing/reverse/jump requests supersede it.
- Reworked Film Distortion, Analog Distortion and Digital Distortion from noise-style single-pass variants into a two-stage damage-map plus composite pipeline. Film now models weave/scratches/dust/burn, Analog models scanlines/chroma/tearing/dropouts, and Digital models macroblocks/packet glitches/quantization.
- Extended the FXM effects engine and compatibility GPU surface path to execute damage effects as explicit `DamageMap` and `DamageComposite` passes, with a single-pass `Draw` fallback for fail-open compatibility.
- Added dedicated damage-effect parameter metadata instead of reusing Noise/Grain metadata, and bumped the effect cache ABI for the new multi-pass output.
- Synchronized build metadata, migration continuity and source contracts to Development Version 237.


## Development Version 236 — Video Playback Regression Fix and Real Damage Shaders

- Fixed the remaining Video playback regression by restoring the safe forward-playback presentation fallback while keeping backwards scrubs/frame-step from showing future frames.
- Made mouse-trim Range refresh lighter again: live strip drags update the selected Range controls immediately while deferring linked audio stream synchronization, audio preview rebuilds and cache invalidation until mouse release.
- Split the previous Noise workflow into separate `Noise` and `Grain` built-in effects: Noise is now procedural/noise-map oriented, while Grain is film/sensor texture oriented.
- Removed the duplicate cosmetic v234 effect-preset entries and replaced them with real built-in damage effects.
- Replaced the first Film/Analog/Digital distortion implementation with a dedicated damage-distortion shader path instead of piggybacking on the Noise shader. Film damage now uses weave, scratches, dust, flicker and burn; Analog damage uses scanlines, horizontal tear, chroma offset and dropouts; Digital damage uses macroblocks, packet glitches, jumps and quantization.
- Removed damage profiles from the Noise/Grain profile menu and exposed damage-specific controls such as Damage, Artifact Size, Blend/Smear, Density, Element Spread, Damage Falloff and damage colors.
- Updated the GPU runtime, embedded shader fallback, cache invalidation, live-cue bounds, hierarchy rows and effect browser categories for the new Noise/Grain/Distortion types.
- Synchronized build metadata, migration continuity and source contracts to Development Version 236.

## Development Version 234 — Range Inspector Cleanup, Reverse Video Stability and Effect Animation Presets

- Simplified Video layer Range properties to one compact row with `In`/`Out` prefixes inside the numeric fields. Removed the visible Source row, Preview row, Set In/Out buttons and feedback line from the Video inspector.
- Added realtime timeline strip timing notifications so Range In/Out updates immediately while trim handles are dragged, not only after mouse release.
- Improved video reverse stepping and backwards scrubbing by expanding the decoded-frame LRU and preventing stale future frames from being presented while an older requested frame is still decoding.
- Fixed Space playback start behavior in Pause/Loop playback zones so it respects the Playback and Cache start setting instead of always jumping back to the beginning.
- Added new effect entries and animation presets: Animated Noise Drift, Glare Sweep, Ripple Loop, Wave Warp Loop, Chromatic Pulse, Soft Bloom Highlight, Cinematic Halation Warm and Micro Contrast Clarity.
- Unified effect settings sizing and labels with the compact Transform-property control style.
- Synchronized build metadata, migration continuity and source contracts to Development Version 234.

## Development Version 233 — Media Range Preview and Modular Visual Effects SDK

- Added Preview In/Out labels above Video and Audio Range controls, keeping the selected source media range locked to the layer strip duration.
- Editing Range In now pushes Range Out by the strip length; editing Range Out now pulls Range In by the strip length, with duration clamping for finite media.
- Timeline strip trimming now updates the corresponding media range edge for Video and Audio layers: dragging the start changes source In, dragging the end changes source Out.
- Added the public Modular Visual Effects SDK foundation: append-only ABI v4, manifest and native plugin descriptors, stable IDs, parameter metadata, custom host-owned widgets, GPU shader/multi-pass metadata, worker-only CPU declarations, color-space/alpha/input contracts, auxiliary input/layer-reference metadata and state serialization/migration hooks.
- Added predefined plugin search roots, environment-path discovery, off-UI-thread scanning, quarantine/blacklist persistence, plugin crash-report JSON, scan diagnostics, safe unload callbacks and Effects-browser controls for Rescan plugins and Clear quarantine.
- Added a developer guide and sample effects under `docs/visual-effects-sdk.md` and `sdk/visual-effects/`.
- Synchronized build metadata, migration continuity and source contracts to Development Version 233.

# v0.8.11-alpha — Development Version 232

## Development Version 232 — Scene-mask placeholder, FX-strip and media range polish

- Scene-mask placeholders are always rendered in the editor, follow the actual layer content shape/corners instead of a rectangular overlay, and continue through the normal effect stack while OBS/source output keeps real mask artwork only.
- Timeline FX strips now appear only for effects that have authored keyframes, and expose only keyframed properties/channels below the strip.
- Added Properties-panel Video range controls with Set In/Set Out playhead buttons and live range feedback; Audio layer Range controls remain available for audio clips/streams.
- Synchronized build metadata, migration continuity and source contracts to Development Version 232.

# v0.8.11-alpha — Development Version 231
## Development Version 231 — Video Decode Efficiency, Frame Stepping and Scene-Mask Placeholders

- Added a small decoded-frame LRU to the asynchronous Video runtime so frame stepping, scrubbing and duplicated timeline frames can reuse nearby decoded frames without waking FFmpeg or regenerating uploads.
- Kept non-expanding Video effects such as color correction, keying, sharpen, pixelate and displacement on the direct image-like GPU base-raster path, avoiding the slow compatibility raster unless an effect needs expanded padding.
- Added editor Left/Right shortcuts for exact one-frame stepping on the timeline.
- Added Transform > Fit Screen and Transform > Fill Screen canvas context-menu actions.
- Reworked scene-mask editor previews to behave like Stinger A/B placeholders: a layer-colored grid background rendered as real layer artwork, not a magenta overlay, so the layer effect stack applies to the placeholder.
- Hotfix: limited the scene-mask grid placeholder to the editor preview session only; OBS/source scene-mask mattes now render the real mask artwork without leaking the grid into output.
- Hotfix: pruned stale decoded audio clips immediately when embedded video audio streams are removed from the title/editor, so removed streams cannot keep playing in the source.
- Hotfix: Video layers are no longer accepted as scene-mask layers through UI, serialization recovery or source scene-mask discovery.

# v0.8.11-alpha — Development Version 230
## Development Version 230 — Frame-Accurate Video Playback Audit

- Locked Video frame selection to project/timeline frame numbers instead of raw sub-frame timestamps.
- Added deterministic FPS mismatch handling: lower-FPS media duplicates frames and higher-FPS media drops frames by stable source-frame mapping, preventing drift in 23.976/25/30/50/60fps combinations.
- Updated the asynchronous Video runtime so frame requests, pending generations and cache keys use the mapped media frame rather than continuously changing playhead seconds.
- Kept decoded-frame presentation asynchronous while preserving the last valid frame only as a temporary preview until the requested mapped frame arrives.
- Audited Video playback paths for editor preview, source render, GPU cache keys, direct image-like rastering, trimming, looping and missing-FPS fallback.
- Fixed Dock cue toggling: clicking a yellow cue again finalizes the uncue and applies the configured end behavior without restarting playback from the beginning.

# v0.8.11-alpha — Development Version 229
## Development Version 229 — Video Performance, Stable Layer List Columns and 3D Refresh

- Optimized Video decoding for slow files by decoding through intermediate frames without converting each one to BGRA; only the selected target frame is converted and uploaded.
- Enabled FFmpeg frame/slice threading and fast seek flags for the asynchronous Video decoder.
- Made layer-list picture visibility and audio mute permanent fixed-width switch columns so Video, Audio and Group rows no longer shift controls when audio availability changes.
- Reduced the layer-list header to the user-facing data columns: Layer Name, Mode, Parent and Matte Source.
- Forced full 3D editor preview invalidation for 3D layer/camera transforms, preventing the 2D transform-only fast path from hiding updates until an unrelated refresh.
- Discarded stale GPU presentation targets when opening a title so existing 3D text layers render immediately.
- Revalidated canvas overlay invalidation so bounding boxes/gizmos refresh on every edit.

# v0.8.11-alpha — Development Version 228
## Development Version 228 — Video Layer Visual Effects and Playback Performance

- Removed the editor empty-image hatch overlay from Video layers.
- Video rows now accept visual/video effects and visual effect presets directly, while embedded streams keep independent audio effects.
- The asynchronous video runtime now quantizes requests to media frame numbers and exposes decoded-frame cache keys.
- GPU preview/cache updates are keyed by the actually decoded frame, preventing redundant uploads of the same frame while the decoder catches up.
- Image-like direct GPU rastering can now use decoded Video frames when no pixel effects force the full compatibility surface.

## Development Version 228 — Video Layer and Multistream A/V Synchronization

## Development Version 228 Hotfix — Video Playback, Embedded Audio and Waveform Progress

- Fixed Video playback stalling on blank frames by allowing the renderer to keep presenting the last decoded frame while the async decoder catches up to the requested title-clock time.
- Decoupled audio playback from waveform generation: decoded PCM clips are now published to the mixer immediately, and waveform analysis runs as a second phase.
- Video timeline rows now draw embedded audio-stream waveform lanes directly inside the Video strip, keeping picture and sound in one timeline item by default while stream controls remain available from the expanded layer hierarchy.
- Added realtime waveform progress state per audio stream, including the currently generating file/stream label in the footer and strip overlay.

- Added a first-class **Video** layer whose picture uses the complete Image layer geometry, crop, anchor, transform, mask, effect and 2D/3D presentation paths.
- Added FFmpeg media probing and asynchronous frame decoding outside the UI and OBS render threads. Video frames are requested from the absolute title transport and obsolete decode requests are dropped instead of allowing picture drift.
- Added one structural Audio child track for every embedded audio stream, preserving stream index, language/title metadata, waveform, gain, pan, mute, solo and audio effects independently.
- Added separate Video picture visibility and master audio mute controls, plus per-stream mute controls in the Layer List.
- Added waveform rows for every embedded stream in the Timeline. Linked stream rows cannot be moved, trimmed, grouped, copied or deleted independently from their owner Video.
- Synchronized Video moves, trims, media in/out, looping, seeks, reverse transport and duplication/paste with all linked streams through one title-clock contract.
- Added paused-preview frame-ready invalidation, source replacement that atomically rebuilds stream tracks, current-schema serialization/migration and missing-media diagnostics.

# v0.8.11-alpha — Development Version 226

## Development Version 226 — Effects UI, Presets and Animation

- Added source-aware **Light Wrap** with composition or layer background input, radius, intensity, edge width, spill color, foreground-luminance protection and alpha-aware edge extraction.
- Added **Displacement Map** with hidden-layer-safe source dependencies, independent X/Y channel selection, signed horizontal/vertical displacement, clamp/repeat/mirror/transparent wrapping and source-space or composition-space mapping.
- Rebuilt the Effects panel around searchable categories, Favorites, Recently Used, thumbnails, capability badges and a scalable effect browser.
- Added effect and complete-stack clipboard operations, stack preset save/import/export, replace, duplicate, effect reset, parameter reset and individual/whole-stack enable controls.
- Added a shared effect hierarchy for the Effects panel, Layer List and Timeline. Effect parent rows now own their parameter channels in exact stack order.
- Exposed meaningful numeric effect parameters to keyframing and the Graph Editor, kept colors as unified controls with component channels, and removed artificial angle/evolution wrapping.
- Marked source-aware and other time/composition-dependent effects as cache-breaking where required and preserved fail-open GPU rendering.

# v0.8.11-alpha — Development Version 225

## Development Version 225 — Keying, Matte and Spill Suppression

- Added Chroma Key, Luma Key, Color Range, Spill Suppression and Matte Choker as append-only built-in GPU effects.
- Added a shared OBS-compatible keying shader with luma/chroma color distance, premultiplied-alpha-safe output, key-color-neutral spill removal and bounded matte morphology.
- Added keyframeable editor controls, catalog manifests, stable IDs, Timeline channel labels, cache routing and fail-open GPU execution.
- Preserved the forward-only schema policy: no legacy modes are exposed, and changed effects reopen with current defaults.
- Added a shader contract that verifies every technique against its declared pixel entry point.

# v0.8.11-alpha — Development Version 224

### Development Version 224 effects shader-entrypoint hotfix

- Fixed OBS/D3D11 compilation of every shader introduced or rewritten in Development Versions 221–224. Noise, Detail, Real Glare, Halation and Finishing pixel entry points now use the OBS-compatible `VertDataOut v_in` signature expected by their technique invocations.
- Synchronized the installed shader assets and embedded fallbacks byte-for-byte so fallback compilation cannot reintroduce the same no-op behavior.
- Bumped both the live GPU-effect cache key and rendered-frame cache ABI to invalidate frames produced while the affected shaders failed to compile.
- Added regression coverage that validates every pixel-shader declaration against every technique invocation instead of checking only that technique names exist.

### Development Version 224 first-frame visibility hotfix

- Fixed a regression where an optional GPU text or primitive raster failure deferred the complete frame transaction forever, leaving both the editor and OBS source transparent.
- Optional accelerator failures now publish all ready layers immediately, retain the last valid layer texture when available, and force a complete compatibility-raster rebuild on the next update.
- GPU text parameter lookup is null-safe and a failed GPU text backend is disabled for the session instead of retrying an incomplete raster every frame.
- Added regression coverage for first-frame fail-open publication and compatibility fallback routing.

### Development Version 224 crash hotfix

- Fixed D3D11 compilation of the GPU text gradient shader by removing the reserved `point` identifier.
- Fixed OBS-compatible Noise shader entry-point signatures and pass formatting.
- Serialized GPU text effect render/reset lifetime and reject incomplete shader techniques.
- Fixed Qt 6.8 effect-property stylesheet substitution that left `%4` unresolved.

## Development Version 224 — Functional Effects Pipeline, Lens, Distortion and Finishing

- Fixed the MSVC build failure in the GPU effect no-pass diagnostic by storing dynamic error text in session-owned storage before exposing its `const char *` view.

- Repaired the complete built-in effects route from catalog discovery and parameter evaluation through auxiliary-texture generation, technique selection and GPU presentation.
- Sharpen, Unsharp Mask, High Pass, Clarity / Local Contrast and Bilateral Sharpen now receive the Gaussian low-pass texture required by their detail kernels.
- Real Glare now uses an independent source-driven optical shader with thresholded highlight extraction, streak shaping and chromatic dispersion instead of reusing Lens Flare.
- Halation now receives its own thresholded blur input and warm inner/outer spectral composite.
- Added fail-open technique execution: a shader or technique that produces no pass leaves the incoming layer intact instead of replacing it with an empty render target.
- Added Lens Distortion, Chromatic Aberration, Directional Blur, Zoom Blur, Radial Blur, Ripple, Wave Warp, Pixelate, Edge Detect, Posterize, Threshold and Scanlines as built-in GPU effects.
- Added catalog manifests, current-schema defaults, editor controls, serialization validation, cache ABI invalidation and embedded shader fallbacks for all Development Version 224 effects.
- Retained the forward-only built-in schema policy: changed effects reopen with current defaults and never expose legacy controls or render branches.

## Development Version 223 — Optical Bloom, Glare and Halation

- Restored discovery of the Development Version 222 detail effects by shipping their built-in catalog manifests.
- Fixed live Noise controls, profile switching, extended aspect range and stale GPU effect-cache invalidation.
- Added Real Glare and Halation as built-in optical effect types with current-schema defaults only.
- Development Version 224 replaces their initial incomplete routing with independent, functional optical passes.

## Development Version 222 — Convolution, Blur and Detail Core

- Fixed the standalone MSVC build of `effect-preset-catalog.cpp` by directly including `effect-runtime.h` before using `EffectDescriptor` and `effect_descriptor()`.
- Added Sharpen, Unsharp Mask, High Pass, Clarity / Local Contrast and Bilateral Sharpen as native GPU effects.
- Added the shared detail shader, low-pass auxiliary contract, alpha protection, luminance-only processing, thresholding, highlight/shadow protection and edge-aware sharpening.
- Reworked Noise into schema 3 with clearly separated Fine Grain, Film Grain, Digital Sensor, Clouds, Turbulence, Ridged, Cellular and Blue-noise profiles.
- Removed all legacy Noise options and fallback branches. Older Noise instances reopen with the current schema-3 defaults.
- Established the generic built-in policy that a changed effect schema resets stored instances to current defaults instead of carrying legacy parameters forward.

# Development Version 221 — Procedural Noise Engine

- Introduced the first procedural Noise runtime with deterministic seed/evolution evaluation, expanded spatial controls and GPU/cache integration.
- Added the initial grain, cellular, fractal and sensor-oriented profile families.
- Strengthened shader portability by replacing dynamic and nested loops with statically unrolled cross-backend implementations.
- Development Version 223 supersedes this implementation with schema 3, removes every compatibility mode and reopens older Noise instances with the current defaults.

# v0.8.11-alpha — Development Version 220

## Scene-mask backdrop compositing correction

- Restored affect-layers-behind effects for OBS scenes inserted through scene-mask layers, so Blur and other backdrop effects process the already-composited lower scenes instead of being skipped or applied only to the foreground scene.
- Snapshots the current OBS destination, maps it into title-local coordinates, evaluates each backdrop effect independently, and bounds the result with the untouched scene-mask silhouette before the scene artwork is drawn.
- Keeps the correction fail-open: an unavailable optional effect shader leaves the lower composition unchanged but never hides the selected scene.

## Startup crash correction

- Fixed an OBS startup crash in `gs_effect_get_param_by_name()` when an effect shader was unavailable or its handle was replaced during nested/group/matte rendering.
- Replaced session-owned mutable effect-pass scratch with a re-entrant invocation-local pass list using 16 inline slots and heap overflow only for unusually large stacks.
- Routed all split `title-source` parameter lookups through a null-safe wrapper, turning shader initialization failure into a graceful render fallback instead of an access violation.
- Serialized built-in and extension shader registry mutation so concurrent first-use compilation cannot publish or replace partially initialized cache entries.

## Build correction

- Added the required direct performance-counter include to the unified `title-source.cpp` translation unit, fixing MSVC lookup failures for `fxm::perf` in the compatibility and GPU effect modules.
- Removed release-build clock reads and timer state from the debug-only `ScopedTimer` instrumentation.

This release covers Development Versions 190–220 and introduces the complete planar 3D layer/camera workflow, animated camera and XYZ motion-path authoring, hardware depth and transparent compositing, keyframe-safe hierarchy changes, 3D masks/effects/motion blur, unified Vector3 Timeline/Graph Editor rows, performance/cache/threading audits, schema-6 migration recovery, the automated source/smoke/full/stress test suite, and a unified allocation-conscious effects runtime.

# Development Version 220 — Unified Effects Runtime and Render Performance Baseline

- Added one canonical `EffectDescriptor` registry for every built-in effect, including stable/legacy IDs, independent schema version, parameter metadata, execution space, GPU/CPU contract, HDR support, color and premultiplied-alpha behavior, minimum passes, cacheability and bounds expansion.
- Replaced per-frame `LayerEffect` copies with allocation-free `ResolvedLayerEffect` snapshots shared by compatibility and GPU compositor paths.
- Centralized time-variant detection, dirty scope and asymmetric effect-bounds expansion so cache, 2D and planar-3D paths no longer maintain conflicting rules.
- Replaced linear built-in shader lookup with an indexed first-use cache and retained stable-ID caching for external effects.
- Uses a re-entrant inline effect pass list and a dimension-aware compatibility surface pool containing the upload texture, ping-pong targets, staging surface and transfer buffers.
- Added debug counters for effect resolution, shader-cache hits/misses, bounds evaluation, pass count/time, resource-pool reuse and empty-stack fast paths.
- Exposed the canonical built-in capabilities and parameter schemas through the extension catalog.
- Preserved title schema 6, existing stable effect IDs and authored project appearance; Development Version 220 migration is a validated no-op.

# Development Version 219 — Automated Test Suite and Render Hot-Path Repair

- Fixed the Development Version 218 editor render-time regression caused by copying full opaque JSON payloads with every Title/Layer render snapshot.
- Replaced raw passthrough strings with immutable shared storage and kept replacement value-like through copy-on-write assignment.
- Removed passthrough JSON parsing/deep merging from `layer_render_fingerprint()` and other render-fingerprint work.
- Added a native hot-path regression test for multi-megabyte passthrough payloads and thousands of snapshot copies.
- Added a versioned test-suite manifest covering GUI, Timeline/Graph Editor, serialization, rendering, cache/threading, audio, cue persistence, external data, shutdown lifetime, and platform build contracts.
- Added deterministic `source`, `smoke`, `full`, and `stress` profiles with manifest validation, timeouts, fail-fast support, CTest integration, and JSON reporting.
- Advanced the runtime and migration ledger to Development Version 219 without changing title schema 6.

# Development Version 218 — Serialization and Migration Audit

- Reuses the complete Canvas layer context menu from normal Timeline layer rows and applies it to the synchronized selected-layer set.
- Preserves Timeline multi-selection on right-click and keeps keyframe, transition, property, camera, and camera-switch context menus specialized.
- Raises the authored title schema to version 6 and extends the contiguous development migration ledger through 218.
- Preserves unknown/newer JSON fields through actual model round-trips at title, layer, camera, animated-property, keyframe, effect, transition, audio-effect, proxy, and external-provider levels.
- Isolates malformed nested cameras, effects, transitions, audio effects, bindings, sources, and fields instead of rejecting the complete title.
- Disables malformed parent-bind matrices without baking or changing transform keyframes.
- Repairs missing/duplicate layer IDs, dangling/self hierarchy links, mask references, and group/transform-parent cycles deterministically.
- Adds bounded detailed migration/recovery logs while retaining atomic `QSaveFile` persistence and best-effort future-schema loading.

# Development Version 217 — Performance, Cache and Threading Audit

- Colors active Graph Editor X/Y/Z/W and A/R/G/B toggles with the same component colors used by their curves.
- Highlights the exact 3D gizmo axis, plane, or rotation ring under the mouse before click.
- Reuses projected gizmo geometry between hit-testing and paint and invalidates it with Canvas overlay state.
- Replaces per-frame cache-state scans with an indexed title/frame aggregate.
- Batches Timeline cache-state reads and static-frame visual hashing once per visible paint range.
- Adds debug counters for queue peak, render/readback duration, UI coalescing, cache indexing, Timeline inspection, gizmo cache efficiency, and active background jobs.
- Retains worker cancellation, dirty-region invalidation, asynchronous decode/render/write/readback, and shutdown resource-release contracts.
- Adds no authored fields; title schema version 5 and Development Version 216 parent-bind data remain compatible.

# Development Version 216 — 3D Pipeline Completion and Keyframe-Safe Parenting

- Replaces all sampled reparent baking with one static 4×4 parent-bind matrix evaluated at the parenting playhead.
- Keeps authored Position, Scale, Rotation, Orientation, temporal easing, spatial tangents, roving metadata, and keyframe count unchanged during Group, Ungroup, Add/Remove from Group, Transform Parent, and parent deletion operations.
- Applies the same effective parent basis in the editor, Canvas hit-testing/manipulation, 2D compatibility compositor, projected 3D renderer, masks, mattes, group depth, motion blur, effects, and overlays.
- Preserves the existing opaque/transparent hardware-depth ordering, destination-aware blend fallback, offscreen group boundaries, effect-space separation, projected bounds, near-plane clipping, and perspective/orthographic parity.
- Adds validated Development Version 216 serialization for the optional finite parent-bind matrix. Older titles load with identity binding.
- Includes parent binding in visual hashes and bumps disk/GPU renderer ABI identities so hierarchy changes cannot reuse stale cache frames.

# Development Version 215 — Timeline and Graph Editor Completion

- Graph keyframes move at sub-frame precision by default; Ctrl/Command explicitly snaps time and values.
- X/Y/Z/W and A/R/G/B channel changes preserve the current keyframe selection.
- Four-channel scalar groups now use the same expanded rows in Layer List, Timeline, and Graph Editor.
- Double-click creates a keyframe on an empty property row and opens Keyframe Velocity on an existing temporal key.
- Copy/cut/paste/delete are available from both Timeline and Graph Editor context menus.
- A single-property clipboard can target the active compatible property, including Vector2/Vector3 conversion.
- Pasting at an occupied time replaces the existing key rather than creating duplicate timestamps.
- Camera switching, camera assignment, and projection switching retain discrete Hold semantics while sharing the common clipboard and undo paths.
- No serialized fields were added; the authored serialization schema remains Development Version 212.

# v0.8.9-alpha — Development Version 214

## Development Version 214.3 — Keyframed Group Reparent Fix

- Treats one-key and constant transform tracks as a static parent basis instead of triggering an unnecessary animated reparent bake.
- Extends the lossless whole-track offset fast path to static 3D translation-only group and transform-parent changes.
- Caps the true animated-basis fallback at 64 total samples, regardless of title duration, while retaining authored key times.
- Collapses constant generated transform channels after fallback conversion so Position-only compensation cannot create hundreds of Scale, Rotation, Orientation, or Z keys.
- Adds slow-group frame counts and the child with the largest transform-keyframe track to the one-second GPU grouping diagnostics.
- Keeps the Development Version 214 runtime label and Development Version 212 serialization schema unchanged.

- Reworked FPS diagnostics into one exact elapsed one-second sampling window for true average FPS and render cost.
- Audited coordinate writes so canvas/world geometry is converted back into effective parent-local Position for manipulation, alignment, distribution, and hierarchy changes.
- Added full-track 2D/3D Position conversion and world-preserving animated reparent/group operations.
- Removed the unbounded per-project-frame reparent bake that could make the editor crawl after grouping an animated layer.
## Development Version 214.2 — Animated Group Render Performance and Diagnostics

- Skips the pre-effects silhouette render unless the Group has an enabled effect that actually affects layers behind.
- Reuses two persistent per-group full-canvas GPU ping-pong targets instead of creating and destroying three texrender surfaces on every group render.
- Publishes the existing ping/pong result directly when possible and performs a final copy only when a mask/effect returns an external shared target.
- Adds one-second aggregate GPU group diagnostics with call count, average/maximum render time, child count, target creations, silhouette passes, and surface size.
- Adds dedicated **Grouping** and **Coordinates** logging categories plus command-level timings for Group, Ungroup, Add to Group, and Remove from Group.
- Keeps the Development Version 214 runtime label and Development Version 212 serialization schema unchanged.

## Development Version 214.1 — Animated Group Reparent Performance Fix

- Preserves animated Position tracks directly when moving layers into or out of static translation-only groups, retaining the original keyframes, temporal easing, spatial tangents, and roving metadata.
- Replaces the previous project-frame-rate bake with authored key times plus bounded 12 Hz adaptive samples only when the source or destination parent basis is itself animated.
- Caps generated adaptive samples at 512 while never discarding authored transform key times.
- Applies the same no-bake fast path to Transform Parent assignment and normal ungrouping.
- Keeps the Development Version 214 runtime label and Development Version 212 serialization schema unchanged.

## Development Version 214 — Unified Editor Data Model and Coordinate Audit

- Updates the footer once per second from one elapsed-time diagnostics window. FPS is the number of successfully presented playback frames divided by the actual window duration; average render time is computed from the frames rendered in that same window.
- Removes the diagnostics callback's unconditional Timeline repaint, avoiding a periodic update unrelated to authored or playback state.
- Establishes one coordinate contract: Transform panel values and keyframes are layer-local relative to the effective group/transform parent; Local, Parent, and World select gizmo-axis orientation only.
- Audits grouping, ungrouping, add/remove group, transform-parent assignment, and parent deletion so reparenting preserves the full evaluated world-space animation instead of only the current frame.
- Uses exact whole-track offsets for static translation-only basis changes. Only animated parent bases use bounded adaptive samples, while authored transform key times are always retained.
- Adds no-op guards so choosing an already active parent cannot rebake or alter a track.
- Routes post-mutation selection through the shared Canvas/Layer List/Timeline synchronization path and retains the existing shared flattened layer/property row model.
- Clarifies the UI with **Local Position** and **Local Axes / Parent Axes / World Axes** labels and explanatory tooltips.
- Keeps the authored Development Version 212 serialization schema unchanged; existing transform fields are reused and no migration is required.

# v0.8.9-alpha — Development Version 213

## Development Version 213 — Editor Stability and Interaction Consistency

- Stops the precise monitor-rate GUI refresh timer whenever the editor is idle and starts it only during explicit pointer interaction.
- Moves stopped-transport clock/ticker invalidation to a bounded 10 Hz timer instead of refreshing at the monitor rate.
- Restricts the application-wide event filter to the Title Editor and its native canvas window, so unrelated OBS docks and controls cannot suspend editor presentation.
- Routes Canvas, Layer List, and Timeline selection through one guarded synchronization path with no-op setters in every view.
- Makes empty-canvas context menus target-specific so stale selected layers cannot receive unintended actions.
- Resolves the **E** shortcut conflict by reserving it for the 3D Rotate gizmo while the canvas has focus and retaining Free Transform elsewhere.
- Adds aggregated Timeline paint profiling plus editor layout and panel-refresh trace diagnostics.
- Keeps the authored Development Version 212 serialization schema unchanged.

## Development Version 212.3.1 — Editor Event-Filter Compile Fix

- Declares the watched QWidget derived from the event-filter QObject before shortcut-scope routing uses it.
- Reuses the validated widget pointer inside the shortcut block instead of performing a second cast.
- Adds a regression guard ensuring the declaration remains before `watched_in_editor`.
- Does not change serialization, editor behavior, or the Development Version 212 authored schema.


## Development Version 212.3 — Editor Usability and Frame Pacing

- Measures editor playback FPS from successful project-rate swapchain presentations instead of render preparation calls.
- Separates render-cost statistics from playback FPS timing, preventing idle time and editing work from contaminating the live FPS value.
- Uses a fractional floor/ceil timer cadence so 60, 59.94, and 29.97 fps transports do not run slowly because of permanent integer rounding.
- Gives ordinary canvas Move, Rotate, and Resize gestures monitor-cadence priority, matching the established 3D gizmo and editor-camera paths.
- Restricts the spatial-keyframe context menu to a direct keyframe-handle hit; right-clicking a motion-path segment now consistently opens the layer context menu.
- Limits the 90 ms layout-settle suppression to the main window and actual dock structure events instead of every descendant control resize/layout request.
- Places the 3D View, gizmo, framing, depth, and normals controls directly after Adaptive in the main canvas toolbar.
- Adds sampling tolerance to the FPS warning color to prevent normal timing jitter from flashing the footer red.
- Retains Development Version 212 serialization and the existing 2D/3D render compatibility boundaries.

## Development Version 212.2 — Shared Layer-List and Timeline Vector3 Rows

- Replaced the widget-local Vector3 disclosure set with title-owned shared disclosure state.
- The layer list and TimelineWidget now consume one flattened `timeline_rows()` model.
- Expanding Position, Scale, Rotation, Orientation, Anchor, camera Position, or Point of Interest inserts the same X/Y/Z rows into both panes.
- Collapsing the property removes the same rows from both panes immediately.
- Channel rows use the same 28 px height as timeline rows, preserving exact vertical alignment and shared scrolling.
- Timeline keyframe hit-testing and row selection now target X, Y, Z, or All in the Graph Editor according to the clicked row.
- Added a row-count invariant and regression contract preventing the layer list and timeline from diverging again.
- The disclosure state is persisted without changing the authored animation schema or the legacy 2D rendering path.

## Development Version 212.1 — Layer-List Vector3 Channels and Graph Targeting

- Keeps one aggregate Position, Scale, Rotation, Orientation, Anchor, camera Position, and camera Point-of-Interest row while adding a disclosure caret only to properties with multiple channels.
- Expands multi-channel rows into editable X, Y, and Z child rows in the layer list, including the previously missing 3D Rotation and Orientation channels.
- Makes the aggregate row an **All** Graph Editor target and each child row a direct X/Y/Z target.
- Adds exclusive X, Y, Z, and All channel toggles to the Graph Editor toolbar.
- Draws all three vector curves together in All mode and applies relative vertical keyframe edits to every component; single-channel mode edits only the selected component.
- Promotes the legacy-named `AnimatedVec2Property` compatibility facade to full X/Y/Z Graph Editor access without changing its serialized compatibility contract or the 2D affine renderer.
- Initially kept the normal timeline as one row per property; Development Version 212.2 supersedes that UI limitation with synchronized expandable X/Y/Z rows while retaining one authored keyframe track.
- Requires no schema migration: the authored Vector3 storage remains Development Version 212 and old 2D projects continue to load with their established Z defaults.


## Development Version 212 — Unified Vector3, Camera Timeline Visibility, and Direct Keyframe Synchronization

- Expands the legacy `Vec2Value` and `AnimatedVec2Property` storage contract to XYZ while preserving their public names as source/serialization compatibility facades.
- Reads pre-212 XY-only JSON with field-specific Z defaults, merges legacy `position_z`, `scale_z`, and `anchor_z` key times into unified vector tracks, and continues to save the old scalar mirrors for backward compatibility.
- Leaves the legacy 2D affine rendering path untouched; Z is ignored for 2D layers, so old 2D projects retain their historical output.
- Includes Z in spatial interpolation, tangent math, velocity, cache identity, sampled animation signatures, serialization, copy/paste, and undo/redo payloads.
- Replaces separate transform-axis timeline rows with one aggregate Position, Scale, Rotation, Anchor, or Orientation row; old unsynchronized axis keyframes are represented through a union of their key times.
- Makes a clicked timeline property row or keyframe the active Graph Editor target without requiring a separate layer-list selection step.
- Refreshes layer-list disclosure rows immediately after timeline paste/delete and refreshes the timeline immediately after layer-list keyframe changes, including removal of the final keyframe.
- Hides the implicit default camera until it has authored keyframes, while custom cameras remain visible and expose their full property set when expanded.
- Adds Camera to the Add Layer menu; a newly created custom camera receives a stable ID/name and becomes the active render camera at the current playhead.


## Development Version 211 — Compatibility and Regression Completion

- Preserves pixel-identical legacy 2D rendering by retaining the historical affine path whenever neither a layer nor its ancestors use 3D.
- Replaces the editor's selective undo restore list with a complete deep authored-title snapshot, covering cameras, camera switches, all layer/camera 3D properties, Live Text Cue authoring, scene-mask state, audio layers, persistence settings, imports, and future title fields automatically.
- Preserves runtime title identity, editor camera override, proxy metadata, cue/playlist playback state, and persistence-transition state while restoring authored undo/redo snapshots, then forces an authoritative GPU refresh and deferred cache invalidation.
- Validates preserved Live Text Cue runtime rows against the restored authored table, clamps playlist indices, and normalizes persistent-column state so undoing cue-table edits cannot leave out-of-range runtime references.
- Forces editor audio runtime publication and a discontinuity-aware transport sync after undo/redo, including when the playhead itself did not move.
- Adds Duplicate Camera, Copy Camera, and Paste Camera actions that preserve complete animated camera properties and generate safe IDs/names.
- Extends layer clipboard payloads with referenced camera definitions and remaps static and keyframed camera assignments during cross-title paste or when a same-title source camera was deleted after copying.
- Keeps title import/export portable by stripping machine-specific proxy metadata, editor render-camera overrides, and active cue/playlist runtime state while retaining authored layer and camera IDs needed by masks, parenting, bindings, and camera assignments.
- Adds a consolidated Version 211 compatibility contract and expands the title snapshot unit test to cover complete 3D/camera restoration plus runtime-state isolation.
- Documents automated and manual regression gates for proxy/prerender, persistence, Live Text Cues, scene masks, synchronized audio, shutdown, leak detection, and repeated editor open/close cycles.


## Development Version 210 — Performance, Cache, and Rendering Audit

- Presents active 3D Move, Rotate, and Scale updates at monitor cadence while reusing resident layer rasters and updating only the evaluated GPU transform snapshot.
- Rejects duplicate pointer coordinates before animation properties, model revisions, or render scheduling are touched.
- Keeps a single coalesced render request, leaves full raster/layout work cost-aware, and preserves transport-driven timeline playback.
- Treats keyframe insertion, deletion, interpolation changes, and post-drag geometry publication as authoritative model boundaries. A full GPU snapshot is guaranteed before transform-only updates can resume.
- Gives the exact post-release frame realtime scheduling priority without carrying transform-only state through `layer_geometry_changed()` or `refresh_preview()`.
- Bypasses stale final-frame cache submission while an authoritative editor model refresh is pending, without clearing or mutating the independent OBS/RAM/disk prerender cache.
- Adds source contracts for realtime pacing, bounded scheduling, cache isolation, and the keyframe-after-transform regression.


## Development Version 209 — Camera-Aware 3D Motion Blur

- Evaluates every temporal sample through the full 3D parent hierarchy and the active or layer-assigned camera.
- Covers XYZ translation, rotation/orientation, perspective scale changes, parent/grandparent animation and camera orbit, rotation and dolly movement.
- Measures projected screen-space travel at five shutter times using corners, edge midpoints and centre points for stable adaptive sampling.
- Raises the real-time temporal sample budget only for projected 3D motion while preserving the existing 2D budget.
- Keeps static projected layers pixel-identical through the zero-travel early-out, preventing stationary smears and alpha halos.
- Uses the same output path in the editor, OBS, prerender and cache.

This release consolidates the work completed after `v0.8.8-alpha` Development Version 144: first-class audio layers and editor monitoring, native OBS Stinger transitions, Scene A/B animation, serialization and migration hardening, cache/prerender scheduling improvements, live cue transition persistence, and broad cross-platform regression coverage.


## Development Version 208 — Full 3D Spatial Motion Paths

- Added a unified `AnimatedVec3Property` and `Vector3Keyframe` model for genuine XYZ position paths, including complete temporal velocity metadata, incoming/outgoing 3D tangents, linked tangent state, spatial interpolation mode, and roving state.
- Promoted 3D layer Position and camera Position/Point of Interest to shared Vector3 tracks while retaining the legacy XY/Z and scalar camera channels as compatibility mirrors for older documents and unpromoted content.
- Added separate X, Y, and Z Value/Speed Graph Editor channels backed by one shared 3D keyframe sequence, so temporal and spatial interpolation are edited together without duplicating keyframes.
- Added Linear, Auto Bezier, Continuous Bezier, and Manual Bezier spatial interpolation, true 3D handle dragging through the active camera, curve-preserving segment insertion, and distance-based roving keyframes.
- Extended timeline copy/paste, move, delete, easing, and undo/redo operations to preserve the complete 3D keyframe payload.
- Projected parent-space paths through each keyframe's evaluated parent hierarchy into world/camera space, and extended Frame Selected to include the complete selected motion path.
- Added serialization, migration, cache hashing, animation classification, runtime tests, and source contracts for the new Vector3 tracks.
- Preserved the anchor-stable projected-content path and all mask, depth, effect, and compositor behavior from Development Version 207.


## Development Version 207 — Camera Timeline and Animation

- Rebuilt the camera-animation delivery directly from the clean Development Version 206 source instead of layering fixes over the earlier 207 revisions.
- Added a collapsed **Camera Switches** owner row and collapsed per-camera rows to the timeline. Camera channels appear only when their AE-style disclosure caret is opened.
- Exposed Position XYZ, Point of Interest XYZ, Orientation XYZ, Rotation XYZ, focal length, field of view, zoom, near/far clipping, and Projection channels to the timeline and shared Value/Speed Graph Editor.
- Added discrete Hold-keyframe tracks for active-camera switching and per-layer camera assignment, including selection, drag, copy/paste, deletion, and undo/redo through the established timeline command path.
- Added serialization and migration for camera orientation, projection mode, camera switching, and layer camera assignment while retaining the legacy static mirrors used by existing projects.
- Preserved the Development Version 206 render baseline for every unkeyed camera: static selection, static assignment, static projection, 3D text rasterization, projected bounds, preview scaling, and layer presentation are unchanged. New camera evaluators are used only for authored animation tracks.
- Fixed long-standing 3D content pulsation with non-centred anchors. Fallback artwork now fits its projective transform from the real texture rectangle to the exact projected local raster corners, matching the already-stabilized bounding-box path instead of extrapolating a unit-square homography.
- Extended animation detection and cache identity only for actual camera/switch/assignment keyframes; opening or navigating camera rows does not invalidate rendered content.
- Kept full XYZ spatial motion paths and 3D spatial Bezier editing scoped to Development Version 208.


## Development Version 206 — Masks, blend modes, and effects in 3D

- Added a stable effect-space contract: ordinary artwork effects run in padded layer space before projection, Motion Blur runs from projected transform samples, and affect-layers-behind effects remain screen-space destination passes.
- Allowed compatible 3D planes with layer-space effects and projected track mattes to enter the hardware depth pass.
- Evaluated track matte alpha/luma/clipping coverage in screen space during the same projected depth draw, preventing masked-out pixels from writing invisible Z.
- Projected complete padded effect-raster bounds through the camera with homogeneous frustum clipping, preserving shadow, glow, blur, and outline extents after perspective rotation and safely rejecting fully off-camera surfaces.
- Kept non-Normal blend modes in the destination-aware full-frame ping-pong compositor while ordering compatible contiguous 3D fallback surfaces by camera depth.
- Preserved groups as offscreen compositing boundaries: compatible children resolve internal depth before the flattened group receives its own mask, effects, opacity, and blend mode.
- Preserved OBS scene-mask insertion at the real layer-stack position and kept scene-mask effects in the full-canvas post-projection path.
- Replaced unbounded projected selection/hover coordinates with homogeneous frustum-clipped polygons and visibility-tested handles, eliminating flicker when a 3D bounding box crosses the canvas or near plane.
- Retained the MSVC C2601 correction by keeping `PropertiesPanel::apply_anchor_preset()` at file scope outside the constructor include chain.
- Added Development Version 206 source contracts for mask/depth coupling, effect-space classification, blend/group ordering, projected effect bounds, scene masks, and stable 3D overlays. No project schema change was required.


## Development Version 205 — Compact 3D properties, stable overlays, and full group parenting

- Made Layer Properties denser across Transform, Character, Paragraph, Shape, Image, and Text Animator sections, with smaller labels, numeric fields, spacing, and controls.
- Placed scale/aspect lock controls consistently after the complete numeric value set, including XYZ Scale, shape Size, and image-box Size rows.
- Moved the layer anchor preset control out of Layer Properties and into the context-sensitive dynamic editor toolbar while retaining the existing compensation, keyframe, and undo paths.
- Converted the 3D Camera inspector to a persistent collapsible header and made its Position, Target, Rotation, focal-length, FOV, zoom, and clipping labels draggable for numeric editing.
- Stabilized projected 3D selection and hover bounds with solid cosmetic outlines plus device-pixel-aligned overlay geometry, removing dash-phase/subpixel flicker without quantizing rendered artwork.
- Added full XYZ world-transform snapshots for grouping, ungrouping, transform-parent changes, group removal, and deletion. Reparenting into or out of a 3D hierarchy now computes the new local TRS while preserving the visible world transform.
- Defined 2D children under 3D parents as strict local XY planes that inherit the complete parent basis but ignore stale local Z/Rotation-X/Y channels until explicitly promoted to 3D.
- Extended hardware depth and deterministic transparent ordering to compatible sibling planes inside nested groups before each group boundary is flattened for its own masks/effects.
- Advanced development metadata and the contiguous no-schema-change migration ledger to Development Version 205.


## Development Version 204 — AE-style 3D layer UI and transparent compositing

- Added a final **2D/3D** toggle to every compatible layer-list row; audio and adjustment layers expose a disabled 2D state.
- Reworked the Transform panel so enabling 3D expands Position, Scale and Anchor from XY to XYZ in place, presents Rotation as X/Y/Z, adds Orientation X/Y/Z, and keeps every Z field at the end of its owning row.
- Unified each visible XYZ control behind one keyframe diamond and one context-menu action while preserving separate X/Y/Z graph channels and the existing serialized XY-plus-Z schema.
- Added a migration-safe `LayerVector3Value` facade used by the shared editor/OBS 3D transform evaluator.
- Added a dedicated transparent 3D pass after opaque depth rendering. Compatible planes are sorted far-to-near by camera depth, with authored layer order as the stable coplanar tie-break.
- Transparent layers with **Write Depth** disabled retain color without contaminating later Z tests; explicitly depth-writing transparent layers participate in persistent depth according to their per-layer settings.
- Kept masked, effected, grouped, custom-blend, motion-blurred and Transition Input layers on the established compatibility compositor.
- Advanced development metadata and the contiguous no-schema-change migration ledger to Development Version 204.


## Development Version 203 — Depth, culling, and material semantics

- Made **Depth Test** and **Write Depth** independent for compatible simple opaque root-level 3D planes instead of requiring both controls to be enabled.
- Added persistent depth writers with `LEQUAL` testing, plus `ALWAYS` writers for layers that intentionally write depth without testing existing Z.
- Added authored-order read-only depth evaluation for **Depth Test enabled / Write Depth disabled** layers. Each such layer tests only against persistent depth already written at its position, then restores that depth without contaminating later layers.
- Changed backface classification to use final screen-space projected winding and retained a world-space plane normal, keeping culling predictable under negative X/Y scale, parent mirroring, perspective, and orthographic projection.
- Added editor-only **Depth** and **Normals** diagnostics showing projected camera depth, depth-state combinations, front/back classification, culled faces, and world-normal direction without changing final output or cache identity.
- Added explanatory tooltips, Development Version 203 migration continuity, and dedicated source contracts for the new depth/culling behavior.


## Development Version 202 — Hardware depth-buffer core

- Added an optional persistent `GS_Z24_S8` render target for compatible opaque 3D runs containing at least two planes; unsupported backends and single-plane runs retain the established compositor.
- Added real per-pixel depth testing with `GS_LEQUAL`, perspective/orthographic camera projection, and shared world matrices for editor/OBS parity.
- Added an alpha-clipped depth shader so fully transparent raster pixels do not occlude geometry behind them.
- Split depth runs whenever the effective render camera changes and retained the authored compatibility compositor around every non-compatible layer.
- Kept masks, effects, motion blur, custom blend modes, groups, transparency, Transition Input layers, and depth-disabled layers on the established texture-compositing path.
- Added runtime resource cleanup, Development Version 202 migration continuity, and a dedicated hardware-depth source contract.


## Development Version 201 — Editor 3D views, navigation, and transform gizmos

- Added an editor-only camera override that cannot be serialized, cached as title content, or propagated to OBS output.
- Added Active Camera, Front, Back, Left, Right, Top, Bottom, and Custom Perspective views.
- Added orbit, pan, dolly/zoom, Frame Selected, and Frame All navigation with persistent per-editor view state.
- Added Move, Rotate, and Scale 3D gizmos with X/Y/Z axes, plane handles, Local/Parent/World orientation, snapping modifiers, multi-selection updates, and the existing editor undo/redo transaction path.
- Added a dedicated 3D control bar and W/E/R, F, and Shift+F shortcuts.
- Kept cached final frames disabled only while an editor camera override is active, without invalidating the title's final-output cache.


## Development Version 200 — MSVC camera inspector compile repair

- Fixed the camera-control enable/disable loop in `title-properties-panel.cpp` by explicitly normalizing every `QDoubleSpinBox*` to `QWidget*`; this avoids MSVC `std::initializer_list` deduction errors C3535/C2440.
- Synchronized CMake, runtime build information, serialization development metadata, package examples, and version contracts to Development Version 200.
- Added explicit no-schema-change migration steps for Development Versions 191–200 so migration reporting remains contiguous.


## Development Version 190 — Planar 3D transform foundation and cameras

- Added opt-in 3D mode for compatible visual layers while preserving the exact legacy 2D path for old projects.
- Added Position Z, Rotation X/Y/Z, Scale Z, Anchor Z, Orientation XYZ, transform-space selection, per-layer camera assignment, depth controls, double-sided rendering, and backface culling.
- Added a shared editor/OBS projective transform evaluator, full 3D parent inheritance, perspective and orthographic title cameras, a default canvas-matching camera, and camera editing controls.
- Added conservative opaque-plane depth sorting for the flattened compositor while preserving authored ordering for 2D, transparent, masked, effected, blended, and depth-disabled layers; hardware Z-buffer compositing remains a follow-up.
- Connected all 3D channels to timeline keyframes, temporal interpolation, animation detection, transform-only refresh, motion-blur travel, serialization, migration, content hashes, and cache invalidation.
- Added a maintained 3D coordinate/effect-ordering guide with explicit development boundaries and a standalone layer-3D source contract.

## Development Version 189 — Disabled FX stack indicator and documentation consolidation

- Layer rows retain their FX badge when an effect stack is bypassed and draw a diagonal strike-through over it. Re-enabling the stack removes the strike immediately.
- Preserved the independent external-data binding dot/label behavior on the same layer indicator.
- Bumped the public version to `v0.8.9-alpha` and the delivery revision to Development Version 189 across CMake, runtime metadata, dependency metadata, package examples, tests, and audits.
- Rewrote the root README around the current feature set and grouped release additions since Development Version 144.
- Merged Text Animator documentation into the text/live-data guide, merged testing and validation guidance into the architecture guide, and removed obsolete root-level per-delivery reports.

# Development Version 188 — Audio mixer visibility and reverse editor audio

- Title sources now call OBS audio-active state dynamically: only titles with
  an Audio layer appear as devices in the Audio Mixer.
- Mixer visibility is re-evaluated when the bound title or its layer structure
  changes, without recreating the OBS source.
- The editor monitored preview now publishes its exact playhead and playback
  direction to the private title source.
- Reverse playback mixes decoded audio samples in descending sample order,
  including editor reverse handling for independent audio layers.
- Direction changes are transport discontinuities so scheduler state and DSP
  history are reset cleanly at reverse/forward boundaries.

# Development Version 187 — Live cue transition persistence and cache performance

- Unified live cue persistence timing for keyframes and transitions, including manual uncue and row-to-row persistence states.
- Made cache visual-state deduplication persistence-aware.
- Batched cache queue construction, added constant-time queued-key lookup, deferred background ordering, and added a realtime/urgent lane that preempts background work without full-queue sorting.
- Converted RAM frame-cache LRU maintenance from linear scans to constant-time list operations.
- Moved disk frame hydration entirely to the cache worker for editor and OBS realtime requests; bounded disk persistence is now nonblocking and best-effort under backpressure.
- Added non-blocking disk membership/usage snapshots, coalesced UI notifications, debounced prerender diagnostics, removed per-live-cue-frame sleeps, and aligned paused/interactive wake predicates with the urgent dequeue lane.
- Bumped the GPU cache ABI so stale frames created under the old persistence clock cannot be reused.
- No persisted title schema fields changed.

# Development Version 186 — Sample-locked editor audio cadence

- Analyzed the v185 runtime log and found 16 synthetic 20–21.6 ms timestamp gaps in nine seconds, with no actual monitor underrun.
- Replaced wall-clock-relative monitor deadlines with absolute sample-locked deadlines.
- Removed periodic timestamp rewrites to `now`; normal packet timestamps remain exactly contiguous.
- Added bounded catch-up for ordinary timer jitter and a 70 ms threshold for genuine hard resynchronization.
- Added deterministic Windows-oversleep regression coverage and retained the existing source-audio scheduler unchanged.

# Development Version 184 — Editor audio delivery restoration and diagnostics

- Replaced the ineffective Development Version 183 threshold-only adjustment with a real delivery-path split.
- Private editor preview audio now uses the exact Development Version 178 `video_tick` packet-submission cadence, while decode remains asynchronous and cached.
- Ordinary OBS source audio continues to use the Development Version 181 background worker and 80–160 ms buffered queue.
- The editor-preview flag is read before constructing `SourceAudioRuntime`, preventing any initial worker-mode burst.
- Added `[FXM Audio]` lifecycle, transport, decode, flow, timestamp-gap, delivery-stall, underrun and idle diagnostics to the OBS log.
- Added source contracts for single-path editor delivery and logging coverage. No serialization fields changed.

# Development Version 183 — Editor audio monitor cadence fix

- Isolated the remaining audio regression to the private editor preview source, which uses OBS monitor-only delivery rather than the normal timestamped source mixer.
- Kept the 80–160 ms queue for ordinary source output and added a dedicated 10–30 ms monitor profile with a three-block burst limit.
- Added automatic scheduler profile switching from the public OBS monitoring type, with clock reset at consumer boundaries.
- Added editor-monitor cadence regression coverage; no persisted schema fields changed.

# Development Version 182 — Windows audio worker compile fix

- Fixed MSVC C3861 in `title-audio-runtime.cpp` by removing the unavailable `os_set_thread_name()` libobs symbol.
- Added private Windows/Linux/macOS thread naming with no new link dependency and no audio-path behavior change.
- Added a portability source contract and extended the no-op serialization migration ledger to Development Version 182.

# Development Version 181 — Audio scheduler repair and automated regression pass

- Fixed the v180 intermittent/noisy audio regression after a direct v178/v180 comparison identified the dedicated output scheduler's 30 ms queue as the only material playback-path change.
- Added an 80–160 ms bounded output window, monotonic gapless timestamps, title-playhead re-anchoring after true underruns, and new audio-output debug counters.
- Kept file decode, waveform generation, audio mixing, network activity and proxy rendering outside UI/render/video threads.
- Added direct external JSON path and audio scheduler unit tests, a CTest wrapper for every Python source contract, the Development Step 100 matrix, and a complete manual regression checklist.
- Extended the serialization migration ledger to Development Version 181 without changing persisted schema.

# Development Version 180 — Performance, cache and threading audit

- Coalesced provider and render-publication bursts by source/field key, with a minimum frame-sized publication window.
- Added bounded compiled formatting-pattern caching and retained canvas pattern-tile diagnostics.
- Moved audio mix/output off the OBS video tick, added cooperative decode cancellation, reusable immutable PCM/waveform assets, weak global ownership, and worker joins on source shutdown.
- Added per-title disk-write generations, stale GPU-readback cancellation, proxy-only visual-hash exclusion, and complete title-cache ownership cleanup while preserving dirty-region invalidation.
- Replaced linear Bezier segment scans with binary search and added an explicitly invalidated UI-only motion-path sample cache.
- Added debug performance counters plus new unit/source contracts for threading, caching, shutdown and migration compatibility.

## Development Version 179 — Unified Serialization and Migration Audit

- Introduced title schema version 4 with per-title `schema_version` and `development_version` fields while preserving the top-level title array used by older builds.
- Added a contiguous Development Version 144–179 migration ledger and idempotent current-schema validation.
- Added validation/recovery for external sources and bindings, rich-text formatting profiles and pattern rules, audio layers, Stinger settings, proxy metadata, Bezier handles, and dock collapse/splitter state.
- Unknown or unavailable external providers now load disabled; missing audio and stale/broken proxy files remain non-fatal.
- Existing non-empty layer IDs are never regenerated during migration or template import.
- Style presets, Auto Styling rule sets, title templates, per-title store entries, and proxy manifests now carry schema metadata where applicable; title/preset/profile writes and manifest rewrites use atomic replacement.
- Added JSON migration round-trip tests and a unified source contract covering every audited feature.

## Development Version 173 — Full Scene A/B layer contract

- Manual Scene A/B inputs now support the same trim, temporal move, keyframes, transitions, hierarchy, rename, copy/duplicate, masks and effects workflows as ordinary shape/image layers.
- Removed the special fixed-duration timeline path and all automatic reapplication of full-duration timing.
- Required A/B layers are protected only from deletion. Duplicates are ordinary deletable scene-input layers.
- Existing authored geometry, timing, effects, transitions, hierarchy and ordering are preserved.
- Newly created required A/B layers start at full canvas size and full title duration.

## Development Version 172 — Shape-like Scene A/B layers

- Scene A/B transition inputs now use the same Size-backed rectangular geometry and resize behavior as shape layers.
- Live OBS scene textures map 1:1 into the current authored A/B layer box rather than inheriting placeholder-raster crop metadata.
- Runtime A/B effects and mask graphs use the layer's real local geometry while remaining non-cacheable per transition frame.
- Switch-at-point mode continues to hide A/B system layers from canvas selection and bounding boxes.

## Development Version 171 — Stinger A/B layer visibility, surfaces, and duration fixes

- Hid Scene A/B system layers from canvas selection, hit testing, hover, and bounding boxes whenever Switch at Point mode is active.
- Corrected manual mode defaults to two full-canvas 1:1 surfaces, with Scene B below Scene A and no implicit transition-point opacity cut.
- Added deterministic migration for the exact generated v168-v170 A/B defaults and fixed their timeline strips to the document animation range.

## Development Version 169 — Stinger Qt keyword compile fix

- Fixed the MSVC syntax-error cascade in `title-data.cpp` caused by using `slots` as a local variable while Qt keyword macros are enabled.
- Renamed the local transition-input container and loop identifiers to unambiguous names.
- Added a regression test for Qt macro-safe compilation of the Stinger A/B layer block.

## Development Version 168 — Stinger switch modes and animatable Scene A/B inputs

- Added Switch at Point and Manual Scene Animation modes to Stinger documents.
- Added editor-only Scene A/Scene B canvas preview backgrounds for point-switch Stingers, with Scene A selected by default for newly created Stingers.
- Added protected, persistent Scene A and Scene B runtime input layers in manual mode. Both participate in transforms, keyframes, masks, mattes, blend modes, and effects, but cannot be renamed, duplicated, grouped, or deleted.
- Kept A/B placeholder surfaces editor-only; ordinary OBS title sources render them transparent, while the native manual transition binds the live OBS scene textures.
- Bound the real OBS transition textures to those layers through the native `obs_transition_video_render()` callback and the unified FXM GPU compositor.
- Marked manual Scene A/B composition as dynamic for cache/proxy validation and added cache-key coverage for switch mode and transition-input identity.
- Synchronized CMake and runtime development-version metadata to 168.

## Development Version 164 — Stinger dock icon compile fix

- Fixed the undeclared `title` identifier in `TitleDock::populate_list()` by using the loop's current `t` title object when composing list-view status icons.
- Added a source contract to prevent the scope regression from returning.
- Synchronized CMake and runtime development-version metadata to 164.

## Development Version 150 — Audio layer UI and effect routing

- Added audio-specific layer controls, group dual eye/speaker controls, Audio Effects catalog/settings integration, and audio/visual compatibility routing.

## Development Version 144 — v0.8.8-alpha and Character panel cleanup

- Bumped the public plugin version from `v0.8.7-alpha` to `v0.8.8-alpha` across CMake, compile-time/runtime metadata, vcpkg metadata, installation/package examples, tests, audits, and canonical documentation.
- Removed the Font and Style labels from the Character properties section and made both combo boxes span the complete available panel width, preserving expansion under narrow and wide inspector layouts.
- Added consolidated README release notes covering the major changes completed since `v0.8.7-alpha` Development Version 107.

## Development Version 143 — Keyframe authoring, Graph Editor modifiers, and matte-column cleanup

- Removed the obsolete legacy Easing submenu from keyframe context menus. Temporal Interpolation is now the sole authoring UI and is also available directly from animated-property rows in the layer list, including Linear, Hold, Auto/Continuous/Manual Bezier, Easy Ease variants, and Keyframe Velocity.
- Made layer-list keyframe controls playhead-aware: diamonds indicate whether the current frame contains a key, numeric fields display evaluated animated values, edits write at the active playhead, and fields use the plugin's normal palette, border, hover, focus, selection, and disabled states.
- Added live Graph Editor drag modifiers: Shift constrains keyframes, handles, and panning to the dominant axis; Ctrl/Command snaps keyframe time/value and handle influence/speed; Alt continues to break linked temporal handles.
- Replaced the Graph Editor toolbar glyph with the supplied `graph.svg`, normalized to `currentColor`, and restored a visible, interactive time ruler and playhead marker above the graph.
- Renamed the layer-list Matte header to Matte Source and consolidated matte source/destination role indicators into one column headed by the matte-destination glyph.

## Development Version 142 — Synchronized keyframe sections and layer-list icon refresh

- Unified layer-list and timeline keyframe-section expansion behind one shared predicate, including the three-state Group caret. Animated property rows now appear and disappear in both panels together.
- Removed the timeline-only aggregate keyframe markers and hit targets from collapsed layer strips, eliminating the state where keyframes were visible in the timeline while their layer-list section was closed.
- Replaced the layer-list lock, unlock, hidden-visibility, matte-only, matte-source, and matte-destination artwork with the supplied SVG geometry. All supplied glyphs use `currentColor` and continue through the OBS-theme-aware icon renderer.

## Development Version 141 — Timeline strip, transition, ruler, and layer-state fixes

- Enlarged the visible layer-strip trim handles and widened their mouse hit zones, with outer strip edges taking priority over transition overlays so in/out points remain easy to resize when transitions are present.
- Made the complete area of an existing transition a valid drag-and-drop replacement target; replacement presets preserve the previous transition duration, subject only to the layer and opposite-edge limits.
- Made text-transition deletion immediately remove its managed Text Animator and generated keyframes, including stale timeline keyframe selections.
- Repainted the complete ruler band during playhead movement so time labels no longer lose their right edge for a frame as the playhead crosses them.
- Replaced normal, hidden, and matte-only layer visibility glyphs with the supplied SVG artwork and normalized it to `currentColor` for the active OBS theme. Existing lock/unlock glyphs remain routed through the same theme-aware renderer.

## Development Version 140 — Editor graph, timeline layout, and theme-aware icon fixes

- Added the supplied graph icon to the checkable Graph Editor button and made Value/Speed Graph, Fit Graphs, and Fit Selection controls visible only while Graph Editor mode is active.
- Restored shared playhead interaction in Graph Editor mode: ruler clicks/drags, loop/pause marker drags, and direct playhead-line dragging are no longer swallowed by graph hover or release handlers.
- Matched sidebar flyout long-press timing to the editor Fit control and made flyouts open horizontally toward the available side of the active screen rather than below the tool icon.
- Enforced a safe minimum layer-list width, synchronized header/row column widths, and reserved a usable minimum Layer Name column so Mode, Parent, Mask, and matte controls cannot overlap.
- Reorganized the timeline ruler into separate label, tick, and cache bands so cache progress no longer clips ruler labels and loop/pause markers remain orderly.
- Replaced the distribute, flip, matte, and Graph Editor artwork with the supplied SVGs. All supplied glyphs use `currentColor` and are rendered through the OBS-theme-aware icon pipeline.

## Development Version 139 — Text-transition glyph-envelope compile fix

- Fixed the Windows/MSVC build failure in `max_rich_text_font_height_hint()`.
- Removed invalid access to `TextLayoutPaintStyle::font_size` and `TextLayoutPaintStyle::scale_y`; paint styles intentionally contain paint-only state.
- The animation-aware glyph envelope now resolves effective `RichTextCharFormat` values at canonical rich-text range boundaries, preserving mixed font sizes and vertical scales without duplicating shaping data into paint runs.
- Added a regression contract that rejects future paint/shaping model cross-access.

## Development Version 138 — Text-transition glyph bounds and integrated blur regression fix

- Restored the proven isolated shaped-unit compositor as the primary unified Text Animator raster path. The flattened compositor is now an emergency fallback only, preventing advance/layout rectangles from clipping italic overhangs, swashes, combining marks, strokes, and overlapping glyph ink.
- Cropped transition units from their actual alpha/ink envelope and retained a transparent resampling gutter around every crop so scale, slide, rotation, filtering, and blur cannot sample against a hard image edge.
- Expanded text-layer temporary surfaces and clips using font metrics, rich-text maximum font height, plain/rich stroke width, and antialiasing slack instead of fixed approximate padding.
- Moved Text Animator blur onto the same premultiplied-pixel blur backend, blur-type pass mapping, and support-radius calculation used by the built-in FXM Blur effect. The retired transition-only blur helper is no longer present.
- Preserved radius-driven blur semantics: a unit is rendered as the blurred representation while blur is active and resolves to the sharp representation at zero, avoiding a sharp core plus halo.
- Bumped the GPU/cache visual ABI so prerenders produced by the cropped/transition-only-blur implementation cannot be reused.
- Added structural regression coverage for primary-path ordering, alpha-bound crops, transparent gutters, animation-aware ink padding, shared blur backend usage, and removal of the old transition blur helper.

## Development Version 137 — Text-transition runtime activation fix

- Fixed the broken title-source include boundary introduced in Development Version 136: the orphaned tail of the retired legacy transition function was removed from `compatibility-layer-raster.inc`, so the unified renderer is emitted at valid file scope.
- Added runtime self-healing that resolves every serialized text-transition descriptor into its bound generic `TextAnimatorStack` before cache-key generation and rendering. Descriptor-only, stale, and intermediate 134/135 titles therefore animate without requiring an editor resave.
- Routed transition-managed text through the conservative compatibility raster compositor while preserving the shared shaped layout and `TextAnimatorEvaluation`; this bypasses the unvalidated per-glyph GPU route without reviving any preset-specific transition evaluator.
- Made transition-managed raster composition use the already rendered canonical text surface first, preserving rich fill, stroke, shadow, background, emoji, and color-font appearance while applying per-unit opacity, transform, wipe, and blur.
- Added regression coverage for descriptor-only runtime recovery, include-module boundaries, managed-transition GPU fallback, and flattened-compositor routing.

## Development Version 136 — Complete unified legacy text transitions

- Restored the complete FXM text-transition authoring workflow while keeping `TextAnimatorStack` as the only runtime evaluator. Text-transition descriptors remain timeline/Transition Editor metadata and are bound to editable managed animators.
- Added the generic `Staggered` selector with exact historical two-stage easing, entrance/exit timing, reverse order, character/word/sentence units, Hold behavior, and frame-accurate layer-local keyframes.
- Converted Fade Text, Slide In/Out, Scale Text, Blur Text, Wipe Text, and Blur Slide In/Out into editable unified animators without deleting their timeline handles.
- Added stable transition binding signatures so duration/direction/editor changes update the managed animator, while unrelated refreshes and saves preserve manual animator/property/selector/keyframe edits.
- Added live synchronization while dragging transition duration and trimming layer edges.
- Restored word/sentence Scale Text behavior through generic shared-unit transform origins instead of scaling each glyph independently.
- Added directional shaped-unit clipping for Wipe Text in the common GPU glyph compositor.
- Replaced SDF edge softening with a multi-sample contracting glyph/stroke blur so Blur Text and Blur Slide no longer render as a sharp core with a glow-like halo.
- Added a generic Qt compatibility-raster adapter driven by the same shaped layout and `TextAnimatorEvaluation`, covering color fonts, emoji, unsupported glyph alpha maps, oversized atlas glyphs, and bounded long-text fallback without reviving legacy preset-specific rendering.
- Updated Transition Editor text previews to create and evaluate the same managed animator model used by editor/source output rather than duplicating stagger/easing formulas.
- Added automatic recovery of timeline transition descriptors from intermediate Development Versions 134/135 and deterministic upgrade to the current managed binding schema.
- Fixed duplicate/delete/enable lifecycle behavior for managed transition animators and added regression coverage for runtime retirement, preview parity, fallback rendering, synchronization, migration, Unicode units, cache signatures, and 1,200-cluster stress.

## Development Version 135

- Fixed the `PropertiesPanel` implementation-module order so the Text Animator member definitions are emitted at file scope instead of interrupting the constructor.
- Fixed GPU ticker Text Animator preparation to use the render session title and cue state when sampling `ticker_runtime`.
- Added `DOWNLOAD_EXTRACT_TIMESTAMP TRUE` to the nlohmann/json `FetchContent_Declare` call to remove CMake CMP0135 warnings.
- Synchronized CMake and runtime development-version metadata.

# Development version 134 — Unified Text Animator Core and Legacy Preset Migration

- Introduced a single shaped-layout-based Text Animator data/evaluation model shared by editor and source rendering.
- Added ordered animator stacks, generic properties, four selector families, selector composition, deterministic seeds, dynamic-text policies, serialization, cache signatures, and timeline discovery.
- Converted every legacy text preset identifier found in development version 133 to editable animator/property/selector/keyframe data and retained legacy loading only as a conversion layer.
- Added a Text Animators inspector and standalone `.fxmtextanim` preset round-trip support.
- Added Unicode/dynamic text/migration/performance tests and `TEXT_AND_LIVE_DATA.md`.
- This revision is the shared-core integration milestone; full layout-animation, effects parity, expanded preset library, pixel fixtures, and Windows/Linux OBS runtime validation remain explicitly tracked in `TEXT_AND_LIVE_DATA.md`.

## Development Version 133 — Dock caret refinement and updated application icon

- Replaced the Titles and Graphics collapse/expand arrows with the same monochrome disclosure caret used elsewhere in the UI. The expanded header caret points down and the collapsed rail caret points right.
- Kept the docking-side-aware placement of the caret/button while leaving the disclosure direction consistent.
- Hid the compact-rail cache icon whenever aggregate cache/prerendering is disabled instead of showing a disabled cache glyph.
- Replaced the Flux Motion application/window icon with the newly supplied brand artwork without redrawing or restyling it.
- Updated build metadata and package naming to Development Version 133.

## Development Version 132 — Collapsible Titles and Graphics Dock

- Added a header caret that collapses only the Titles and Graphics pane; Live Text Cues remains mounted and operational.
- Added a compact rail showing the Flux Motion icon, selected-title active/inactive state, cue state, and aggregate cache/prerender status.
- Collapse is visibility/layout-only: the title list widget and model are never recreated, selection is preserved, and cue/playback/prerender state continues uninterrupted.
- Persisted collapsed state, expanded dock width, expanded splitter size, and last dock area. Carets follow left/right docking and use vertical direction while floating.
- Avoided dock-width changes during collapse to prevent OBS layout jumps; floating expansion restores the saved width.

## Development Version 123 — Structural & Invisible Character Recognition

- Extended learned regex inference to recognize paragraph/newline boundaries, CRLF, LF, CR, tabs, form feed, vertical tab, spaces, repeated spaces, and Unicode line/paragraph separators.
- Added exact structural escaping (`\x20`, `\t`, `\r\n`, etc.) instead of collapsing invisible characters into ambiguous whitespace matches.
- Added recognition for non-breaking and other Unicode invisible spacing characters while preserving their exact UTF-8 form.
- Prevented learned prefix rules from crossing line or paragraph boundaries.
- Fixed punctuation-followed-by-newline inference so the newline is not incorrectly absorbed into the punctuation delimiter.
- Added standalone regression coverage for Windows/Linux line endings, tabs, repeated spaces, NBSP, Greek/Unicode text, and U+2028 line separators.

## Development Version 122 — Learned Regex Auto-Formatting (Milestone 1)

- Added **Learn formatting from text** to Auto Styling Rules.
- Infers reusable regex rules from manually formatted canonical rich-text runs.
- Supports delimiter-based paragraph prefixes such as speaker names before `:`, `|`, `-`, `)` or `]`.
- Added regex full-match/capture-group evaluation with fail-closed invalid-pattern handling.
- Learned rules preserve their inferred character-format mask without requiring a style preset.
- Persisted regex pattern, capture group and case-sensitivity in title JSON.
- Added Unicode/Greek regression coverage proving one formatted speaker prefix can style subsequent rows.

## Development Version 121 — Temporal Graph Editor and Manual Velocity Handles

- Added a dedicated Graph Editor mode to the timeline with switchable Value Graph and Speed Graph views, sub-frame curve sampling, final evaluator values, current-time indication, and correct layer-local-to-timeline time mapping.
- Added per-keyframe incoming/outgoing temporal influence and speed, linked/broken temporal tangent state, and Linear, Hold, Auto Bezier, Continuous Bezier, and Manual Bezier modes while preserving legacy segment easing until an explicit velocity edit is made.
- Added deterministic cubic temporal evaluation in real time/value space with Newton iteration plus bisection fallback. Time influences remain single-valued, while property values and speeds remain unclamped to support negative values and overshoot.
- Added direct incoming/outgoing velocity-handle dragging in Value and Speed graphs. Alt-drag breaks only the edited temporal pair; linked handles preserve paired speed/influence editing.
- Added Easy Ease, Easy Ease In, Easy Ease Out, and a numeric Keyframe Velocity dialog for mode, incoming/outgoing influence, and incoming/outgoing speed.
- Added marquee and Shift multi-keyframe selection, relative multi-edit for keyframe time/value and velocity deltas, graph zoom, pan, Fit Graphs, and Fit Selection in both axes.
- Added the temporal interpolation and velocity commands to the ordinary timeline keyframe context menu as well as the Graph Editor context menu.
- Routed scalar, vector Position, scalar-group, and numeric extension animation through the same temporal evaluator used by editor playback, OBS output, and prerender/cache rendering.
- Persisted temporal velocity metadata in title JSON, retained extension metadata during keyframe value updates, and included all temporal fields in cache fingerprints.
- Kept graph edits on the timeline's existing undo stack: drag operations commit once on release, while mode, Easy Ease, and numeric-dialog edits create immediate title snapshots.
- Preserved independently authored 0–100% incoming/outgoing influences without silent pair renormalization, keeping displayed graph handles identical to the temporal cubic used for final evaluation.
- Stabilized multi-keyframe crossing edits by preserving extension-track index identity during drag and remapping the selected keys only after the release-time sort. Legacy easing presets now explicitly exit velocity mode on native and extension tracks.
- Defined deterministic endpoint behavior even for sub-epsilon keyframe intervals and expanded finite-difference checks so the Speed Graph derivative agrees with the Value Graph curve.
- Expanded standalone animation tests and added a temporal Graph Editor source contract covering overshoot, negative values, very short intervals, UI interactions, persistence, cache invalidation, extension properties, and shared evaluation.

## Development Version 120 — On-Canvas Motion Paths

- Expanded the selected animated layer overlay into a directly editable final-space motion path with keyframe vertices, incoming/outgoing Bezier handles, current-position marker, and direction indication.
- Added direct vertex dragging and tangent dragging on the canvas. Vertex edits are inverse-mapped through the parent/group hierarchy into layer-local Position values; tangent edits remain layer-local and support Shift 45-degree constraints and Alt-drag break behavior for only the selected tangent pair.
- Added double-click path subdivision. Cubic segments use deterministic de Casteljau splitting so the inserted keyframe lies on the existing curve without moving the surrounding authored vertices.
- Added canvas and timeline context actions for Linear, Auto Bezier, Continuous Bezier, Manual Bezier, Rove Across Time, Break Tangents, and Join Tangents.
- Added deterministic roving-time redistribution for interior Position keyframes, persisted the rove flag in title JSON, retained it through keyframe copy/paste, and included it in cache fingerprints.
- Added motion-vertex snapping to user guides and other keyframe positions while excluding descendants when editing their parent/group, avoiding self-hierarchy snap feedback.
- Hid editable vertices/handles during inline text editing and on locked layers, while retaining a lightweight read-only path/current-position display.
- Kept hover highlighting overlay-only: handle/path hover invalidates the selection overlay rather than dirtying the title model or requesting a full rendered canvas frame.
- Added release-time undo snapshots for vertex/tangent drags and immediate snapshots for path insertion, interpolation mode, rove, and tangent break/join actions.
- Expanded animation and source-contract regression coverage for curve subdivision, stable keyframe vertices during mode changes, roving timing, transformed-space editing, snapping exclusions, and input wiring.

## Development Version 119 — Spatial Bezier Keyframes Core

- Separated temporal easing from spatial path interpolation for animated vector properties while retaining the existing temporal keyframe controls.
- Added layer-local incoming and outgoing spatial tangents, linked/broken state, and Linear, Auto Bezier, Continuous Bezier, and Manual Bezier modes to position keyframes.
- Added deterministic cubic Bezier evaluation in `AnimatedVec2Property`, shared automatically by the editor, prerender/cache pipeline, and OBS source output.
- Preserved backward compatibility: vector keyframes without spatial metadata deserialize as Linear and reproduce their previous straight-line motion exactly.
- Added canvas motion-path rendering and draggable incoming/outgoing handles. Shift constrains handle direction; Alt breaks a linked pair; linked handles preserve independent lengths.
- Added Position keyframe context-menu controls for spatial mode and Break/Join Tangents. Mode changes and tangent drags enter the existing undo/redo history.
- Persisted spatial tangents/mode/link state in title JSON and retained them through full-struct keyframe copy/paste.
- Kept tangent storage in layer-local coordinates and maps editing/display through parent/group transforms, preserving the authored curve under affine hierarchy transforms and nested composition placement.
- Added spatial metadata to cache fingerprints and cubic control hulls to dirty-region bounds so tangent edits invalidate prerenders and curved motion cannot be clipped by stale straight-line tile envelopes.
- Expanded standalone animation tests and added a source contract audit covering persistence, canvas editing, copy/paste, undo integration, cache invalidation, and shared evaluation.

## Development Version 118 — External Data Diagnostics Logging

- Added a dedicated **External Data** logging category covering provider lifecycle, asynchronous refreshes, parsing/publication, source states, field/table updates, table-to-cue synchronization, cue-cell resolution, and render-queue coalescing.
- Added Info, Debug, and Trace detail levels so normal sessions remain readable while diagnostics can follow individual source, field, row, layer, and cue paths.
- Added safe logging helpers that redact URL credentials/query strings and report value type, length, emptiness, and a deterministic fingerprint instead of raw external values or authentication tokens.
- Added UI action logs for source creation/removal/duplication, test/connect/disconnect/refresh, binding changes, mapping changes, and the actual Live Text Cue widget population path.
- Added a standard-library-only logging bridge so the external-data core remains testable without Qt/OBS, plus regression coverage for filtering, sanitization, redaction, fingerprints, and sink failures.
- Fixed table-managed Live Text Cue row changes in **Loop** and **Pause** modes: the OBS source now resolves the pending row through the same external binding/formatter path as immediate Play Once cues instead of applying the intentionally empty authored cell.
- Added cue-control diagnostics for requested/current/pending row, playback mode, transition phase, and the final source-side row commit, without logging raw cue values.

## Development Version 117 — Live Table Binding Preservation Fix

- Fixed mapped Live Text Cue rows being created with the correct count but blank values.
- `normalize_live_text_rows()` now validates generated cell bindings against the title's active column order instead of a moved-from temporary vector.
- Preserves `ExternalTableManaged` state, formatted live values, read-only behavior, and OBS/source runtime bindings when the cue table rebuilds.
- Added a runtime regression that reproduces the exact synchronize → normalize → dock display sequence.

## Development Version 116 — Live Table Value Resolution and Managed Cell State

- Fixed mapped Live Text Cue rows appearing blank while the table result preview contained values.
- Row-specific table runtime values now take precedence over same-path scalar fields or schema placeholders.
- Added an explicit `ExternalTableManaged` cue-cell state: mapped text/image cells are read-only and italic in the dock while remaining cueable.
- Added **Convert to editable value** and **Restore table-managed value** actions. Detached snapshots survive provider refreshes without changing the table mapping for other cells.
- Preserved the exact mapped cell value across transient provider-registry rebuilds.
- Added regression coverage for scalar shadowing, read-only state, detachment, authored-value preservation, and restoration.

## Development Version 115 — Live Table Value Resolution and Cue Styling

- Fixed table-managed Live Text Cue rows remaining blank when a provider exposed valid table cells but did not publish an identical row-specific scalar field key.
- Added a runtime-only authoritative value to generated table cell bindings; it is rebuilt from each table snapshot and is never serialized over authored cue data.
- Routed the runtime value through the same `ExternalDataManager::resolve()` pipeline, preserving connection state, keep-last-value, formatter, empty-value, fallback, and field-default behavior.
- Ensured `apply_live_text_runtime_binding()` carries the table value into editor playback and the OBS source path, not only the dock preview.
- Included runtime table values in unchanged-update comparison so a value-only table refresh updates cue widgets/output without rebuilding unrelated rows.
- Displayed table-managed values in italics inside the Live Text Cues table as a visual origin indicator; output typography remains unchanged.
- Added regression coverage for table snapshots with no scalar row paths and for value-only table updates.

## Development Version 114 — Live Table Cue Value Display Fix

- Fixed source-managed Live Text Cue rows appearing blank even though their generated external bindings were valid.
- Bound text and image cue cells now display the resolved, formatted live provider value in the dock.
- Preserved the authored/fallback cell value separately, so live display refreshes never overwrite authored cue data.
- Added a regression contract requiring table-mapped cells to resolve through `effective_live_text_cue_value()` when their widgets are created.

## Development Version 113 — External Table to Live Cue Mapping

- Added automatic table snapshots for JSON arrays, nested JSON arrays, CSV data rows, local text, and manual/internal providers.
- Added **Populate from external table…** to the Live Text Cues data menu with source/table selection and live result preview.
- Added per-column mapping from provider table fields to exposed cue columns, using the same formatter, fallback, and live-value pipeline as ordinary external bindings.
- Added Replace, Append, and Synchronize update modes, optional starting row/row limit, empty-row filtering, and preservation of manual cue rows.
- Added stable row identity from a selected field or provider row index so synchronized updates retain the correct cue row when source ordering changes.
- Generated cells remain real external-data bindings, retain last-known/fallback behavior, update without authored-value mutation or undo commands, and display the existing bound-cell indicators.
- Added asynchronous provider refresh integration, table-update coalescing, unchanged-snapshot suppression, source-managed row cleanup, and current/pending cue remapping by stable row ID.
- Added runtime and structural regression coverage for table discovery, mapping, shared formatting, row synchronization, cleanup, and UI integration.

## Development Version 112 — Automatic External Field Discovery

- External JSON file, HTTP JSON, WebSocket, and CSV providers now discover every scalar field on each successful update even when schema overrides already exist.
- Discovered nested JSON paths, array indexes, CSV headers, and numeric CSV columns appear immediately in binding popups and existing settings rows without reopening the dialog.
- Reworked **Fields** into an optional schema override table with explicit pinning, display-name/type customization, manual values, and live values.
- Binding a discovered field from a layer property, provider binding row, or live cue cell automatically pins its inferred schema so it remains available while offline.
- Authored/pinned field types remain stable, while unpinned discovered fields may follow genuine provider type changes until they are pinned.
- Unchecking/removing a schema override now releases its fixed type/alias on the next provider synchronization without discarding the last discovered value.
- Added runtime and structural coverage for discovery, auto-pinning, offline placeholders, JSON/CSV discovery with overrides, and unchanged authored-value behavior.

## Development Version 111 — External Data UI and Formatting

- Completed External Data Source Settings with a state-aware source list, add/remove/duplicate actions, provider-specific connection settings, test connection, manual refresh, live field values, timestamps, errors, and refresh status.
- Added serialized refresh behavior per source: refresh on cue, refresh continuously, or refresh manually.
- Added a reusable external-data binding popup for text and image properties with source, field, typed fallback, live raw/formatted preview, and provider state.
- Added a shared structured formatter pipeline for prefix/suffix, decimal places, thousands separators, text case, date/time formatting, conditional replacement, and empty-value behavior while retaining legacy formatter compatibility.
- Added external-data bindings to live text/image cue cells, including refresh-on-cue behavior and runtime-only overrides that preserve authored cue and layer values.
- Added visible bound-state indicators on property buttons, cue cells, and layer rows.
- Added backward-compatible serialization for formatter configurations, refresh modes, and stable cue-cell bindings.
- Added runtime coverage for formatting, cue bindings, authored-value preservation, and a Development Version 111 UI contract test.

## Development Version 110 — Isolated Qt WebSockets Bootstrap

- Downloads the official `qt/qtwebsockets` module automatically when it is absent from the OBS Qt6 dependency bundle.
- Pins the downloaded source to the exact detected Qt patch version (`v<Qt6_VERSION>`) to prevent ABI mismatches.
- Configures, builds, and installs the module through a separate `ExternalProject` CMake process.
- Prevents the module's internal `find_package(Qt6)` from colliding with vcpkg/OBS imported targets such as `Threads::Threads`.
- Disables examples, tests, benchmarks, manual tests, and standalone tests so Qt Quick is not required.
- Exposes the installed static library to the plugin through an imported `Qt6::WebSockets` target.
- Reuses the versioned `_deps/qtwebsockets-<Qt version>` source, build, and install cache.

# Changelog

## Development Version 108

- Fixed Windows OBS dependency discovery so Qt6 is resolved from `lib/cmake/Qt6` and other supported SDK layouts.
- Prevented a detected Qt6 OBS SDK from silently falling back to Qt5.
- Split Qt WebSockets discovery from the base Qt modules to produce an accurate missing-component diagnostic.


## Development Version 108 — Asynchronous External Data Providers

- Added a common `IExternalDataProvider` lifecycle with connect, disconnect, refresh, validation, state, and error reporting.
- Added providers for JSON files, CSV files, HTTP/HTTPS JSON endpoints, WebSocket feeds, local text files, and manual/internal data tables.
- Moved every provider, polling timer, file read, HTTP request, and WebSocket connection to a dedicated worker thread so the UI and OBS render thread remain non-blocking.
- Added nested JSON field paths and array indexing, JSON root selection, CSV row selection, first-row headers, and field-to-column mapping.
- Added HTTP headers, bearer-token authentication, configurable timeout, retry count, exponential retry backoff, and refresh intervals.
- Added WebSocket JSON message parsing, automatic exponential reconnect, explicit connect/disconnect/refresh controls, and configurable last-known-value behavior.
- Added provider-side rate limiting and latest-value coalescing before the existing manager/render queue, while unchanged values remain suppressed by `ExternalDataManager`.
- Added `Connected`, `Updating`, `Disconnected`, `Error`, and `Stale` runtime states without waking rendering when an informative state transition cannot change the effective value.
- Added a provider settings dialog with source configuration, field definitions, manual values, CSV mappings, request options, live state/error/last-update reporting, and direct provider-field bindings to text/image layer properties.
- Persisted provider configuration with titles while keeping current values, runtime errors, and connection state transient and backward compatible.
- Added shutdown synchronization, last-known-value behavioral coverage, and a Development 107 provider contract audit.

## Development Version 106 — Provider-neutral External Data Core

- Added the central, thread-safe `ExternalDataManager` with provider-neutral source schemas, typed fields, current values, source/field timestamps, and connection/error state.
- Added external field types for string, integer, float, boolean, color, date/time, image/file paths, and URLs.
- Added optional per-layer property bindings with source ID, field path, formatter, and binding fallback while preserving the ordinary authored property as the final fallback.
- Persisted external source definitions and layer bindings in title/project JSON without persisting runtime current values or connection state, preserving compatibility with titles that have no external-data keys.
- Wired `text.content` and `image.path` through transient effective-value resolution in the editor, OBS source renderer, scene-mask paths, compatibility/GPU image paths, and cache identity.
- Added a provider-free mock update API, editor refresh callbacks, and a coalescing thread-safe update queue consumed on the OBS render/tick path.
- Suppressed notifications, render work, runtime revision changes, and cache invalidation when a provider repeats an unchanged value.
- Added behavioral and source-contract tests for authored/live/fallback resolution, connection loss, typed updates, mock fields, serialization, and render-queue coalescing.

## Development Version 105 — OmniaTV branding, v0.8.7-alpha, application icon, and documentation consolidation

- Updated the public version to `v0.8.7-alpha` and Development Version 105 across CMake, runtime build metadata, packaging, and dependency manifests.
- Replaced the previous personal credit with **Developed by: omniatv**.
- Added theme-aware normal/inverted OmniaTV logos to the About dialog and linked the logo to `https://omniatv.com`.
- Added the Flux Motion application icon and applied it to editor windows and plugin-owned dialogs that previously inherited the OBS application icon.
- Rebuilt the README as a concise project overview and removed stale, duplicated, and personal branding references.
- Consolidated the large collection of one-off documentation notes into seven canonical thematic documents plus this index/changelog set.
- Expanded `.gitignore` for CMake/build outputs, IDE files, generated packages, platform artifacts, caches, logs, temporary files, and local configuration overrides.
- Folded the obsolete standalone PowerShell manifest-fix notes into the maintained build documentation and removed the redundant root text files.

## Development Version 104 — Effects Panel Spacing, Bottom Toolbar and Dock Names

- Removed the extra gap between every effect header and its first setting while preserving the shared side and bottom panel insets.
- Moved the Effects toolbar below the scrollable effect stack, keeping **Add Effect** and **Respect Masks** permanently accessible at the bottom of the dock.
- Renamed the effect-stack dock to **Effects Settings** and the presets dock to **Effects and Presets**, including their Window-menu actions.

## Development Version 103 — Qt6 Title Thumbnail Repaint Compile Fix

- Removed the per-item `QListWidget::visualItemRect()` repaint path from title cue-state refreshes, eliminating the reported MSVC/Qt6 member-resolution compile failure.
- Title thumbnail cue-state changes are now accumulated and trigger one safe viewport repaint after all item roles are updated.
- Preserved immediate cue/uncue thumbnail feedback while reducing redundant paint requests for title lists with multiple items.

## Development Version 102 — Panel-based Effects Stack and Persistent Inspectors

- Replaced the separate Effects Stack list and single Effect Settings editor with one panel-based stack: every effect is shown as its own collapsible settings panel and the visible panel order is the actual render-stack order.
- Reduced the Effects toolbar to **Add Effect** and **Respect Masks**, removing the old Remove, Duplicate, Move Up, and Move Down buttons.
- Added an enable switch directly in every effect header, between the drag handle and effect name, and removed the duplicate Enabled control from the settings body.
- Added a compact overflow menu immediately before the caret with **Duplicate Effect**, **Delete Effect**, **Move Up**, and **Move Down** actions.
- Connected effect-panel drag-and-drop reordering directly to the layer effect model while preserving the selected effect, keyframe bindings, canvas handles, and effect-specific controls.
- Persisted collapsible-panel expanded state and user-defined panel order across editor sessions through `QSettings`; effect order continues to persist in the title/layer model rather than editor preferences.
- Replaced the layer-list expand/collapse icons with the same native caret renderer used by the inspector panels, including the three-state group expansion marker.
- Updated the effects interaction regression contract for the new list-free panel stack and persistent shared panel/caret infrastructure.

## Development Version 101 — Unified Collapsible and Reorderable Inspector Panels

- Made the compact switch track and thumb smaller, lower-contrast, and less visually dominant while retaining clear checked, mixed, hover, focus, and disabled states.
- Added the reusable `FxmCollapsiblePanel` widget with a compact After Effects-style header, a drag handle at the left, and an expand/collapse caret at the right.
- Added true sibling-panel reordering by drag-and-drop from the header handle, including before/after drop indicators and preservation of layout stretch factors.
- Unified panel chrome, spacing, body shading, borders, accent separators, and content margins between the Properties and Effects inspectors.
- Migrated the Properties inspector sections, Shape/Image custom sections, gradient Presets/Stops, Effects Stack, Effect Settings, selected effect controls, and nested effect element controls to the common panel widget.
- Preserved dynamic section visibility, existing controls/signals, keyframe behavior, explicit custom colors, and active OBS palette inheritance without adding a third-party runtime dependency.

## Development Version 100 — Compact After Effects-style Qt Controls

- Added native compact Qt widgets that follow the active OBS palette instead of installing an application-wide theme or overriding explicitly customized colors.
- Replaced editor, Effects, Preferences, Transitions, Dock-dialog, and Prerender checkboxes with sleek switch controls.
- Added circular direction controls with synchronized numeric input for Drop Shadow, Inner Shadow, and Long Shadow angles.
- Added strong theme-accented section dividers throughout the layer Properties panel and tightened panel spacing for a denser After Effects-like workflow.
- Added a reusable internal widget layer for future compact controls without adding a third-party runtime dependency.
- Reviewed MIT-licensed Qlementine and LGPL-2.1 Qt Advanced Stylesheets; neither is bundled because both are designed to own the application-level style and could interfere with OBS theme inheritance.

## Development Version 099 — Qt6 Thumbnail Repaint Compile Fix

- Fixed the Titles & Graphics cue-state repaint call for Qt6/MSVC.
- `visualItemRect()` is now invoked on the owning `QListWidget`, rather than incorrectly on `QListWidgetItem`.
- Preserves the lightweight immediate thumbnail-state repaint without triggering preview or cache regeneration.

## Development Version 098 — Thumbnail Ending-State Cleanup

- Synchronized the **Titles and Graphics** thumbnails with the live cue runtime state.
- Active cues use the same red border as the active Live Text Cue row.
- Cue-to-cue transitions and manual uncue/outro playback switch the thumbnail border to yellow until the outgoing title reaches its authored end.
- Cues waiting to become active use a green border, while inactive titles have no cue-state border.
- Applied the state consistently in both list view and icon view, including titles without exposed Live Text fields.

## Development Version 096 — Live Cue Header Runtime Timer

- Added a live runtime counter at the right side of the **Live Text Cues** header.
- Pause and Loop cues show elapsed on-air time as `MM:SS`.
- Cue-to-cue transitions and full uncue outros show the exact remaining time as `MM:SS:T` (minutes omitted below one minute).
- Play Once cues show the remaining time from their appearance until the authored title end.
- The counter is driven by the active OBS source playhead and disappears when no selected title source is running.

## Development Version 095 — Yellow Uncue/Outro Cue State

- Added a dedicated yellow `ending` visual state for the active Live Text Cue row while its uncue/outro is playing.
- The Cue button/icon, cue-row background, and cue-row border switch from red to yellow immediately when uncue begins.
- The yellow state remains active through Play Once, Pause, Loop, and Ping-Pong Loop outro playback and clears only when the authored end is reached and `When cue ends` is applied.
- Kept queued cues green and normally active cues red, with the ending state taking visual priority over the active state.
- Added a regression contract covering the yellow state source, priority, dock refresh paths, static title-only cue rows, and row decoration.

## Development Version 094 — Continuous Uncue Playback and Live Cue Text Snapshots

- Changed manual uncue so Play Once, Pause, Loop, and Ping-Pong Loop titles continue forward from the exact current on-air frame instead of seeking to the pause/loop boundary.
- Kept the cue active throughout the outro and applied the configured `When cue ends` behavior only after the title reaches its authored end.
- Fixed title-only cue toggles for titles without exposed live-text fields by preserving their synthetic cue row until the outro completes.
- Made the active cue's already-applied text/image values authoritative for cached playback, so editing a live-text cue row does not alter the current cue or its uncue; the edited values appear on the next cue.
- Kept cue-to-cue Pause/Loop transitions using their authored hand-off points while limiting current-frame continuation specifically to manual uncue.
- Added a dedicated uncue/playback-text-snapshot regression contract covering dock actions, title hotkeys, source state transitions, cache lookup, and end behavior.

## Development Version 093 — Layer-Space Effects Stack and Background Bounds Fix

- Kept ordinary layer effects on the transform-neutral padded layer raster instead of rerouting the complete stack to a full-canvas pass when Shadow, Glow, Outline, Blur, Bloom, or Long Shadow is present.
- Fixed initial Background Color bounds by versioning the base raster retention mode and translating layer-box/clip metadata whenever an alpha-only raster is cropped.
- Made Background Color geometry explicitly layer-relative, preventing a later Shadow/Glow pass from expanding the fill across the complete canvas.
- Added an affine layer-space basis for full-canvas group, matte, and adjustment paths, so spatial effects follow layer translation, scaling, rotation, and parent transforms.
- Anchored 4-Color Gradient, Lens Flare, Vignette, Noise, and Roughen Edges to layer space while keeping texture sampling in surface space.
- Added missing local support padding for Lens Flare and Roughen Edges, excluded affect-behind effects from unnecessary local-raster expansion, and versioned GPU/disk cache identities.
- Added a dedicated effects-stack layer-space/bounds regression contract and reran the existing effect, procedural shader, cache, modularity, and MSVC include-order audits.

## Development Version 092 — MSVC GPU Cache Alias Compile Fix

- Fixed the MSVC `C2267`/`C2601` failure caused by defining `alias_global_gpu_frame_locked()` between implementation fragments that form one contiguous function body.
- Kept the GPU cache alias helper visible through a top-level forward declaration and moved its definition after all split `title-source` function continuations are closed.
- Added a structural regression audit that verifies the include order, top-level declaration, and balanced combined translation unit.

## Development Version 091 — Linux Text Visibility, Typing Refresh, and Temporal Cache Reuse

- Preserved Linux/FreeType `QImage::Format_Alpha8` glyph coverage explicitly before SDF generation, preventing valid glyph masks from becoming blank during GPU text rendering.
- Replaced partial dynamic-atlas writes with complete uploads of smaller 1024×1024 R8 pages, preventing discard-mapped atlas contents from producing random glyph fragments while typing.
- Made text edits cancel delayed presentation work and force an immediate full-canvas GPU present, so typed characters and text-box geometry appear without waiting for a coalesced playback/editor tick.
- Connected exact evaluated visual-state deduplication to the prerender worker. The first frame of an unchanged state is canonical; subsequent frames share its GPU tiles and create metadata-only SSD aliases instead of repeating layout, effects, compositing, readback, and compression.
- Resolved an equivalent canonical frame already waiting for GPU readback before submitting another one, eliminating redundant in-flight renders.
- Restricted temporal reuse to fully deterministic/cacheable titles and excluded animated asset timelines whose local playback mapping can differ from root timeline time.
- Bumped the cache ABI to invalidate older blank/corrupted Linux text payloads and added a dedicated Development Version 091 structural regression audit.

## Development Version 090 — Text Position-Map Compile Fix

- Fixed the Windows/MSVC build regression introduced by the Development Version 089 UTF-8/UTF-16 position-map optimization.
- Updated the remaining rich-text range call to pass the shared `EditorQtUtf8PositionMap` required by the new helper signature.
- Replaced stale editor-side `rich_byte_offset_from_qtext_position()` calls with the local position map already built for the `QTextDocument` conversion.
- Replaced stale source compatibility-renderer `qtext_position_from_rich_byte_offset_source()` calls with the local `SourceQtUtf8PositionMap`, resolving the cascading `std::min`/`std::max` template errors.
- Added regression-contract coverage that rejects the removed conversion helpers and verifies that editor and source compatibility paths reuse their local maps.

## Development Version 089 — Linux Text Rendering and Text Pipeline Performance Audit

- Retained the exact physical `QRawFont` selected by Qt shaping and Fontconfig and reused that face in the GPU glyph atlas. Linux fallback glyphs are no longer reconstructed only from family/style names, and visible glyphs that cannot produce an alpha map fall back to the Qt compatibility renderer instead of disappearing.
- Reworked inline typing around canonical UTF-8 range edits. Ordinary insertion, Backspace, Enter, and single-character replacement no longer rebuild the complete `QTextDocument`, synchronously render a frame, or repeat document-size and cursor conversions for the same keystroke.
- Coalesced Properties panel, Timeline model, live-output publication, and canvas invalidation during continuous typing while preserving immediate editor feedback and the normal full commit path at the end of editing.
- Added per-paragraph UTF-8/UTF-16 position maps across shaping, source compatibility rendering, and editor adapters, removing repeated string slicing/conversion work for Unicode, emoji, RTL, and large rich-text documents.
- Changed the glyph atlas to upload only newly dirtied rectangles instead of retransferring an entire 2048×2048 page for each uncached glyph, and reduced transient layout allocations through reserved/merged GPU batches and binary paint-run lookup.
- Removed repeated full `Layer` copies and redundant rich-text normalization passes from layout/evaluation hot paths. Auto-style state is now indexed by Unicode code-point boundaries, and transient editor drafts bypass the shared process cache while the retained cache is byte-bounded.
- Audited text property propagation from model through Qt layout and GPU paint runs. Fixed mixed-range underline/strikethrough handling, connected stroke antialiasing to the shader, preserved every fill/gradient/stroke field, and route unsupported miter/bevel inline joins through the exact compatibility raster path.
- Added standalone Unicode/model/layout tests and a text-pipeline performance contract covering Linux font identity, glyph upload behavior, canonical range editing, coalesced editor refreshes, property propagation, and removal of known quadratic conversion paths.

## Development Version 088 — Independent Monitor-Rate Editor Presentation

- Replaced the editor canvas `obs_display_t` presentation path with an editor-owned libobs GPU swap chain. OBS displays are rendered from the main project video loop, so timer-only throttling could not make a stopped editor present faster than the project frame rate.
- Editing, direct manipulation, text editing, keyframe changes, and timeline scrubbing now present from the Qt monitor-paced path and are capped by the refresh rate of the monitor hosting the editor.
- Real transport playback remains isolated: only project-rate transport ticks authorize a canvas present while playback is active. Generic Qt repaints cannot add extra playback frames.
- Retained the existing GPU artwork renderer, overlays, selection inversion, color-space updates, resize handling, and explicit shutdown teardown while changing only swap-chain ownership and presentation scheduling.
- Counted any graphics-backend present wait inside the monitor cadence and coalesced pending presents, preventing double waits and runaway repaint queues on high-refresh displays.
- Added regression coverage that rejects reintroduction of `obs_display_create()` for the editor canvas and verifies independent swap-chain creation, monitor-paced editing, project-paced playback, and shutdown destruction.

## Development Version 087 — Monitor-Capped Editing and Project-Rate Playback

- Canvas artwork refreshes while editing are capped to, and never scheduled above, the refresh rate of the monitor currently hosting the editor window.
- Timeline scrubbing is always treated as editing and never bypasses the monitor cap, even if transport state changes during the interaction.
- Playback refresh is driven only by the project/OBS frame rate, independent of monitor refresh rate.
- Removed the previous hidden 60 Hz transport ceiling, allowing 100/120/144 fps projects to preview at their configured project cadence when rendering can keep up.
- Render-cost pacing remains active, so expensive frames may run below the monitor cap without creating an event backlog.
- Moving the editor between monitors recalculates the editing cadence safely after the window/layout transition.

## Development Version 086 — Shutdown Safety and Effect-Handle Performance Audit

- Added an explicit, idempotent editor shutdown phase that stops all editor/dock timers, disconnects cross-widget Effects Stack ↔ Canvas callbacks, and prevents late playhead, autosave, paint, or property updates from running during Qt child destruction.
- Added explicit Canvas GPU teardown before widget destruction, including OBS display callbacks/textures and the editor GPU render session, with shutdown guards on queued title/playhead updates and draw callbacks.
- Deduplicated effect canvas-handle publication and Canvas ingestion, avoiding repeated JSON rebuild-driven repaints and GPU invalidations when evaluated handle positions have not changed.
- Kept both possible Effects Stack/Canvas construction orders safe with unique signal connections, preventing duplicate handle updates.
- Moved dock removal and frontend callback cleanup into the OBS-supported shutdown window and prevented frontend API calls after `OBS_FRONTEND_EVENT_EXIT`.
- Added deterministic shutdown of the asynchronous title-store save worker before the final synchronous save, preventing stale writes and plugin code execution during module unload.
- Added regression coverage for shutdown guards, timer/signal teardown, GPU resource release, frontend lifecycle rules, save-worker joining, and handle-update deduplication.

## Development Version 085 — Stable Effect Handle Dragging and Keyframed Positions

- Preserved the active effect canvas handle across live Effects Stack refreshes, preventing a point drag from falling through to the layer resize/scale interaction after the first mouse movement.
- Re-evaluated and republished effect canvas-handle positions whenever the playhead changes, so native and extension point controls follow their keyframes in real time.
- Restored the original unrestricted radial Fill Gradient center and radius editing. Only the focal point remains constrained just inside the active radial circle, while a non-zero radius prevents inverted or malformed gradients.
- Added regression coverage for active-handle identity during drag, playhead-driven handle refresh, unrestricted center/radius motion, and focal-circle safety.

## Development Version 084 — Effect Handle Wiring, Vector Editing and Stable Stack Selection

- Connected the Effects Stack dock to the already-created canvas in the editor's real construction order, restoring visible and draggable on-canvas controls for native and extension effect positioning properties.
- Added independent draggable **X** and **Y** labels to extension point/vector controls, so both components support the same drag-to-edit workflow and undo grouping.
- Prevented transient `currentRowChanged(-1)` notifications during Effects Stack rebuilding from replacing the selected effect with the first stack item after an edit.
- Constrained radial Fill Gradient controllers: the center stays inside the fill bounds, the radius handle cannot move beyond the bounds in its active direction, and the focal handle remains inside the radial circle to avoid malformed or inverted rendering.
- Added regression coverage for late canvas/dock signal wiring, vector-axis drag labels, stable effect selection, and radial handle constraints.

## Development Version 083 — Unified Effect Keyframes and Canvas Position Handles

- Unified built-in and extension effect diamonds with the editor's standard keyframe behavior: inactive/animated/active states, click-to-toggle at the playhead, automatic insertion while editing an animated property, right-click **Delete All Keyframes**, and timeline easing/interpolation support.
- Added native and extension effect-property tracks to the layer timeline with unique per-effect-instance identities, including copy, cut, paste, multi-selection, drag retiming, easing changes, and keyframe deletion.
- Grouped native effect color channels into a single timeline color track, so moving, copying, deleting, or changing easing updates Alpha, Red, Green, and Blue keyframes together.
- Added a confirmation modal when removing an effect that contains native or extension keyframes. Confirming removes the effect and all of its associated timeline tracks; cancelling preserves both.
- Added transform-aware on-canvas handles for every extension point/position parameter and for native Lens Flare/Vignette centers plus Background Color gradient center and focal point. Handles follow layer rotation, scale, parenting, and group transforms and respect auto-key animation.
- Applied extension keyframe easing consistently in both the GPU presentation path and compatibility compositor, including Linear, Ease In, Ease Out, Easy Ease, custom Bezier, and Hold interpolation.
- Added regression coverage for effect-instance timeline tracks, grouped color animation, deletion warnings, extension clipboard operations, easing, and layer-local canvas-handle conversion.

## Development Version 082 — Content-Clipped 4-Color Gradient and Visible Effect Keyframes

- Changed **4-Color Gradient** to use the original layer artwork alpha as its matte, so the generated colors are clipped to text glyphs, transparent images, shapes, and composited group content instead of filling the layer bounding rectangle.
- Fixed effect keyframe controls that were backed by animation data but rendered as font-dependent Unicode glyphs; they now use the same reliable SVG diamond icons as the main Properties panel.
- Replaced font-dependent Unicode diamond characters with the editor's actual active/inactive keyframe SVG icons across built-in, extension, color, point, enum, boolean, and compound-element effect properties.
- Preserved direct keyframe toggling, playhead-state updates, animated-value editing, accessibility labels, and extension keyframe persistence.

## Development Version 081 — Effects Cleanup, Keyframes and 4-Color Gradient

- Removed obsolete effect controls that were still visible although the GPU renderer ignored them, including legacy blur-type selectors, nonfunctional blend-mode selectors, and unsupported outline/background corner variants. Backward-compatible serialized fields remain readable but are no longer exposed as misleading controls.
- Added diamond keyframe controls beside every effect option backed by an animation property, including the new generated-gradient points, colors, Blend, Jitter, and Opacity controls.
- Repaired **Background Color** so it uses the layer raster bounds and animated padding/corner/gradient values while always compositing the original layer artwork above the generated background.
- Rebuilt **Emboss** as a directional relief effect driven by alpha and luminance, with functional Depth, Height, Angle, Softness, and Opacity controls.
- Added **Built-in → Generate → 4-Color Gradient** with four movable points, four independently keyframeable colors, Blend, Jitter, Opacity, blending modes, and on-canvas point handles.
- Added regression coverage for the effect registry, extension schema, cleaned UI contract, shader uniforms, Background Color compositing, Emboss relief, and 4-Color Gradient animation bindings.

## Development Version 080 — Restored Consistent Canvas Scaling

- Restored the unified pre-074 scaling contract for every canvas object after it regressed in Development Version 079.
- Normal, Shift-constrained, snapped, rotated, and Alt centre-resize now all derive from the same pointer-defined local target rectangle.
- Scale-backed Text, Group, and Asset Layers receive the required position correction on every resize, not only while Alt is held, so the grabbed handle stays under the pointer and the opposite handle remains fixed.
- Size-backed shapes, images, and other layers preserve the animated origin as a fraction of the new dimensions instead of reusing stale absolute local offsets.
- Removed the competing second Alt-only position correction that could pull the object away from both the pointer and its intended anchor.
- Added a regression contract that rejects both the absolute-offset path and modifier-specific anchor correction while preserving the fixed animated Asset Layer envelope introduced in Version 079.

## Development Version 079 — Static Animated Asset Bounds and Direct Asset Editing

- Animated Asset Layers now calculate a fixed envelope from the union of the source composition across its complete animation, including keyframed transforms, animated size/origin/free-transform corners, nested asset content, timed visibility, and general slide/scale transitions. The selection box no longer changes with the playhead; the animation runs inside one stable bounding box spanning its spatial extremes.
- The Assets library context menu now includes **Edit Asset** alongside Insert and Delete.
- Right-clicking a single Asset Layer on the canvas now exposes **Edit Asset** for its linked source asset.
- When the current title has unsaved changes, Edit Asset opens a modal with **Save**, **Discard**, and **Cancel** before switching the editor to the source asset. Saving the asset preserves its asset identity, so it remains hidden from Titles & Graphics and available in Libraries → Assets.

## Development Version 078 — Independent Asset Playback Controls

- Asset Layers now expose synchronized/independent playback only when their nested composition contains real timeline animation: keyframes, transitions, timed visibility, animated effects, or another animated Asset Layer.
- Independent Asset Layers use a per-instance monotonic runtime clock in both the editor and OBS output, so scrubbing or stopping the parent title playhead no longer changes their animation time.
- Assets saved with Pause playback expose **Pause for** with an `HH:MM:SS:FF` timecode plus a complete-animation Loop toggle.
- Assets saved with Loop playback expose **Loop _N_ times** plus a complete-animation Loop toggle; restart and ping-pong loop areas are both preserved.
- Source duration, pause marker, loop area, loop type, playback mode, pause duration, and loop count are stored in each Asset Layer snapshot for reliable offline/nested playback.
- Static assets no longer show the synchronized/independent selector or any playback-only controls.

## Development Version 076 — Unified Libraries Dock

The former Styles dock is now named **Libraries** and contains a single ordered tab set: **Color Swatches**, **Gradients**, **Text Styles**, and **Assets**. Color Swatches has been moved into this tab set and its standalone dock/window action has been removed. The separate Animated Assets library has also been removed; the unified Assets tab now lists both static and animated asset titles while preserving each asset layer’s synchronized or independent playback behavior. Saved editor layouts are migrated to the new dock schema.

## Development Version 075 — Assets and Animated Assets

Complete titles can now be saved from **File → Save as Asset** and reused inside other titles as dedicated **Asset Layers**. The Styles dock includes searchable, categorized **Assets** and **Animated Assets** libraries with double-click insertion and drag-and-drop directly onto the canvas. Asset instances preserve exposed text/image overrides, effects, mattes, nested composition, group-style content bounds, and either synchronized title-time playback or independent ticker-style animation. Asset records are intentionally hidden from the Titles & Graphics dock and OBS title-source selector.

> **Alpha software:** This build is intended for development and testing. Keep backups of important projects and templates. File formats, UI behavior, performance characteristics, and internal APIs may still change before the stable release.

## Development Version 074 — Consistent Group Canvas Manipulation

- Group transform handles and transform calculations now use the same descendant-derived local bounds.
- Move, resize, rotate, snapping, Shift-constrained resize, and Alt centre-resize no longer operate from a synthetic centred rectangle.
- Asymmetric and nested groups manipulate from the exact canvas outline shown to the user.
- Group duplication drag keeps the copied hierarchy as the active gesture target.
- Development Version 074 is exposed through the editor title, dock/About UI, build metadata, and plugin log.

## Development Version 073 — Authoritative Duplicate Drag Target

- Alt+drag now keeps the duplicated layer IDs as the authoritative gesture target for every mouse-move event until release.
- Synchronous layer-list or timeline selection refreshes can no longer redirect the drag back to the original artwork.
- The duplicated layers remain selected when the drag completes.
- Development Version 073 is exposed through the editor title, dock/About UI, build metadata, and plugin log.

## Development Version 072 — Continuous Alt+Drag Duplication

- Alt+drag now transfers the active canvas gesture directly to the duplicated layer IDs.
- The duplicate continues following the pointer immediately after it is created; the original remains at its starting position.
- The behavior is shared by single layers, multi-selection, groups, and nested groups.
- Synchronous layer-list and timeline refreshes can no longer redirect the in-progress drag back to the original selection.
- Development Version 072 is exposed through the editor title, dock/About UI, build metadata, and plugin log.

## Development Version 071 — Group Alt+Drag Duplication

- Alt+drag on the canvas now duplicates complete group layers, including all nested child layers and nested groups.
- Internal group membership, parenting, transform-parent, and matte references are remapped to the duplicated hierarchy.
- Only the duplicated root selection is moved during the drag, preventing child transforms from being applied twice.
- Locked children inside an unlocked duplicated group are preserved as part of the copied container.
- Development Version 071 is exposed through the editor title, dock/About UI, build metadata, and plugin log.

## Development Version 070 — Clipping Matte Shape Coverage Fix

- Changed **Clipping Matte** extraction to use the matte source artwork shape at full compositor opacity instead of behaving like an Alpha Matte.
- Animated layer opacity, transition opacity, and parent/group compositor opacity no longer fade or disable the clipping region, including at 0% opacity.
- Preserved intrinsic image transparency and anti-aliased text/shape edges so the clipping boundary remains smooth and follows the actual artwork geometry.
- Separated clipping-shape textures from ordinary matte/artwork textures in the GPU cache. A visible clipping base still renders with its normal opacity, while its clipping shape remains opacity-independent.
- Applied the same behavior to ordinary layers, nested mattes, grouped layers, and Groups used as clipping sources.
- Development Version 070 is exposed through the editor title, dock/About UI, build metadata, and plugin log.

## Development Version 069 — Clipping Matte Artwork Visibility Fix

- Fixed **Clipping Matte** sources ignoring the three-state matte visibility control.
- **Hidden artwork, active as matte** now keeps the clipping source fully functional as the target mask without compositing the source artwork into the frame.
- **Visible artwork and active as matte** continues to composite the clipping source once as the base beneath the linked clipped target.
- The behavior is identical for root layers, grouped children, and Groups used as clipping sources.
- Development Version 069 is exposed through the editor title, dock/About UI, build metadata, and plugin log.

## Development Version 068 — Clipping Matte Composition and Toggle-State Fix

- Fixed **Clipping Matte** so the matte source is composited once as the visible base immediately before the first linked clipping target, independent of either layer's position in the stack.
- The linked target is then alpha-clipped against the completed matte source, including Group matte sources and Group targets.
- Fixed the Alpha/Luma/Clipping type toggle leaking `VisibleAndMatte` state after passing through Clipping. Alpha and Luma now resume correctly immediately, without disabling and re-enabling the target.
- Multiple layers may share one clipping source without repeatedly compositing the base layer.
- Development Version 068 is exposed through the editor title, dock/About UI, build metadata and plugin log.

## Development Version 066 — Group as Track Matte Source Fix

- Fixed the asymmetric GPU matte path where rendering a Group matte reused and overwrote the target layer's shared foreground texture before masking.
- Group matte sources now preserve the consumer texture, composite all descendants, publish the completed Group matte, and only then apply alpha/luma masking.
- The same protected path is used for normal layers, grouped children, adjustment coverage and affect-behind silhouettes.
- Development Version 066 is exposed through the editor title, dock/About UI, build metadata and plugin log.

## Development Version 064 — Group Target Matte GPU Feedback Fix

- Fixed a GPU read/write feedback hazard when a matted group result reused the shared mask render target after one of its children had already been masked.
- Mask inputs are now snapshotted into independent full-canvas textures whenever they alias the destination mask target, so the completed group can be masked reliably.
- The protection applies to group targets, child mattes and nested matte chains without changing the already-working group-as-matte-source path.
- Group effect order continues to follow the existing `Effects -> Matte` / `Matte -> Effects` setting.
- Development Version 064 is exposed through the editor title, dock/About UI, build metadata and plugin log.


## Development Version 059 — Unified Gradient Preset Swatches

- Qt 6 dialog capture compile fix: gradient preset naming now captures the popup by reference, avoiding const-parent conversion and deleted `QDialog` copy-constructor errors.

Gradient presets now use one persistent library across the entire editor. The Gradient Styles dock and every fill/stroke gradient popup display the same compact, color-swatch-style previews, including gradient type, opacity, intermediate stops, spread, angle, and transparency. Presets can be created directly from the active shape fill, text fill, layer stroke, or inline rich-text fill/stroke; applying a swatch immediately updates the current target. User presets can also be removed from the swatch context menu, while built-in presets remain protected. Changes are synchronized live between open editor surfaces, and import/export continues to use the shared Styles library.

## Development Version 057 — Refined Group Editing, Independent Parenting, and Matte Controls

Group and parenting workflows are now clearly separated: transform parenting no longer creates or modifies Groups, while parent/unparent operations preserve the layer's world-space appearance. Newly created Groups start collapsed, collapsed or keyframe-only Groups behave as single selectable containers, and fully expanded Groups expose their children for direct canvas and Effects-panel editing. Child effect stacks continue to render correctly inside Groups, including mask-aware ordering and affect-behind compositing. The update also prevents parents and Groups from snapping to their own descendants, adds three-state track-matte visibility, introduces three-state Group timeline expansion, aligns hierarchy-aware layer-list columns, and standardizes Parent and Matte selectors with numbered layer names.

## Development Version 128 — Auto Styling Merge API Compile Fix

- Declares `rich_text_auto_style_rules_equivalent` and `rich_text_merge_auto_style_rules` in the public rich-text header.
- Fixes the MSVC C3861 errors in `property-synchronization.inc` when loading, appending, or learning rules.
- Keeps rule merging and deduplication in the canonical rich-text model instead of duplicating logic in the properties panel.

## Development Version 127 — Rule Deduplication and Smart Generalization

- Prevents duplicate learned rules and duplicate rules during rule-set Append/Replace loading.
- Promotes equivalent examples to one reusable all-matches rule instead of creating repeated entries.
- Adds per-rule generalization policy: Smart merge, Exact structure only, or Always keep separate.
- Adds explicit duplicate-prevention and multiple-case application controls in Advanced options.
- Persists the new controls in title JSON and portable `.gsp-auto-style.json` rule sets.

## Development Version 126 — Smart Text Analysis and Learned Styles

- Generalizes formatted examples into semantic rules for time, date, numbers, email addresses and URLs.
- Infers fields at paragraph starts, between delimiters and after structural separators.
- Learned rules carry a complete inline style snapshot, so fill, stroke, font and other formatting remain portable without an existing preset.
- Added Clear All Rules actions to the main Auto Styling panel and Rule Editor, with destructive-action confirmation.

## Development version 145 — Audio Layer Data Model
- Added migration-safe `LayerType::Audio` and serializable audio clip properties.
- Added audio import for WAV, FLAC, MP3, AAC/M4A and video containers with audio streams.
- Added timeline waveform rendering, trim-safe clip timing, grouping/duplicate compatibility, and missing-media-safe loading.
- Audio layers do not expose visual transform/keyframe rows.

## Development version 146 — Audio Playback and OBS Integration

- Declared the FXM title source as an OBS audio-capable, controllable media source.
- Added a per-source asynchronous audio decode/cache runtime; decoding never runs in the render callback.
- Routed mixed stereo PCM through `obs_source_output_audio`, preserving OBS mixer volume, mute, monitoring, filters and routing.
- Added synchronized title-playhead transport and independent audio clocks, pause/resume continuity, immediate seek, looping, pan, gain and fades.
- Added OBS media-control callbacks for play, pause, restart, stop, seek, duration, current time and state.
- Added optional FFmpeg decoding for FLAC, MP3, AAC/M4A and audio streams in video containers, with a dependency-free PCM WAV fallback.
- Missing or undecodable media remains silent without crashing or repeatedly resetting the stream.
