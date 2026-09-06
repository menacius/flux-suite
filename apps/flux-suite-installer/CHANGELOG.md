# 2026 - v0.8.19-1-alpha

- Added cross-scope installation discovery and cleanup, including saved custom
  roots and both OBS plugin locations, before activating a verified package.
- Added removal of the retired Broadcast Graphics Live plugin and retained its
  source identifier through the current Flux Motion OBS plugin.
- Made the Suite bootstrapper remove prior per-user and system-wide Suite
  registrations before upgrades, downgrades, or installation-scope changes.

- Kept each product's latest changelog link visible after installation and
  updates complete.
- Published the v0.8.19 Motion editor and OBS plugin packages while retaining
  the unchanged v0.8.18 Encoder package in the product catalog.
- Added product release notes for native Chart layers, consistent external and
  Static Data controls, per-value colors, shared gradients and deterministic
  chart caching.
- Added release notes for adaptive playback performance, the in-window Welcome
  popup, first-install precaching, standalone title setup, cache/text/canvas
  reliability and the simplified Playback and Cache dock.
- Synchronized the installer, setup, signed-feed and website release metadata
  with the current v0.8.19 alpha version.

# 2026 - v0.8.18-alpha

- Added maintainable, application-specific changelog records to the product
  manifest and readable per-application Changelog dialogs for available updates.
- Changes Install All to Update All whenever installed applications have
  updates and excludes already-current applications from that operation.
- Added explicit Ready, Queued, Currently updating, Successfully updated and
  Failed to update states across sequential multi-application updates.
- Synchronized installer, setup, resource and package metadata for v0.8.18-alpha.
