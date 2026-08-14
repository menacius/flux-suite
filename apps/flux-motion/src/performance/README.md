# Performance & Stability

Owns audit artifacts for profiling, render caches, render-time spikes, memory
usage, redundant code paths, crash risks, and regression-test planning. This
module observes feature modules without owning feature behavior.

Development Version 220 adds debug counters for effect parameter resolution,
shader-cache hits/misses, bounds evaluation, executed passes and pass time,
compatibility resource-pool reuse, and empty-stack fast paths.
