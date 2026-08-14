# Repository Architecture

Flux Suite uses one Git repository with explicit component boundaries.

| Component | Location | Version source |
| --- | --- | --- |
| Flux Motion | `apps/flux-motion` | `apps/flux-motion/VERSION.txt` |
| Flux Encoder | `apps/flux-encoder` | `apps/flux-encoder/VERSION.txt` |
| Flux Suite Installer | `apps/flux-suite-installer` | `apps/flux-suite-installer/VERSION.txt` |
| Flux Motion Plugin for OBS | `plugins/obs/flux-motion` | `plugins/obs/flux-motion/VERSION.txt` |
| Flux Common | `packages/flux-common` | `packages/flux-common/VERSION.txt` |

Applications consume common code through `FLUX_SUITE_COMMON_DIR`. Flux Motion
consumes the OBS host adapter through `OBS_FXM_OBS_PLUGIN_ROOT`; no filesystem
junctions or hard-linked compatibility copies are required.

Generated files are written below `out/` and are never source-controlled.
