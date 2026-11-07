"""Validate the embedded assets with only the Python standard library."""
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
data = (ROOT / "main/assets/frog-laugh.gif").read_bytes()
assert data[:6] in (b"GIF87a", b"GIF89a")
width, height, packed = struct.unpack_from("<HHB", data, 6)
assert (width, height) == (280, 392)
pos = 13 + (3 * (1 << ((packed & 7) + 1)) if packed & 128 else 0)
frames = duration = 0
delay = None


def skip_blocks(offset):
    while True:
        size = data[offset]
        offset += 1
        if not size:
            return offset
        assert offset + size <= len(data), "Truncated GIF data"
        offset += size


while True:
    marker = data[pos]
    pos += 1
    if marker == 0x3B:
        break
    if marker == 0x21:
        kind = data[pos]
        pos += 1
        if kind == 0xF9:
            assert data[pos] == 4 and data[pos + 5] == 0
            flags, delay, _ = struct.unpack_from("<BHB", data, pos + 1)
            assert not flags & 1, "Transparent deltas aren't supported by this LVGL configuration"
            assert ((flags >> 2) & 7) in (0, 1), "Require preserved opaque frame rectangles"
            assert delay in (8, 9), "Unexpected frame timing"
            pos += 6
        else:
            assert not (kind == 0xFF and data[pos + 1:pos + 12] == b"NETSCAPE2.0"), "Must not loop"
            pos = skip_blocks(pos)
    elif marker == 0x2C:
        x, y, w, h, flags = struct.unpack_from("<HHHHB", data, pos)
        assert w > 0 and h > 0 and x + w <= width and y + h <= height
        if not frames:
            assert (x, y, w, h) == (0, 0, width, height)
        assert not flags & 64, "Unexpected interlacing"
        pos += 9
        if flags & 128:
            pos += 3 * (1 << ((flags & 7) + 1))
        assert 2 <= data[pos] <= 8
        pos = skip_blocks(pos + 1)
        assert delay is not None
        frames += 1
        duration += delay
        delay = None
    else:
        raise AssertionError(f"Unexpected GIF marker {marker:#x}")

assert pos == len(data), "Unexpected trailing data"
assert frames == 210 and duration == 1750, (frames, duration)
assert len(data) < 5 * 1024 * 1024, "Asset exceeded the app/memory budget"
assert (ROOT / "main/assets/frog-idle.rgb565").stat().st_size == width * height * 2
print(f"Frog assets passed: {frames} frames, {duration / 100:.2f}s, opaque RGB565-compatible, single-shot")
