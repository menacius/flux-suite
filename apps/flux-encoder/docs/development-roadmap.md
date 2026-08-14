# Development roadmap

## Development Version 002 — Source inspection and queue editing

- Asynchronous FFprobe metadata service.
- Duration, streams, color metadata and source warnings in the inspector.
- Editable per-row profile and output controls.
- Retry failed, retry with software, duplicate, reorder and priority commands.
- Queue manifest import/export.

## Development Version 003 — Complete profile management

- Rename, delete and drag-reorder user categories.
- Nested category path display.
- Profile import/export bundles with schema migration and conflict handling.
- Derived profiles and inherited values.
- Profile comparison and test-encode dialog.

## Development Version 004 — Resource-aware parallel scheduling

- Multiple isolated workers.
- CPU, NVIDIA, Intel, AMD and VAAPI concurrency pools.
- GPU device selection.
- Per-backend limits and memory-aware scheduling.
- Worker heartbeat and orphan-process cleanup.

## Development Version 005 — Professional processing controls

- Stream mapping, subtitles and channel layouts.
- Loudness normalization.
- SDR/HDR color metadata and tone mapping.
- Hardware scaling/filter paths including CUDA, VAAPI, QSV and Vulkan where supported.
- Watch folders and filename templates.

## Development Version 006 — Render providers

- Stable renderer-provider API.
- Flux Motion composition provider.
- Image-sequence and frame-server inputs.
- Remote render-node protocol.
