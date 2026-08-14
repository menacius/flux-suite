# Effects System

Owns stackable effect contracts in `layer-effects.h`, including Drop Shadow,
Long Shadow, Glow, Inner Glow, Inner Shadow, Color Overlay,
Brightness/Contrast, Saturation, blending modes, effect caches, and effect
serialization adapters.

Development Version 220 adds `effect-runtime.*` as the canonical metadata and
evaluation layer. `EffectDescriptor` owns stable IDs, effect schema versions,
parameter metadata, execution/color/alpha contracts, pass requirements, HDR
support, dirty scope and bounds expansion. `ResolvedLayerEffect` is the
allocation-free render snapshot consumed by both compositor paths.

Development Version 222 upgrades the built-in Noise effect with a versioned
schema-3 procedural engine. New Noise instances use deterministic pixel-space
sampling, extended profile metadata, cache-safe static evaluation and explicit
alpha/clamp/temporal-stability controls. Schema-1 instances retain the legacy
shader branch until explicitly upgraded in the Effects panel.
