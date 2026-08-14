# Flux Motion

This directory owns the standalone Flux Motion application and its Qt
Widgets UI. The compatibility CMake target `flux-motion-editor` produces the
`Flux Motion` executable and depends on Core and Shared but
does not link OBS.

The executable constructs `TitleEditor` directly and contains no `TitleDock`.
It accepts the selected title ID, OBS configuration root, scene-collection
scope, and plugin data root on its command line so the dock can open the same
project in an independent process.

Host operations used by the editor are expressed through Shared interfaces.
The standalone host supplies project/configuration paths and the software
preview session without exposing OBS types to Editor code.

The standalone software preview currently covers basic text, image, and solid
primitive layers. GPU effect, 3D/video, and monitored-audio preview parity
remains backend work.

The completed dock/Editor ownership checkpoint is documented in
`docs/ARCHITECTURE_MIGRATION_HISTORY.md`.
