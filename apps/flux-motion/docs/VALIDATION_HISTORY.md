# Validation and performance audit history

This file consolidates delivery-specific validation snapshots and performance
audit notes. Current commands and acceptance criteria belong in
[ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md). Behavioral runtime
contracts belong in [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md).

## Performance audits

### Development Version 294: Motion Blur

The audit restored distance-adaptive resident-texture sampling and retained a
strict budget for CPU-raster temporal work. Motion Blur remains a final
per-layer temporal operation. The sampled visual pipeline keeps Trim Paths,
Noise, Grain, shadows, glows, color effects and transitions. Temporal samples
form a normalized premultiplied exposure, with alpha resolved independently to
preserve authored coverage.

### Development Version 395: runtime hot paths

The audit identified logger configuration/I/O, unconditional diagnostic
snapshots, Editor layout churn and oversized Point-light shadow atlases as the
dominant avoidable costs. Logger state became cached and buffered, expensive
diagnostics became opt-in, routine `LayoutRequest` events stopped resetting
frame pacing, and Point-light atlas area was reduced by 4× while planar-light
resolution remained authored. The retained evidence includes the Point-light
memory estimate and the measured reduction from a six-face full-resolution
atlas.

### Development Version 396: effect-stack regression

Two regressions were corrected: **Static cache hits occurred too late** and
**Neutral effects still consumed complete GPU passes**. Cache preflight now
occurs before effect resolution, while provable built-in identity states are
removed before shader and render-target acquisition. Source-dependent and
extension effects retain conservative behavior. Motion Blur topology and
effect ordering were not changed.

## Consolidated delivery snapshots

- Dev 395–397: performance, effect-cache, lighting and text-shadow contracts
  passed; Linux configure reached the expected missing-libobs boundary.
- Dev 400–403: layer icons, appearance colors, contrast-aware rows/timeline,
  MSVC clamp correction and waveform polarity contracts passed.
- Dev 406–409: standalone Z/render performance, media preview, authored frame
  rate, unified GPU rendering and two-resolution smoke coverage passed in the
  native Windows environment.
- Dev 405 build verification: branding, file associations, OBS-native dock,
  Editor audio meter and package naming passed. Its binary hash was specific
  to that old build and is intentionally not retained as current evidence.

Historical aggregate runners were already red during several of these
deliveries because they included contracts pinned to Development Version 243,
obsolete exact source tokens, unavailable `g++`, or incomplete runtime DLL
paths. These are test-maintenance records, not current production requirements.

## Current audit: v0.8.19-alpha, Development Version 419

- Flux Motion OBS plugin: Release compile passed.
- Flux Motion Editor and renderer: Release compile passed.
- Native Chart Layer runtime/data and playback-pipeline performance contracts
  passed, including two-resolution target propagation and shared renderer checks.
- Distribution was staged under Flux Suite `Dist`; installation was not run.

The local OBS dependency bundle still produces non-fatal Qt WebSockets
`LNK4217` warnings. The complete historical Flux Motion suite remains
non-gating until its version-pinned contracts and runtime test environment are
modernized.
