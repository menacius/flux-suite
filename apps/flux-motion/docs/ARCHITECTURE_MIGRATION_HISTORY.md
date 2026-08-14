# Architecture migration history

This document consolidates the completed Phase 10–17 migration records. The
current architecture and build instructions live in
[ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md); this file preserves the
decisions and validation history without presenting old paths as current.

## Phase 10: rendering boundary

Rendering resources and backend operations were separated behind Shared
interfaces. OBS texture and graphics operations became OBS-adapter
responsibilities, removing direct OBS rendering calls from reusable Core and
Editor code.

## Phase 11: audio boundary

Audio transport, scheduling and preview contracts were separated from the OBS
audio implementation. Reverse transport and source-audio behavior retained
their existing semantics while reusable code stopped depending on OBS APIs.

## Phase 12: Editor module boundary

Editor widgets, controls, assets and title-editing helpers received explicit
ownership. Frame-rate, localization and asset-path services moved behind
host-neutral providers, allowing the Editor code to be classified separately
from OBS integration.

## Phase 13: OBS integration ownership

Plugin lifecycle, sources, transitions, hotkeys, audio adapters, host adapters
and the ordered title-source implementation modules became OBS-owned. Core
video and transport code remained reusable. The obsolete `src/obs` inventory
and its README were removed.

## Phase 14: obsolete dependency removal

Legacy OBS source/header inventories and direct Core dependencies on module,
frontend and logging APIs were removed. Shared host context, logging, preview,
command-transport and rendering contracts became the dependency boundary.

## Phase 15: independent build targets

The standalone `flux-motion-editor` executable and lightweight
`flux-motion-obs-plugin` module were introduced. A combined compatibility
target was retained while standalone preview parity was completed. The Editor
ceased linking OBS directly; the plugin excluded the full editing window.

## Phase 16: dock and Editor ownership

The standalone application became the full Editor window, while the plugin
kept the operational OBS dock. Dock edit actions launch the Editor with title,
configuration, project-scope and data-root context. Atomic shared-file refresh
provides the current process bridge.

## Phase 17: theme ownership

The standalone Editor gained a single Flux Motion application theme. The OBS
dock deliberately continues to inherit the active OBS palette, stylesheet and
font and does not depend on the Editor theme module.

## Current physical layout

- `Flux Motion`: Core, Editor, renderer, data, tests and build orchestration.
- `Flux Motion plugin for OBS`: plugin lifecycle, OBS adapters, source,
  transition, hotkey and title-source implementation modules.
- `Flux Suite Common Files`: Shared runtime contracts, canonical suite version
  and common Satoshi resources.

Compatibility junctions remain under `Flux Motion/OBSPlugin`, `Flux
Motion/Shared` and `Flux Motion/data/fonts` for older tools. They do not create
duplicate source or asset copies.

## Validation summary

Each migration checkpoint passed its targeted source-boundary contracts and a
native Windows build before the next ownership change. Current build and test
evidence is maintained in [VALIDATION_HISTORY.md](VALIDATION_HISTORY.md).

