# Flux Motion Core

This directory contains the reusable Flux Motion Core model contracts.

Phase 3 contains the existing project/title model and its immutable snapshot
helper. Phase 4 adds the existing layer-based scene graph model. These model
headers do not depend on OBS or Qt Widgets. Phase 5 adds the existing
timeline/keyframe contract, and Phase 6 adds its animation implementation. The
Phase 7 adds the isolated serialization components, and Phase 8 adds the
backend-independent asset runtime. The legacy `title-data.cpp` implementation
remains physically in `src/core` to preserve history, but its config path,
project scope, and logging services are supplied through Shared interfaces and
contain no OBS dependency.

Phase 13 classifies the OBS-free video decode/playback runtime and audio
scheduling/transport helpers as Core playback components. They retain their
existing Qt and FFmpeg dependencies, but contain no Qt Widgets or OBS APIs.
Phase 14 adds project-loader and playback-controller contracts for future
standalone players, encoders, headless renderers, and remote workers.
