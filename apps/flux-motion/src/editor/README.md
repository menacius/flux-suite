# Transitional OBS-Coupled Editor Integration

The backend-neutral Editor UI has moved to the top-level `Editor` module.

The files retained here combine editor widgets with OBS source, frontend,
preview, hotkey, or runtime integration. They are explicitly classified as
Phase 13 input in CMake and must not be added to the clean Editor module until
those OBS responsibilities have been separated.

`style-presets` remains here because its library is also consumed by the
current OBS renderer; splitting its reusable model from its Qt panel belongs
to the same architecture-preserving Phase 13 work.
