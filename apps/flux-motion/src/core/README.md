# Core Architecture

Owns shared title/project data, serialization, metadata, command and undo/redo
contracts, and global state transitions. Code in this module must remain free of
Qt widget, OBS source, rendering backend, and editor-tool dependencies.
