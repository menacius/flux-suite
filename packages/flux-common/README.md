# Flux Suite Common Files

Canonical assets and source contracts shared by Flux Suite applications live
here. `VERSION.txt` versions this package only; every application and plugin
owns a separate version. `resources/fonts` is the canonical Satoshi
distribution, `resources/icons` owns artwork reused by more than one product,
and `Shared` contains host-neutral runtime interfaces used by Flux Motion and
its OBS integration.

The monorepo uses direct CMake paths and contains no compatibility junctions or
hard-linked source copies. CMake consumers should use `FLUX_SUITE_COMMON_DIR`
and the modules under `cmake`.
