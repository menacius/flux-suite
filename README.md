# Flux Suite

Flux Suite is the monorepo for Flux desktop applications, host plugins, shared
runtime code, build tooling, and release infrastructure.

Product page and downloads: [software.omniatv.com/flux-suite](https://software.omniatv.com/flux-suite/)

## Repository layout

- `apps/` contains independently versioned desktop applications.
- `plugins/` contains independently versioned host integrations.
- `packages/flux-common/` contains shared source contracts and brand assets.
- `infrastructure/` contains update-feed schemas and deployment documentation.
- `scripts/` contains repository-level build, packaging, and release entrypoints.

Each application and plugin owns its own `VERSION.txt`. There is intentionally
no repository-wide version.

## Configure

Use a component preset from the repository root:

```powershell
cmake --preset encoder-windows
cmake --build --preset encoder-windows
```

See each component's README for its SDK and runtime requirements.

## License

Original Flux source code is licensed under GPL-3.0-or-later. Third-party
assets and dependencies retain their own licenses; see `THIRD_PARTY_NOTICES.md`
and the license files distributed with those components.
