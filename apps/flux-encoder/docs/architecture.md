# Architecture

## Processes

`Flux Encoder` owns the GUI, SQLite state, profile browser and scheduler. `flux-encoder-worker` owns one FFmpeg process and emits newline-delimited JSON progress messages. The queue process never performs media decoding or encoding in the UI thread.

## Persistence

SQLite uses WAL mode. Every queue mutation is written immediately. At startup, jobs left in active states are changed to `Interrupted`. Each job carries the profile UUID, profile revision and a full resolved profile snapshot.

## Hardware capability model

Capability detection has two levels:

1. **Compiled:** the encoder appears in `ffmpeg -encoders`.
2. **Runtime available:** a one-frame test encode succeeds with the current hardware and driver.

Profiles stay visible when unavailable so projects remain understandable across machines. The UI disables unavailable profiles and displays the probe reason. Worker-side validation provides a second line of defense and can fall back to a software encoder without invoking a shell.

## Flux Motion widget language

The reusable controls live under `src/ui/flux`. They preserve Flux Motion's palette-driven approach, compact dimensions and collapsible inspector behavior. Application-specific persistence uses the `Flux/FluxEncoder` settings namespace.
