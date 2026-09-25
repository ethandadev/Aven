#!/usr/bin/env python3
"""Draws the Aven app icon at every size the platforms need.

    python3 tools/icon/make_icon.py

Writes:
  engine/data/icon/aven_{16,32,48,64,128,256}.png   (embedded; used as the window icon)
  resources/icon/aven_{512,1024}.png, aven.ico, aven.icns, aven.svg

Standard library only. Shapes are signed distance fields, so edges are anti-aliased
at any size.
"""

import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

TOP_LEFT = (59, 130, 246)      # blue
BOTTOM_RIGHT = (124, 58, 237)  # violet
SPARKLE = (253, 224, 71)


def clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def coverage(d, px):
    """Signed distance (negative inside) to pixel coverage."""
    return clamp(0.5 - d / px)


def sd_round_box(x, y, half, r):
    qx, qy = abs(x) - half + r, abs(y) - half + r
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - r


def sd_diamond(x, y, r):
    return (abs(x) + abs(y) - r) / math.sqrt(2)


def sd_sparkle(x, y, r):
    # A four-pointed star: the union of two thin diamonds.
    a = (abs(x) / r + abs(y) / (r * 0.28)) - 1
    b = (abs(x) / (r * 0.28) + abs(y) / r) - 1
    return min(a, b) * r * 0.25


def blend(dst, src, a):
    return tuple(d + (s - d) * a for d, s in zip(dst, src))


def render(size):
    px = 1.0 / size  # one pixel in normalized units
    rows = []
    for j in range(size):
        row = []
        for i in range(size):
            # Normalized coordinates in [-0.5, 0.5], y up.
            x = (i + 0.5) / size - 0.5
            y = 0.5 - (j + 0.5) / size
            # Background: rounded square with a diagonal gradient and a soft top light.
            box = sd_round_box(x, y, 0.47, 0.2)
            a_bg = coverage(box, px)
            if a_bg <= 0:
                row.append((0, 0, 0, 0))
                continue
            t = clamp((x - y) * 0.9 + 0.5)
            col = blend(TOP_LEFT, BOTTOM_RIGHT, t)
            light = clamp(1 - math.hypot(x + 0.15, y - 0.3) * 1.6) * 0.18
            col = blend(col, (255, 255, 255), light)
            # Soft shadow under the diamond.
            shadow = sd_diamond(x - 0.012, y + 0.03, 0.27)
            col = blend(col, (20, 16, 60), clamp(0.5 - shadow / 0.06) * 0.35)
            # The diamond ring (the Aven mark).
            outer = sd_diamond(x, y, 0.27)
            inner = sd_diamond(x, y, 0.115)
            ring = max(outer, -inner)
            col = blend(col, (255, 255, 255), coverage(ring, px))
            # A small sparkle: learning is a bit of magic.
            sp = sd_sparkle(x - 0.25, y - 0.25, 0.11)
            col = blend(col, SPARKLE, coverage(sp, px))
            row.append(tuple(int(round(c)) for c in col) + (int(round(a_bg * 255)),))
        rows.append(row)
    return rows


def png_bytes(rows):
    h = len(rows)
    w = len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for p in row:
            raw += bytes(p)

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def ico(pngs):
    """Windows .ico with PNG-compressed entries (supported since Windows Vista)."""
    header = struct.pack("<HHH", 0, 1, len(pngs))
    offset = 6 + 16 * len(pngs)
    entries = b""
    body = b""
    for size, data in pngs:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        body += data
    return header + entries + body


def icns(pngs):
    """macOS .icns with PNG entries."""
    types = {128: b"ic07", 256: b"ic08", 512: b"ic09", 1024: b"ic10"}
    body = b""
    for size, data in pngs:
        if size in types:
            body += types[size] + struct.pack(">I", len(data) + 8) + data
    return b"icns" + struct.pack(">I", len(body) + 8) + body


SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#3b82f6"/>
      <stop offset="1" stop-color="#7c3aed"/>
    </linearGradient>
  </defs>
  <rect x="3" y="3" width="94" height="94" rx="20" fill="url(#bg)"/>
  <path d="M50 23 L77 50 L50 77 L23 50 Z M50 38.5 L38.5 50 L50 61.5 L61.5 50 Z" fill="#fff" fill-rule="evenodd"/>
  <path d="M75 14 L77.2 22.8 L86 25 L77.2 27.2 L75 36 L72.8 27.2 L64 25 L72.8 22.8 Z" fill="#fde047"/>
</svg>
"""


def main():
    sizes = [16, 32, 48, 64, 128, 256, 512, 1024]
    if "--quick" in sys.argv:
        sizes = [16, 32, 48, 64, 128, 256]
    pngs = {}
    for s in sizes:
        pngs[s] = png_bytes(render(s))
        print("rendered", s)
    for s in (16, 32, 48, 64, 128, 256):
        write(os.path.join(ROOT, "engine", "data", "icon", f"aven_{s}.png"), pngs[s])
    res = os.path.join(ROOT, "resources", "icon")
    for s in sizes:
        if s >= 512:
            write(os.path.join(res, f"aven_{s}.png"), pngs[s])
    write(os.path.join(res, "aven.ico"), ico([(s, pngs[s]) for s in (16, 32, 48, 64, 128, 256)]))
    if 1024 in pngs:
        write(os.path.join(res, "aven.icns"), icns([(s, pngs[s]) for s in (128, 256, 512, 1024)]))
    write(os.path.join(res, "aven.svg"), SVG.encode())


if __name__ == "__main__":
    main()
