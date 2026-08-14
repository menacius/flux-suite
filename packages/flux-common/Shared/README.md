# Flux Motion Shared

This directory contains shared interfaces, infrastructure, and the canonical
Flux Motion rendering engine.

`rendering-engine/` is the single source of truth for the GPU and compatibility
compositor used by Flux Motion standalone and Flux Motion Plugin for OBS. The
OBS project owns host adapters only. Legacy `title-source` paths are filesystem
links for older tools, not source copies. Host-neutral shutter sampling and
frame-budget policy lives in `motion-blur-sampling.*` so consumers cannot drift.

Phase 2 contains the dependency-light JSON path, performance counter, and
system memory utilities. These utilities do not depend on OBS, Qt Widgets,
editor UI, or Flux Motion domain models. Phase 9 introduces the rendering
resource and backend interfaces used to isolate future OBS and standalone
rendering implementations. Phase 14 adds the backend-neutral logger and
command-transport contracts. The versioned Flux Encoder client provides the
suite media-export handoff without adding rendering or encoding concerns to
the editor.

The application-split checkpoint adds neutral editor-host, audio-preview,
title-preview, render-session, asset-path, and host-context boundaries. A
future local IPC, named-pipe, Unix-domain, TCP, or WebSocket transport can
implement the command endpoint without changing Editor command dispatch. The
Flux Encoder integration uses a documented local JSON IPC endpoint; see
`docs/FLUX-ENCODER-IPC.md`.
