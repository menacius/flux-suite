# Effects Asset Contract

Each GPU-capable effect lives in its own folder:

```text
data/effect-transitions/shaders/<effect-id>/<effect-id>.effect
```

`TitleEffectRegistry` maps `LayerEffectType` values to these files and compiles
them independently with `gs_effect_create_from_file()`. Add new effect shaders by:

1. Creating a new folder under `data/effect-transitions/shaders`.
2. Adding the `.effect` file with a `Draw` technique.
3. Registering the new path in `src/rendering/title-effect-registry.cpp`.
4. Adding the new `LayerEffectType` mapping where effect usage is collected.

The current per-effect shaders are standardized pass-through contracts. They are
compiled separately now so effect implementations can move from CPU/Cairo to GPU
one effect at a time without changing plugin packaging again.
