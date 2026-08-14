# Flux Encoder IPC contract

Flux Motion delegates every media-rendering and encoding decision to Flux
Encoder. It does not expose codec, format, destination, queue, preset or render
settings of its own.

## Discovery and lifecycle

Protocol version 1 uses the Qt local-server endpoint
`com.fluxsuite.encoder.ipc.v1`. Flux Motion probes that endpoint first so an
existing Flux Encoder process is always reused. If the endpoint is absent, the
installed Encoder is launched with:

```text
Flux Encoder --single-instance --ipc-endpoint com.fluxsuite.encoder.ipc.v1
```

During development, Editor Preferences exposes an optional
`FLUX_ENCODER_EXECUTABLE` path override. It is checked first and stored under
Flux Motion's Editor settings; clearing it restores automatic discovery. The
environment variable with the same name remains available for automation.

The Encoder installer registers `installation/executable` (or
`installation/path`) under the native `FluxSuite` / `Flux Encoder` application
settings. Sibling-suite, platform install, and `PATH` locations are supported
as fallbacks.

The Encoder server must restrict its local endpoint to the current OS user.

## Framing and envelope

Each request and response is one compact UTF-8 JSON object followed by `\n`.
The server responds once per request and echoes `request_id`. Both sides reject
an incompatible major protocol version.

```json
{
  "protocol": "com.fluxsuite.encoder.ipc",
  "protocol_version": 1,
  "request_id": "uuid",
  "command": "export.open",
  "source": {
    "application": "flux-motion",
    "project_id": "current-title-id",
    "project_name": "Current title",
    "project_store_path": "/absolute/project/store",
    "project_scope": "scene collection or standalone scope"
  },
  "options": {
    "activate_window": true
  }
}
```

A successful response is:

```json
{
  "protocol": "com.fluxsuite.encoder.ipc",
  "protocol_version": 1,
  "request_id": "uuid",
  "ok": true
}
```

Failures set `ok` to `false` and include either an `error` string or an error
object containing stable `code` and human-readable `message` fields. Flux
Motion displays that message to the user.

New commands such as `queue.add`, `batch.add`, or scripted/background render
operations can reuse this envelope. Additive fields remain optional within
protocol version 1; incompatible framing or semantics require a new endpoint
and major version.
