# Flux Motion Visual Effects SDK

Development Version 233 introduced the public **Modular Visual Effects SDK**. The SDK is intentionally host-owned and ABI-stable: third-party plugins describe effects, parameters, widgets, render passes and state contracts, while FXM owns editor widgets, scheduling, serialization, render-thread isolation and missing-plugin recovery.

## Discovery

FXM discovers visual effects from predetermined roots, recursively up to a bounded depth:

1. The plugin data `extensions` folder shipped with FXM.
2. The user/config `extensions` folder created by OBS for FXM.
3. The app-data `Effects` folder.
4. Any paths listed in `FXM_EFFECT_PLUGIN_PATH` (`;` on Windows, `:` on Linux/macOS).

Scanning runs off the caller/UI thread and publishes one consistent catalog snapshot. Manifest effects use `*.fxm-effect.json` or `manifest.json`. Native plugins use the platform library extension (`.dll`, `.so`, `.dylib`) and export one of `fxm_plugin_query_v1` through `fxm_plugin_query_v4`.

## ABI and identifiers

The public header is `src/extensions/fxm-plugin-api.h`. ABI v4 is append-only and pure C. Every effect must expose a stable effect id such as `com.example.glow.v1`. IDs are serialized into projects; changing an ID creates a different effect.

A native v4 plugin exposes `fxm_plugin_descriptor_v4`, which contains:

- plugin id, name and version,
- effect descriptors,
- stable schema version,
- parameter metadata,
- custom property widget JSON,
- GPU render pass declarations,
- input declarations,
- color-space and alpha contracts,
- optional state migration/validation callbacks,
- safe unload callbacks.

## Manifest schema

A minimal GPU shader effect manifest:

```json
{
  "apiVersion": 4,
  "id": "com.example.invert.v1",
  "name": "SDK Invert",
  "provider": "com.example",
  "version": "1.0.0",
  "category": "SDK Samples",
  "schemaVersion": 1,
  "backend": "gpu",
  "shader": "invert.effect",
  "inputCount": 1,
  "colorSpace": "preserve-input",
  "alpha": "premultiplied-preserve",
  "parameters": {
    "amount": { "type": "float", "label": "Amount", "min": 0, "max": 1, "step": 0.01, "default": 1, "animatable": true }
  },
  "renderPasses": [
    { "name": "main", "shader": "invert.effect", "technique": "Draw" }
  ],
  "stateSerialization": {
    "format": "json-object",
    "missingPluginPlaceholder": true
  }
}
```

## Parameters and widgets

Parameter metadata is declarative JSON. FXM creates the property widgets, validates values, writes keyframes and serializes state. Custom property widgets are also declarative JSON; plugins must not pass Qt widget pointers across the ABI.

Supported parameter families include `bool`, `int`, `float`, `angle`, `color`, `enum` and `point`. Parameter ids must remain stable once published.

## GPU effects and multi-pass effects

GPU effects declare one or more render passes. A single-pass effect uses `backend: "gpu"`; a multi-pass effect uses `backend: "gpu-multi-pass"` or multiple entries in `renderPasses`. Pass declarations are metadata for the host renderer and cache system, including target names, input aliases, shader files and techniques.

## CPU effects

CPU effects must declare `backend: "cpu-worker-only"` or `cpuWorkerOnly: true` in capabilities. FXM never calls third-party CPU code from the render loop. CPU work must be scheduled through the host worker pipeline and must produce bounded, cacheable outputs for the GPU compositor.

## Inputs and layer references

`inputCount`, `inputs`, `auxiliaryInputs` and `layerReferences` declare whether an effect consumes only the current layer, auxiliary textures, scene/composition inputs, or explicit layer references. Hidden source dependencies are declared up front so FXM can render them in dependency order and avoid accidental recursion.

## Color and alpha contracts

Every effect declares color-space and alpha requirements:

- `preserve-input`, `scene-linear`, `display-referred`, `hdr-linear`,
- `premultiplied-preserve`, `premultiplied-expand`, `premultiplied-replace`, `straight-input-required`.

The host uses these contracts to insert conversion passes and to mark cache-breaking effects accurately.

## Serialization and migration

Effect state is serialized as a JSON object under the stable effect id and schema version. Native plugins can provide `validate_state` and `migrate_state` callbacks. FXM catches third-party exceptions around migration/validation and reports the effect as invalid instead of allowing exceptions to reach the render loop.

When a plugin is missing, FXM keeps a missing-plugin placeholder containing the original effect id, schema version, parameters and keyframes. Reinstalling the plugin restores the effect without data loss.

## Stability and safety

- Plugin scanning runs outside the UI thread.
- A plugin that fails scanning is quarantined and skipped on later scans.
- Quarantine and blacklist lists are stored in the FXM extensions config folder.
- Crash reports include plugin path, name, version, time and failure reason.
- Native libraries are unloaded only through the safe unload path and can veto unload while instances are active.
- No third-party exception is allowed to escape into FXM's render loop or project import path.

## Samples

See `sdk/visual-effects/samples/gpu-invert` for a manifest/shader effect and `sdk/visual-effects/samples/native-v4` for a native ABI v4 descriptor skeleton.
