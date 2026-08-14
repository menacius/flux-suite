# Flux Motion documentation

These are the canonical documents for `v0.8.19-alpha` Development Version 419. Current behavior belongs in the thematic guides; release history belongs in the consolidated changelog.

| Document | Purpose |
| --- | --- |
| [USER_GUIDE.md](USER_GUIDE.md) | Installation, title creation, editing, cueing, audio/video, Stingers, caching and everyday use. |
| [EDITOR_WORKFLOW.md](EDITOR_WORKFLOW.md) | Canvas, layers, hierarchy, cameras, 3D gizmos, timeline, Graph Editor, motion paths and assets. |
| [MULTI-TITLE-PROJECTS.md](MULTI-TITLE-PROJECTS.md) | Versioned `.fxmproj` containers, Project panel workflow, nested title/graphic layers, loading stages and OBS selection. |
| [TEXT_AND_LIVE_DATA.md](TEXT_AND_LIVE_DATA.md) | Canonical rich text, Text Properties/styles, H/V Scale, stroke, Undo/Redo, Text Animators, cues and external data. |
| [EFFECTS_AND_EXTENSIONS.md](EFFECTS_AND_EXTENSIONS.md) | Effect stacks, execution spaces, presets, transitions, manifests and native extensions. |
| [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md) | GPU/compatibility rendering, text performance, adaptive preview, motion blur, audio runtime and RAM/disk cache. |
| [ARCHITECTURE_AND_BUILD.md](ARCHITECTURE_AND_BUILD.md) | Source ownership, serialization/migration, build, packaging, automated profiles and manual OBS regression matrix. |
| [ARCHITECTURE_MIGRATION_HISTORY.md](ARCHITECTURE_MIGRATION_HISTORY.md) | Consolidated Phase 10–17 ownership and dependency migration record. |
| [VALIDATION_HISTORY.md](VALIDATION_HISTORY.md) | Consolidated historical validation and performance-audit evidence. |
| [PACKED-TITLE-FORMAT.md](PACKED-TITLE-FORMAT.md) | Separate `.fxmp` container, manifest, compression blocks and import-safety contract. |
| [CHANGELOG.md](CHANGELOG.md) | Consolidated development history and release notes. |
| [visual-effects-sdk.md](visual-effects-sdk.md) | Public modular visual-effects SDK and sample integration. |

Current runtime behavior is maintained in [RENDERING_AND_CACHE.md](RENDERING_AND_CACHE.md); delivery-specific evidence is consolidated in [VALIDATION_HISTORY.md](VALIDATION_HISTORY.md).

## Maintenance rule

Update `README.md`, the relevant canonical guide, and `CHANGELOG.md` for each delivery. Do not add a new one-feature document for routine development versions. Keep historical validation evidence separate from the current behavioral contract; machine-readable inventories belong under `tools/`, and executable contracts belong under `tests/`.
