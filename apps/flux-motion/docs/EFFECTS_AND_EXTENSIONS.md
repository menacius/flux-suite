# Effects and extensions

## Panel-based effect stack

Effects Settings is a direct representation of the layer effect stack. Each effect is a collapsible panel. Panel order is render order. Dragging a header reorders the underlying model; the header switch enables/disables the effect; the overflow menu provides Duplicate, Delete, Move Up, and Move Down.

The bottom toolbar contains **Add Effect** and **Respect Masks**. Effect settings are bound to their own effect instance so several expanded panels can remain visible without controls writing into the wrong stack entry.

The layer list keeps the **FX** badge visible whenever a stack exists. An enabled stack uses the active badge state; disabling the whole stack draws a diagonal strike-through over the badge so the user can distinguish “effects present but bypassed” from “no effects”. External-data binding dots remain independent from this visual state.

## Unified effect runtime

Development Version 220 establishes `EffectDescriptor` as the canonical built-in effect contract. The editor catalog, shader registry, cache policy, compatibility compositor and planar-3D renderer consume the same stable ID, legacy adapter, schema version, parameter metadata, execution space, backend, color-space contract, premultiplied-alpha contract, minimum pass count, HDR support, bounds behavior and static-cache eligibility. Built-in metadata is also exposed through the extension catalog so future external effects can follow the same vocabulary.

Render-time parameter evaluation produces an allocation-free `ResolvedLayerEffect` snapshot. It stores only evaluated scalar/color state and a non-owning pointer to optional extension JSON; it never copies effect strings, keyframe vectors or opaque serialization payloads. Static descriptors remain independent from title schema 6, allowing a future effect-specific migration without rewriting unrelated title data.

Dirty propagation has three scopes: effect-output-only, layer-raster and whole-composition. Screen-space/affect-layers-behind effects require composition invalidation; geometry-generating Background Color and Outline require a layer-raster refresh; ordinary pixel effects invalidate only their effected output. The same runtime computes asymmetric left/top/right/bottom expansion for shadow and other padded effects, replacing parallel compositor-specific padding rules.

## Built-in effects and presets

Built-in effects use stable namespaced IDs (`fxm.builtin.*`) while retaining numeric serialization adapters for older project files. Presets and Add Effect menus are generated from the shared catalog. Built-ins include color/generate effects, blur and detail families, shadows, glow, outline, grading, procedural noise, vignette, Lens Flare, Real Glare, Halation, lens distortion, chromatic aberration, warps, finishing effects, motion blur and generated gradients.

Effect parameters may be static or animated. Position-like parameters can expose canvas handles, and keyframe diamonds use the same animation model as ordinary layer properties.

## 3D execution spaces

The host classifies every enabled effect into three stable effect execution spaces. This is derived from the existing effect type and flags, so projects require no migration:

- **Layer space:** standard artwork effects operate on the padded transform-neutral raster before 2D/3D projection. Expanding effects such as shadow, glow, blur, and outline therefore keep their complete bounds after perspective rotation.
- **Post-transform/final layer space:** Motion Blur is resolved after ordinary layer and screen-space layer effects. Transform-only motion reuses one fully effected local texture while sampling projected parent/camera and transition state; animated procedural effects (including Noise/Grain), animated Trim Paths/source geometry, active text transitions, transition blur and screen-space effects use a bounded complete-pipeline sampler. Samples accumulate into one normalized premultiplied exposure. Final color is blended between the current frame and the temporal exposure, while alpha is resolved independently to the strongest authored coverage; internal effect detail is blurred without making translucent pixels more opaque.
- **Screen space:** effects marked **Affect layers behind** read the already composited destination.

Track mattes are projected coverage rather than layer-space effects. Compatible hardware-depth planes sample their full-canvas matte during the same depth-tested draw; effects configured to respect the matte after masking remain on the full-frame compositor to preserve stack order. Groups resolve child depth in an offscreen surface before the group-level stack is applied once.

## Transition presets

Layer transitions are stored separately from effects but use the same preset/data packaging area. Text and general transitions can expose host-owned controls and previews without embedding third-party Qt widgets.

## Manifest extensions

Portable GPU extensions live under `flux-motion/extensions/` and contain a `*.fxm-effect.json` manifest plus an OBS `.effect` shader. The manifest declares:

- globally unique effect ID;
- display/category/provider metadata;
- parameter schema and defaults;
- supported shader techniques;
- editor schema, presets, and assets where applicable;
- capability flags for keyframes and compound graphs.

Supported parameter concepts include float, integer, boolean, color, enum, texture, point, and string. Animatable parameters are evaluated by the host so timeline behavior remains consistent across built-in and external effects.

## Native extension ABI

Optional native extensions are loaded only from the user configuration extension directory. They expose the stable C ABI declared in `src/extensions/fxm-plugin-api.h` and must not depend on private Qt/C++ editor headers.

Compatibility rules:

- IDs are permanent and namespaced.
- API/ABI mismatches are rejected.
- Duplicate IDs are ignored and reported.
- Unknown extension IDs and raw parameter JSON round-trip without data loss.
- Missing extensions do not destroy project settings.
- Native extensions execute inside the OBS process and should only be installed from trusted sources.

## Compound graphs

API v2 can flatten ordered graph elements to bounded shader uniforms (`elementCount`, `element0_*` through `element15_*`). Animation paths use the same element/property names, allowing compound effect elements to be keyframed without extension-specific timeline code.


## 3D depth and material interaction

Depth Test and Write Depth are independent for compatible planar layers:

- **Test enabled, Write enabled:** compare against existing depth and publish passing depth.
- **Test enabled, Write disabled:** compare against persistent depth without contaminating later layers.
- **Test disabled, Write enabled:** draw in authored order and publish depth with the always-pass state.
- **Test disabled, Write disabled:** remain an authored-order compositing layer.

Backface classification uses final projected winding so negative scale, mirrored parents and perspective remain predictable. Transparent compatible planes are ordered far-to-near with authored order as the coplanar tie-break. Masks, destination-aware blend modes and groups retain their required compatibility/offscreen boundaries.

## Procedural Noise Engine

Development Version 222 replaces the compatibility split with a single Noise schema 3. Fine Grain, Film Grain, Digital Sensor, Clouds/fBM, Turbulence, Ridged, Cellular and Blue-noise Dither use deliberately different spatial models and scales. Stored Noise instances from an older schema are reset to the current defaults when loaded; no legacy engine or legacy parameter mode is exposed. Static Noise remains cacheable, while animated or keyframed Noise is time-variant.

## Detail, optical and auxiliary-pass effects

Sharpen, Unsharp Mask, High Pass, Clarity / Local Contrast and Bilateral Sharpen share the GPU detail core. The host first creates a Gaussian low-pass texture and binds it as `blurredImage`; the selected detail technique then compares that texture with the original source. Controls include radius, amount, threshold, luminance-only processing and alpha protection, with effect-specific highlight/shadow, midtone and edge-preservation controls.

Real Glare and Halation no longer share Lens Flare rendering. Glare extracts source highlights and builds directional streaks with dispersion, while Halation composites a thresholded warm diffusion field. Their auxiliary passes are generated before the final optical technique and both paths fail open if an optional shader technique cannot execute.

## Lens, distortion and finishing effects

Development Version 224 adds dedicated GPU techniques and editor controls for:

- Lens Distortion and Chromatic Aberration;
- Directional Blur, Zoom Blur and Radial Blur;
- Ripple and Wave Warp;
- Pixelate and Edge Detect;
- Posterize, Threshold and Scanlines.

These effects use the same canonical descriptors, cache fingerprints, serialization validation, padded-bounds calculation and embedded-shader fallback mechanism as the rest of the built-in stack.

Built-in effect schemas are intentionally forward-only. When a built-in effect implementation changes schema, existing instances reopen as fresh instances using the new defaults. Legacy controls and compatibility render branches are not retained.

## Development Version 225 — Keying and matte effects

The built-in keying family contains Chroma Key, Luma Key, Color Range, Spill Suppression and Matte Choker. All five use current-schema defaults only, are routable through the standard GPU effect compositor, and remain keyframeable and cache-aware.

## Development Version 226 — Source effects, presets and shared animation hierarchy

Light Wrap can sample either the already composited background or a selected layer. Its GPU path uses alpha-aware foreground-edge extraction, configurable radius, intensity and edge width, spill tinting and foreground-luminance protection. Displacement Map samples a selected layer even when that layer is hidden from the final composite, supports independent luminance/R/G/B/alpha channels, signed horizontal and vertical amounts, four wrap modes and source-space or composition-space mapping. Cyclic source dependencies fail open rather than recursing indefinitely.

The Effects panel now provides a searchable categorized browser, Favorites, Recently Used, generated thumbnails and GPU/CPU/HDR/external-plugin/screen-space/cache-breaking badges. Individual effects and complete stacks can be copied and pasted; complete stacks can also be saved, imported and exported as `.fxmstack` presets. Replace, reset parameter, reset effect, duplicate and stack-wide enable operations are available from the effect and stack menus.

The effect stack is the hierarchy authority for both the Layer List and Timeline. Each effect has a parent row and its keyframeable parameters appear directly below it in the same order. Numeric controls use ordinary animation tracks and Graph Editor channels, color controls remain unified while exposing component channels, angle values support continuous rotation and evolution values are not wrapped.
