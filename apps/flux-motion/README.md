# Flux Motion

**Flux Motion (FXM)** is a native C++/Qt broadcast-graphics application with a separately packaged **Flux Motion Plugin for OBS**. It combines multi-title projects, a dockable project workflow, layered 2D/3D editing, rich text, live data and cueing, audio/video layers, reusable nested graphics, native Stinger transitions, GPU rendering, and RAM/disk prerendering without browser sources.

**Current version:** `v0.8.19-1-alpha` · `Development Version 420`
**Suite label:** `2026 - v0.8.19-1-alpha`

This release separates the OBS plugin and Editor distributions,
adds OBS-only cache/prerender preferences and synchronizes the Flux Suite versions.

## Highlights since Development Version 281

### Development Version 417 multi-title projects and product naming

- Introduces versioned `.fxmproj` files containing an ordered, arbitrary number
  of independent titles/graphics while retaining legacy `.fxmt`/`.fxmp` reads.
- Adds the standalone Project panel, staged cancellable loading, reusable nested
  Title/Graphic Layers, and project-file/title selection in OBS source settings.
- Renames the products to Flux Motion and Flux Motion Plugin for OBS across UI,
  packages, installer metadata, documentation, and the Windows executable.

### Flux Suite packaging and OBS preferences

- Keeps Flux Motion out of the OBS plugin package and discovers its
  standalone installation automatically or through the OBS plugin preferences.
- Restricts OBS plugin preferences to shared cache/prerender settings and the
  standalone Editor executable path.

### Development Version 415 Flux Encoder executable override

- Adds an editable `FLUX_ENCODER_EXECUTABLE` path and file browser to Editor
  Preferences for development setups where automatic discovery is unavailable.
- Applies the override immediately and allows clearing it to return to the
  installer, running-service and `PATH` discovery flow.

### Development Version 414 Flux Encoder media export

- Adds File → Export as Media… while keeping it visibly disabled when Flux
  Encoder is not installed or running.
- Saves pending title changes and opens Flux Encoder's export window directly,
  reusing an existing Encoder process through a stable versioned IPC endpoint.
- Leaves formats, codecs, destinations, presets and render queues exclusively
  in Flux Encoder.

### Development Version 413 timeline chrome and keyframe indicators

- Expands the timeline scroll/zoom navigator directly after the active graph controls, removing the unused gap on its left.
- Replaces font-rendered `◆/◇` keyframe markers with consistently sized custom-painted diamonds that remain clear in Satoshi and OBS-hosted themes.

### Development Version 412 editor audio and timeline navigation

- Restores live standalone audio metering with linear-to-dB fallback polling while preserving native OBS meter callbacks.
- Replaces the old timeline zoom buttons and slider with a Premiere-style navigator: drag the body to scroll, either grip to zoom, the wheel to zoom around the pointer, or double-click to fit.
- Makes standalone tabs slightly slimmer and scrollbars substantially slimmer; the OBS dock continues to inherit OBS styling and typography.

### Development Version 411 Flux Motion visual identity

- Replaces the Adobe Dark, Light and System choices with the single default Flux Motion theme and its `#7838f5` accent.
- Loads the packaged Satoshi Regular, Medium, Bold and Black families (including italics) for Flux Motion, while the OBS dock inherits the host theme and font.
- Restores the supplied SVG's optical version spacing and adds Development Version and copyright lines to both About and splash artwork.

### Development Version 410 About artwork splash screen

- Shows the existing Flux Motion About artwork while the standalone Editor initializes libobs and loads the project.
- Reuses one version-aware SVG rendering path for both the splash screen and About dialog, including high-DPI output.

### Development Version 409 unified GPU and independent render sampling

- Runs the standalone Editor through the same libobs GPU compositor, shaders, blend modes, masks, 3D depth and effects used by OBS live output.
- Introduces an explicit per-session physical output extent and sample duration while preserving authored logical title coordinates.
- Scales geometry, GPU text/vector rasters and pixel-distance effects for the requested render target instead of assuming title pixels equal target pixels.
- Makes video frame selection a function of title/media time rather than project FPS, so the same timestamp resolves identically at every consumer cadence.
- Stages the libobs graphics runtime/data and validates D3D11 plus real 320x180 and 640x360 compositor readbacks with the Editor GPU smoke test.

### Development Version 408 preview performance and title format

- Adds editable Resolution and Frame Rate fields to Title Properties and persists the authored format with each title.
- Drives standalone timeline timecode, stepping, playback cadence and video sampling from the opened title's FPS.
- Counts actual standalone canvas presentations so the footer reports live playback FPS beside average frame-render time.
- Renders Auto-quality playback directly at its adaptive surface size, avoiding the former full-resolution render followed by a downscale.

### Development Version 407 world-space Z and media preview

- Sorts every compatible automatic-depth plane assigned to the same camera by evaluated camera/world depth across the complete sibling scene; authored layer order is now only an equal-depth tie-break.
- Reconnects standalone Video layers to the shared asynchronous FFmpeg frame runtime instead of treating them as static images.
- Adds a real standalone audio-preview session for Audio layers and embedded Video audio tracks, synchronized to editor playhead, speed, trim and loop state.
- Adds Preferences > Editor > Preview Audio output-device selection and stages the Qt Multimedia runtime with the Editor.

### Development Version 406 editor Z and render performance

- Keeps unified XYZ, free-transform, camera and depth fields out of the layer-raster fingerprint, so Z-only edits reuse resident text/vector/effect rasters.
- Reuses static standalone frames and layer rasters instead of rebuilding identical pixels for every transport timestamp.
- Evaluates smooth point/spot lighting on a bounded lattice and interpolates it per pixel, avoiding a full material equation at every 1080p pixel.
- Preserves Z ordering across non-rendering Light, Audio and Empty rows.

### Development Version 405 Flux Motion identity and OBS-native dock

- Uses OBS-native icons for common dock actions such as add, delete, move up/down, settings, playback, visibility, and save, with Flux Motion assets retained as fallbacks.
- Derives dock action icon sizes and compact control dimensions from the active OBS/Qt style so they follow UI scaling and theme changes.
- Completes the Flux Motion visual identity and file-format naming (`.fxmt`, `.fxmp`, and the related Flux Motion extensions).
- Shows `v0.8.15-alpha · Development Version 405` in the dock, editor captions, diagnostics, packages, and About artwork.

### Development Version 404 OBS 32.2.1 compatibility and stability

- Rebuilds the native plugin against the OBS Studio 32.2.1 and matching Qt ABI.
- Makes `build-windows.ps1` detect Qt 6.11.1 from the selected OBS SDK and reset stale mixed-ABI CMake caches automatically.
- Removes icons from editor tabs and exposes lower-left and lower-right docking targets.
- Shows `v0.8.14-alpha · Development Version 404` in a persistent header at the top of the main plugin dock.
- Corrects cue/uncue cache-state gating so live output follows the requested title state.
- Removes the title-lifetime mutex cycle that could freeze source activation, editor opening, and OBS shutdown.
- Uses non-blocking model acquisition on the real-time source path so a transient edit cannot stall OBS rendering.
- Avoids the expensive editor-startup blur-shader path and keeps large text blur on the bounded analytic SDF implementation.
- Invalidates only the cached GPU overlay when the canvas pointer moves, keeping ruler cursor indicators real-time without rerendering title artwork.

### Development Version 403 background-aware threshold and waveform polarity

- Makes the layer number and the 2D/3D control use the same composited-row foreground selection as the rest of the layer list.
- Switches to dark theme content only when relative luminance reaches `0.42`, keeping light text/icons on medium-bright custom colors.
- Removes repeated icons from the layer blend-mode dropdown so its entries are text-only.
- Draws audio and linked-video waveforms with the opposite dark/light semantic polarity from the strip label and layer-type icon.

### Development Version 401 background-aware layer rows and timeline strips

- Computes contrast from the actual row surface: OBS `Window` plus the translucent layer color, or the opaque selected-layer color.
- Re-tints visibility, audio, lock, matte and expand controls whenever row selection or the OBS palette changes.
- Uses the layer-color chip itself to choose the dark/light layer-type icon and fallback abbreviation.
- Draws the same layer-type icon in timeline strips and chooses strip label, fade, lock hatch and transition text colors from the final strip background; Development Version 403 gives waveforms the opposite polarity.
- Keeps popup menus and combo dropdowns on their native OBS theme surfaces while the closed controls remain readable over the colored row.

### Development Version 400 layer-type icons and appearance colors

- Uses the supplied Text, Clock, Ticker, Shape, Image, Video, Audio, Empty, Adjustment, Color Solid, Camera and Light SVGs consistently in the Add Layer menu and the layer-list type indicator.
- Converts the artwork to `currentColor` and refreshes action/row pixmaps on OBS palette or style changes, so dark and light themes use the correct icon tint.
- Adds dedicated configurable defaults under Preferences > Appearance for Video, Audio, Empty, Adjustment, Color Solid, Camera and Light rows.
- Keeps Group, Asset and transition-input fallback labels where no replacement artwork was supplied.


### Development Version 399 close Point-light text-shadow projection

- Replaces the single two-triangle Point-shadow sprite with a reusable 32×32 UV grid only for close or very wide Text, Clock and Ticker casters.
- Prevents large text planes from crossing Point cube-face eye planes as canvas-sized triangles, eliminating the sharp diagonal/triangular gaps that appeared when light-to-text Z distance fell below roughly 300 px.
- Keeps ordinary Shape, Image and Video casters, distant text, Spot/Parallel maps and the receiver PCSS filter on their existing fast paths.
- Reuses one GPU-resident grid per render session and destroys it with the session, avoiding per-frame vertex-buffer allocation.
- Advances GPU and persistent visual cache identities so no shadow map generated by the old projection geometry is reused.

### Development Version 398 planar shadows and text continuity

- Uses the same explicit light-space basis, projection centre/span and linear depth normalization in both Spot/Parallel shadow-map generation and receiver sampling.
- Removes the remaining dependency on backend-specific Qt matrix packing and OpenGL-versus-D3D clip conversion for planar shadows.
- Expands conservative Text, Clock and Ticker shadow alpha coverage from five taps to a 4×4 projected footprint with a small half-texel dilation, eliminating thin-stroke gaps under minification.
- Keeps Point-light cube-atlas projection and ordinary Shape, Image and Video caster cost unchanged.
- Advances GPU and persistent visual cache identities so no stale planar or text-shadow output is reused.

### Development Version 397 text-shadow continuity

- Uses projected-footprint max-alpha sampling for Text, Clock and Ticker shadow casters so thin glyph stems and diagonals remain continuous after shadow-map minification.
- Keeps Shape, Image and Video casters on the original single-sample shadow writer, avoiding a general shadow-rendering cost increase.
- Enforces a `0.1 px` minimum Source Size in both property panels, loaded/keyframed light data, runtime evaluation and editor light visualization.
- Invalidates visual/GPU cache identities for the corrected shadow writer while preserving the Development Version 396 effect-cache fast path.

### Development Version 396 effects performance audit

- Moves static effect-output cache validation ahead of per-effect animation resolution, shader-registry lookup and shader queue checks.
- Skips conservative, mathematically neutral built-in effect states before shader acquisition and GPU render-target allocation.
- Leaves custom extension shaders untouched unless their ID resolves to a registered built-in effect, avoiding unsafe assumptions about extension parameter semantics.
- Adds debug performance counters for effect-output cache hits/misses and no-op passes removed.
- Preserves the existing full/fast Motion Blur temporal paths and effect-stack ordering.

### Development Version 395 performance audit

- Reworked the logger around cached preferences, lazy message construction and a persistent buffered file handle instead of repeated `QSettings` reads and file open/close operations on hot paths.
- Disabled verbose timing/presentation/render-diagnostic categories by default while retaining opt-in diagnostics through Preferences.
- Avoided render-session diagnostic snapshots and associated mutex acquisition unless their log category is enabled.
- Stopped treating ordinary `QEvent::LayoutRequest` traffic as a structural dock transition, preserving editor playback pacing while transport labels and controls update.
- Reduced Point-light six-face atlas area by 4× at each authored quality setting; Spot and Parallel lights retain the selected 256–4096 px planar resolution.


### Vector stroke animation

- Added geometry-stage **Trim Paths** with animatable Start, End, Offset and Simultaneously/Individually modes.
- Added general **Stroke Offset**, keyframes, serialization, migration, cache invalidation and matching editor/live output.
- Kept trimming and stroke geometry inside the normal effect, transition and Motion Blur pipelines.

### Motion Blur and temporal rendering

- Rebuilt Motion Blur around complete shutter-time sampling of transforms, text transitions, Trim Paths, effects, shadows, glows, masks and projected output.
- Added coverage-preserving and temporal-occupancy alpha resolution so translucent artwork does not accumulate opacity and opaque moving bodies remain solid.
- Added distance-adaptive quality, separate live-output budgets, transform-only GPU reuse and reduced editor/UI overhead during playback.
- Added high-precision temporal accumulation where supported, Background Persistence compatibility, safe temporal fallbacks and compositor exception containment.
- Fixed the Motion Blur plus active text-transition crash and the subsequent render-session deadlock that could leave output blank, stale or permanently deferred.

### 3D lighting, materials and shadows

- Added native **Ambient, Point, Spot, Parallel and Environment Light layers** with animated color, brightness, position, point of interest, source size, falloff, cone and shadow controls.
- Added opt-in per-layer material response: ambient, diffuse, specular, shininess, metallic, roughness, reflection, emission and lighting acceptance.
- Added alpha-aware per-light Spot/Parallel shadow maps and omnidirectional Point-light shadows through a six-face atlas.
- Added contact-hardening/soft shadow filtering, receiver-plane-aware self-shadow handling, distance-stable depth ranges and real-time shadow updates while dragging.
- Added Preferences > Advanced **Shadowmap Size** choices from 256 to 4096 px, with 2048 px as the default.
- Moved first-use shader compilation off the frame presentation path and added a centered compile-progress screen while retaining the last stable frame.

### 3D editing, cameras and extrusion

- Added Light and Empty layers to the normal hierarchy, timeline and transform-parent workflow.
- Expanded the 3D Camera, Light, Environment, Material and Geometry inspectors with keyframes and responsive XYZ controls.
- Improved local/world/parent rotation, first-drag behavior, projection-independent rotation scrubbing, double-sided text and authored-track visibility.
- Added transactional frame publication and stale-frame recovery for 3D transforms, negative rotation, lights and extrusion.
- Improved Text/Clock/Ticker/Shape extrusion with hardware depth, adaptive shell density, lighting and shadow interaction.
- Isolated embedded 3D asset projection space from the parent title/graphic and retained independent asset playback.

### Assets and document import

- Added a separate selective packed title format for optionally embedding images, video and fonts while preserving the normal external-asset workflow.
- Added layered SVG, GIMP and Photoshop import through one unified **File > Import** command.
- Expanded SVG/PSD support for fills, gradients, strokes, fonts, font sizes, rich text, blend modes and enabled layer effects.
- Added canvas-space horizontal/vertical flip, collapsed imported groups and Photoshop text-scale/content corrections.

### Live cueing and preview

- Added a read-only cue Preview with green Preview and red Program row states, Take/Cancel controls and next/previous cue preview hotkeys.
- Added OBS Studio Mode routing for duplicate-scene Preview workflows while keeping the local preview available as an option.
- Split **Select Row Before Cue** from the optional **Show Preview** behavior.
- Moved the authoritative Preview into the external Live Text Cues window when that window is open and blocked editing in all preview canvases/scenes.
- Improved cue ending-state handoff, cache-column visibility and complete-panel pop-out behavior.

### Editor, layers, audio and workspace

- Added responsive inspector layouts, right-aligned keyframe navigation, top-positioned tabs, consistent labels and toggle-switch controls.
- Added per-layer custom colors, continuous color-backed layer/timeline rows, aligned fixed columns, drag handles and hierarchy-safe structural drops.
- Added persistent numeric Volume/Pan controls, an improved Audio Editor meter and cleaner Audio/Video audio property sections.
- Added responsive Titles and Graphics list rows, dock locking/recovery, hidden tabbed-dock headers and safer layout reset/preferences handling.
- Added improved loop/restart transport behavior, cross-session clipboard support, correlated render diagnostics and source/runtime logging.

## Core features

### Native OBS integration

- Native OBS sources and FXM Stinger transitions.
- Dockable **Titles and Graphics** and **Live Text Cues** workflows.
- Add saved titles to scenes, rebind existing FXM sources and route cue Preview in Studio Mode.
- Automatic OBS Audio Mixer visibility for titles that contain audio.
- OBS theme integration, persistent editor layouts and configurable dock locking.

### Layer-based editor

- Text, Clock, Ticker, Shape, Image, Video, Audio, Asset, Group, Adjustment, Light, Empty and Stinger Scene A/B layers.
- Layer ordering, visibility, locking, parenting, grouping, masks, mattes, per-layer colors and structural drag/drop.
- Free Transform, corner pinning, vector editing, snapping, rulers, guides and safe areas.
- Timeline, keyframes, Graph Editor, motion paths, reusable assets and independent asset playback.

### 3D layers, cameras and lights

- Opt-in planar 3D while legacy 2D projects retain their original rendering path.
- Position, Scale, Anchor, Rotation and Orientation XYZ with local, world and parent axes.
- Perspective/orthographic cameras, animated switching, editor views, navigation and transform gizmos.
- Depth testing, culling, transparent sorting, extrusion, camera-aware Motion Blur and projected effect bounds.
- Ambient, Point, Spot, Parallel and Environment lights with per-layer material response and opt-in shadows.

### Text and typography

- Multiple independent styles and properties inside one text box.
- Direct inline editing with shared selection/caret geometry and title-level Undo/Redo.
- Font, size, H/V Scale, tracking, baseline, OpenType, fill, gradient, stroke, Stroke Offset and Trim Paths controls.
- Paragraph alignment, justification, indents, spacing, vertical alignment, wrapping and auto-size.
- Text Styles, automatic formatting rules, Text Animators, Clock and Ticker layers.

### Effects, media and playout

- Searchable effect browser, favorites, recent items, presets and extension SDK.
- Keying, matte, spill suppression, blur/detail, optical, distortion, damage, finishing and transition effects.
- Video layers with embedded multistream audio, trim, loop, reverse, time remap, interpolation, proxy and decode-cache workflows.
- Full-pipeline Motion Blur, Background Persistence, RAM/disk prerendering, live fallback and synchronized editor monitoring.
- Selective packed title files and expanded layered SVG/GIMP/PSD import.

## Building and documentation

The project uses CMake and vcpkg. See [INSTALL.txt](INSTALL.txt) and [docs/ARCHITECTURE_AND_BUILD.md](docs/ARCHITECTURE_AND_BUILD.md).

Canonical documentation is indexed in [docs/README.md](docs/README.md), including the user guide, editor workflow, text/live-data guide, rendering/cache guide, effects/extensions guide, packed-title format, consolidated changelog and Visual Effects SDK.

FXM is alpha software. Source-level and standalone native contracts are included, but every release should also be built and visually tested in the target Windows OBS/Qt/MSVC environment before production use.
