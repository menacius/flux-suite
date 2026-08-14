# Multi-title projects

Flux Motion project files use the `.fxmproj` extension and a versioned
`flux-motion-project` JSON container. A project owns an ordered `titles` array
and project metadata including a stable project ID, name, timestamps, and the
active title ID. Every title/graphic keeps its own stable ID, metadata, canvas,
timeline, layers, animation, settings, and preview thumbnail.

The composition payload inside `titles` is the same canonical Title schema used
by the editor, renderer, scene-collection store, templates, and Flux Motion
Plugin for OBS. There is no parallel or reduced project representation.
Unknown title fields continue to use the existing serialization passthrough and
title schema migration system.

Legacy compatibility is intentionally broad. The project reader accepts:

- `.fxmproj` multi-title containers;
- `.fxmt` title/template objects;
- `.fxmp` packed titles;
- bare title JSON objects; and
- historical top-level title arrays or object roots containing `titles`.

Saving a legacy single-title file from Flux Motion uses `.fxmproj`, leaving the
source file untouched. Individual project items can still be exported as
`.fxmt` templates.

## Project panel workflow

The standalone Project panel is the navigation source for the open project. It
supports create, duplicate, rename, delete, import, export, search, extended
selection, context actions, double-click open, and drag reordering. Switching
items rebinds the existing canvas, Layer List, Properties, Timeline, preview,
audio, and cache systems to the selected composition.

Selecting **Insert as Title/Graphic Layer** creates the existing Asset Layer
precomposition boundary. `asset_title_id` is the stable reusable reference;
embedded descendants remain a compatibility/fail-safe snapshot for projects
created by earlier releases and for offline/deleted library assets. The Asset
Layer retains its own transform, opacity, blend mode, effects, parenting,
visibility, timing mode, loop controls, and isolated 3D projection space.
Direct and transitive self-reference cycles are rejected.

## OBS workflow

Flux Motion Plugin for OBS exposes a Project File property followed by the
project's Title selector. The selected title ID is stored in OBS source
settings. Renaming a title preserves the binding because IDs are stable. A
deleted/missing selection falls back to the first non-library project title,
and the project file timestamp is checked during playback so saved changes are
reloaded without recreating the source. Empty Project File preserves the
scene-collection title workflow used by older sessions.

## Loading stages

Flux Motion reports Reading project, Loading assets, Initializing timelines,
Building caches, and Preparing previews. The modeless window is shown only for
project work and cancellation is honored between safe parse/migration
boundaries.
