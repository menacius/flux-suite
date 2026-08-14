"""Generate the Windows icon from the supplied Flux Encoder SVG geometry."""

from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "resources" / "flux-encoder.ico"
VIEWBOX = 145.44
BACKGROUND = (0x12, 0x10, 0x16, 0xFF)
ACCENT = (0x42, 0xA2, 0x74, 0xFF)
BARS = (
    (37.60, 29.72, 71.72, 20.32),
    (37.60, 62.56, 55.82, 20.32),
    (37.60, 94.18, 20.32, 20.32),
)


def inside_rounded_square(x: float, y: float) -> bool:
    radius = 18.35
    if radius <= x <= VIEWBOX - radius or radius <= y <= VIEWBOX - radius:
        return 0 <= x <= VIEWBOX and 0 <= y <= VIEWBOX
    corner_x = radius if x < radius else VIEWBOX - radius
    corner_y = radius if y < radius else VIEWBOX - radius
    return (x - corner_x) ** 2 + (y - corner_y) ** 2 <= radius**2


def sample_color(x: float, y: float):
    if not inside_rounded_square(x, y):
        return None
    for left, top, width, height in BARS:
        if left <= x <= left + width and top <= y <= top + height:
            return ACCENT
    return BACKGROUND


def rgba(size: int, samples: int = 4) -> bytes:
    pixels = bytearray()
    scale = VIEWBOX / size
    count = samples * samples
    for py in range(size):
        for px in range(size):
            totals = [0, 0, 0, 0]
            opaque = 0
            for sy in range(samples):
                for sx in range(samples):
                    x = (px + (sx + 0.5) / samples) * scale
                    y = (py + (sy + 0.5) / samples) * scale
                    color = sample_color(x, y)
                    if color is None:
                        continue
                    opaque += 1
                    totals[0] += color[0]
                    totals[1] += color[1]
                    totals[2] += color[2]
            if opaque:
                pixels.extend((totals[0] // opaque, totals[1] // opaque,
                               totals[2] // opaque, round(255 * opaque / count)))
            else:
                pixels.extend((0, 0, 0, 0))
    return bytes(pixels)


def png_chunk(name: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + name + data +
            struct.pack(">I", zlib.crc32(name + data) & 0xFFFFFFFF))


def png(size: int) -> bytes:
    pixels = rgba(size)
    stride = size * 4
    scanlines = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride]
                         for y in range(size))
    return (b"\x89PNG\r\n\x1a\n" +
            png_chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            png_chunk(b"IDAT", zlib.compress(scanlines, 9)) +
            png_chunk(b"IEND", b""))


def main() -> None:
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = [png(size) for size in sizes]
    header_size = 6 + 16 * len(images)
    offset = header_size
    entries = []
    for size, image in zip(sizes, images):
        dimension = 0 if size == 256 else size
        entries.append(struct.pack("<BBBBHHII", dimension, dimension, 0, 0,
                                   1, 32, len(image), offset))
        offset += len(image)
    OUTPUT.write_bytes(struct.pack("<HHH", 0, 1, len(images)) +
                       b"".join(entries) + b"".join(images))
    print(f"Generated {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
