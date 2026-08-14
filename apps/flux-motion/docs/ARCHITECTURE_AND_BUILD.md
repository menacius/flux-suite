# Architecture and build

## Source ownership

Flux Motion keeps public source paths stable while large implementation units are divided into ordered ownership-oriented `.inc` modules. The facade translation units preserve private symbol ordering and anonymous namespaces while making navigation manageable.

Major facades include:

| Facade | Ownership |
| --- | --- |
| `src/obs/title-source.cpp` | OBS source lifecycle, playback, cache presentation, masks, groups, effects, and registration. |
| `src/editor/title-editor.cpp` | Window/session lifecycle, commands, panels, document editing, playback, templates, and signals. |
| `src/canvas/canvas-preview.cpp` | Geometry, selection, snapping, tools, input, overlays, and preview rendering. |
| `src/editor/properties-panel.cpp` | Inspector construction, text/shape/image properties, gradients, and synchronization. |
| `src/editor/title-dock.cpp` | Title management, import/export, cue rows, playlists, cache status, and dock actions. |
| `src/cache/cache-manager.cpp` | Cache identity, storage, invalidation, queueing, workers, and publication. |

The machine-readable module inventory lives at `tools/modular-source-map.json` and is validated by `tools/audit_modularity_performance.py`.

## Dependency direction

```text
Editor / Dock / Canvas / Timeline
                │
                ▼
       Layer and title models
          │             │
          ▼             ▼
 Text / Effects     Cache contracts
          \             /
           ▼           ▼
        Rendering and OBS integration
```

Core serialization must not depend on Qt widgets. OBS source code must not depend on editor widgets. Shared algorithms belong in core, text, effects, layers, rendering, or cache modules rather than being copied between editor and source paths.

## Configure and build

The project requires a matching OBS/libobs development package and Qt toolchain. CMake configuration fails intentionally when `libobsConfig.cmake` cannot be found.

### Windows

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The script discovers configured SDK paths, builds the plugin, stages runtime dependencies, copies locale/icon/effect data, and creates a versioned ZIP.

Native plugins must be built against the ABI used by the target OBS release. The `v0.8.15-alpha` Windows validation target is OBS Studio 32.2.1 with its matching Qt runtime; reusing a binary built against an older OBS/Qt SDK is unsupported even when the plugin loader can discover it.

`build-windows.ps1` resolves `Qt6Config.cmake` from the selected OBS SDK and passes that directory explicitly to CMake. When an existing build cache points to a different OBS SDK or Qt directory, the helper removes only that generated build directory and performs one full rebuild. This prevents old Qt headers, generated MOC files, WebSockets builds, and libraries from being combined with the current Qt 6.11.1 ABI.

### Linux/WSL

```powershell
powershell -ExecutionPolicy Bypass -File .\build-ubuntu-wsl.ps1
```

Build against the same ABI, glibc baseline, Qt version, and packaging environment used by the target OBS distribution. Avoid producing a binary against a newer libc than the target OBS runtime.

### Incremental update/build/package

```powershell
powershell -ExecutionPolicy Bypass -File .\update-and-build.ps1
```

Configuration values can be stored in the adjacent JSON config files instead of hardcoding local paths in scripts.

## Data and packaging

CMake stages locale files, all SVG icons, and effect/transition data into the OBS plugin data directory. Windows dependency DLLs are copied beside the plugin binary. Build trees, staged packages, archives, IDE state, caches, and generated artifacts are excluded by `.gitignore`.

## Tests and audits

`tests/test-suite-manifest.json` is the versioned coverage contract. `tools/run_automated_test_suite.py` validates that every required subsystem has executable coverage before it runs anything.

- **source:** all Python source contracts; no OBS, Qt or compiler required.
- **smoke:** source plus a focused native CTest subset.
- **full:** source plus the complete configured CTest project.
- **stress:** native lifetime/performance tests for repeated open/close, shutdown, worker cancellation, cache release and snapshot-copy regressions.

```bash
python tools/run_automated_test_suite.py --validate-only
python tools/run_automated_test_suite.py --profile source
python tools/run_automated_test_suite.py --profile smoke --build-dir build
python tools/run_automated_test_suite.py --profile full --build-dir build --jobs 8
python tools/run_automated_test_suite.py --profile stress --build-dir build --json-report build/stress-report.json
```

`--fail-fast` stops after the first source failure and `--timeout` overrides the bounded per-test timeout. Native profiles require `OBS_FXM_BUILD_TESTS=ON`. When a canonical document path changes, update its contract test rather than retaining a duplicate historical note.

## Contribution rules

- Add code to the owning module rather than the facade.
- Keep UI-only state out of title serialization.
- Keep project data out of global `QSettings`.
- Preserve editor/source rendering parity.
- Add a focused regression contract for compile fixes and model-order changes.
- Update `docs/CHANGELOG.md` and the relevant thematic document rather than adding a new one-off markdown file.
## External-data runtime ownership

`src/core/external-data-types.h` contains the provider-neutral serialized schema and layer binding model. `src/core/external-data.cpp` owns mutable runtime source state and is the only component that accepts provider updates. `src/core/external-data-provider.cpp` owns provider lifecycles, parsing, file access, polling timers, HTTP requests, and WebSocket reconnects on one dedicated Qt worker thread. Provider code submits typed values to the manager; it never mutates `Layer` authored fields or touches GPU objects.

`Editor/external-data-settings-dialog.cpp` edits provider definitions and pending layer bindings. Runtime Connect/Refresh actions can preview a configuration, while Save is the only path that commits bindings to the title; Cancel resynchronizes the provider service from the authoritative title store.

A changed value publishes a runtime-only revision, editor callback, and coalesced render-queue entry. OBS consumes pending entries from its source tick/render path and resolves effective values from a mutex-protected snapshot. Equal values are acknowledged by timestamp only and intentionally produce no callbacks or cache invalidation.

## Serialization and migration

The title store remains a top-level JSON array. Schema 6 and the development migration ledger are contiguous from Development Version 144 through 220, including deliberate no-op steps. Migrations and validation are idempotent. Malformed nested cameras, keyframes, effects, transitions, audio effects, bindings, proxy data and provider entries are isolated rather than rejecting the complete title.

Unknown/newer fields survive real load-edit-save round trips at title, layer, camera, animated-property, keyframe, effect, transition, audio-effect, proxy and external-provider levels. Opaque future-schema payloads use immutable shared storage with copy-on-write replacement; render fingerprints explicitly disable passthrough parsing and merging so preservation does not add per-frame copies.

Duplicate/missing layer IDs, dangling/self links and hierarchy cycles are repaired deterministically. Invalid parent-bind matrices are disabled without modifying transform keys. Authored files use atomic `QSaveFile` persistence. Runtime provider values, decoder state, worker queues, editor camera overrides and cache residency are never serialized as authored content.

## Regression coverage

The automated profiles cover editor GUI ownership, Timeline/Graph Editor parity, serialization round trips, 2D/3D rendering, masks/effects, cache/proxy/threading, audio transport, cue persistence, external data, shutdown lifetime and platform builds. The Development Version 219 hot-path test copies snapshots containing multi-megabyte unknown-field payloads and verifies shared immutable storage.
The Development Version 220 effects-runtime contract additionally verifies canonical descriptors, allocation-free resolved state, shader caching, shared bounds policy, reusable compositor resources, effect diagnostics and the no-op schema-6 migration ledger entry.

Manual host validation remains required for GPU output, OBS mixer timing, proxy playback and lifecycle behavior. Repeat the matrix with cache disabled, RAM cache enabled and RAM+disk enabled:

| Area | Required result |
| --- | --- |
| Legacy 2D | Approved reference frames remain pixel-stable. |
| 3D save/load | Every layer/camera channel, switch, assignment, projection and depth option survives reopen. |
| Undo/copy/paste | IDs stay unique and authored relationships/appearance round-trip. |
| Proxy/prerender | No stale frame wins; missing/corrupt cache falls back safely. |
| Persistence/cues | Cue, uncue, transitions and Ignore Persistence match uncached playback. |
| Audio | Forward/reverse/ping-pong/seek remain synchronized without duplicate mixer devices or tails. |
| Lifecycle | Repeated open/close and shutdown during active jobs produce no crash, deadlock, leak or surviving worker. |
| Platforms | Supported Windows and Linux builds compile, package and launch with the intended OBS/Qt ABI. |

## Manual release checklist vocabulary

The maintained host checklist explicitly covers Undo/redo, Copy/paste, Group/ungroup, Save/reopen, External JSON, Audio layer pause/resume/seek, Stinger scene transition, Corrupt proxy cache recovery, Dock layout restoration, Windows and Linux parity, and OBS startup/shutdown.

The Development Version 222 procedural-noise contract verifies versioned schema reset behavior, deterministic shader inputs, schema-3 controls, cache-key coverage and editor exposure.
