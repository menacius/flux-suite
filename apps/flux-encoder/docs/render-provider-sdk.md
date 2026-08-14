# Flux Encoder Render Provider SDK v1

Flux Encoder discovers providers from `providers/<provider-id>/provider.json`. Render providers are intentionally out-of-process so the encoder GUI does not load OBS, Flux Motion, Qt, codec, or GPU symbols from a third-party renderer.

The ABI is a small C boundary with sized structs, reserved fields and explicit host/provider versions. The newline-delimited JSON control protocol supports request IDs, progress, cancellation and heartbeats. Video/audio payloads are designed for shared memory or shared GPU texture transports rather than base64 JSON.

A future `org.fluxmotion.renderer` provider will contain a headless Flux Motion project loader and rendering runtime. Flux Motion and OBS may both be closed after a saved `.fxmt` project or portable `.fxmp` package has been queued. Unsaved changes require Flux Motion to create a render snapshot first.

The placeholder manifest is deliberately unusable until the provider executable is installed. The Flux Encoder UI consequently keeps **Add Render Project** disabled rather than pretending the feature works.
