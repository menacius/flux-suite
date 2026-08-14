# Render profile schema v1

A render profile is a structured JSON object. Important identity fields are `uuid`, `revision`, `categoryUuid` and `parentProfileUuid`. Encoding fields include container, extension, video/audio encoder, hardware backend, resolution, frame rate, quality/bitrate, pixel format and argument arrays.

Imported profile files must be parsed as JSON and migrated by schema version. Arguments must remain arrays passed directly to `QProcess`; a profile must never be executed through a command shell.
