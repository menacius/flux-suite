# Packed title format (`.fxmp`)

`.fxmp` is a binary, versioned container. It is intentionally separate from
the JSON-based `.fxmt` template format.

## Container version 1

All integers in the fixed 64-byte header are little-endian.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic: `FXMPACK1` |
| 8 | 4 | Container version (`1`) |
| 12 | 4 | Header size (`64`) |
| 16 | 8 | Manifest offset |
| 24 | 8 | Manifest size |
| 32 | 32 | SHA-256 of the UTF-8 JSON manifest |

Entry data follows the header. The manifest is stored at the end so resource
files can be compressed and written as streams without buffering a complete
video or the complete output archive in memory.

Each entry consists of independent blocks of at most 1 MiB. A block has an
8-byte header (`raw_size`, `stored_size`) followed by its payload. The high bit
of `stored_size` marks a raw block; other blocks use LZ4. Data that would grow
under compression is stored raw.

## Manifest

The manifest identifies the format as
`flux-motion-packed-title`, declares its version and package ID,
records the selected packing categories, and lists every entry. Entry offsets
and sizes are decimal strings to preserve the complete 64-bit range. Every
entry has its own SHA-256 digest.

`title.json` contains the ordinary title-template document. Packed resource
references use `packed://assets/...` URIs. Resource entries use these kinds:

- `image` for image layers, transition mattes and environment maps;
- `media` for video and audio source files;
- `font` for application-local font files.

## Import guarantees

Import validates the header, manifest hash, entry count, 64-bit bounds, total
expanded size, block sizes, archive paths and each entry digest. Absolute,
drive-qualified, backslash and traversal paths are rejected. Resources are
written atomically beneath a package-ID plus manifest-hash directory, and only known title path
fields are allowed to resolve a `packed://` URI. Packed fonts are registered
with Qt before missing-resource diagnostics and text rendering.

The exporter exposes independent `Images`, `Video and audio`, and `Fonts`
choices. A disabled category remains an external reference and is never copied
into the container.
