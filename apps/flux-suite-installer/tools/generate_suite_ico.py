"""Generate a multi-resolution Windows ICO matching Flux Suite Icon.svg."""

from pathlib import Path
import struct
import zlib


OUTPUT = Path(__file__).resolve().parents[1] / "resources" / "icons" / "flux-suite.ico"
VIEWBOX = 145.44
BACKGROUND = (0x12, 0x10, 0x16)
ACCENT = (0xFC, 0x51, 0x48)
BARS = ((37.60, 29.72, 71.72, 20.32),
        (37.60, 62.56, 55.82, 20.32),
        (37.60, 94.18, 20.32, 20.32))


def color_at(x: float, y: float):
    radius = 18.35
    if not (radius <= x <= VIEWBOX - radius or radius <= y <= VIEWBOX - radius):
        corner_x = radius if x < radius else VIEWBOX - radius
        corner_y = radius if y < radius else VIEWBOX - radius
        if (x - corner_x) ** 2 + (y - corner_y) ** 2 > radius ** 2:
            return None
    for left, top, width, height in BARS:
        if left <= x <= left + width and top <= y <= top + height:
            return ACCENT
    return BACKGROUND


def make_png(size: int) -> bytes:
    scale = VIEWBOX / size
    pixels = bytearray()
    for py in range(size):
        for px in range(size):
            totals = [0, 0, 0]
            opaque = 0
            for sy in range(4):
                for sx in range(4):
                    sample = color_at((px + (sx + .5) / 4) * scale,
                                      (py + (sy + .5) / 4) * scale)
                    if sample is not None:
                        opaque += 1
                        for channel in range(3):
                            totals[channel] += sample[channel]
            if opaque:
                pixels.extend((*[value // opaque for value in totals], round(255 * opaque / 16)))
            else:
                pixels.extend((0, 0, 0, 0))

    def chunk(name: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + name + data
                + struct.pack(">I", zlib.crc32(name + data) & 0xFFFFFFFF))

    stride = size * 4
    scanlines = b"".join(b"\0" + pixels[row * stride:(row + 1) * stride]
                         for row in range(size))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def main() -> None:
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = [make_png(size) for size in sizes]
    offset = 6 + 16 * len(images)
    entries = []
    for size, image in zip(sizes, images):
        dimension = 0 if size == 256 else size
        entries.append(struct.pack("<BBBBHHII", dimension, dimension, 0, 0,
                                   1, 32, len(image), offset))
        offset += len(image)
    OUTPUT.write_bytes(struct.pack("<HHH", 0, 1, len(images))
                       + b"".join(entries) + b"".join(images))
    print(f"Generated {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
