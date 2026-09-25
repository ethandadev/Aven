"""Draws the starter tileset (editor/data/starter_tiles.png): 16 tiles of 16x16 pixels in a 4x4 grid.

    python3 tools/art/make_starter_tiles.py

Tile numbers (row by row):
     0 grass       1 dirt        2 stone       3 brick
     4 planks      5 sand        6 water       7 ice
     8 leaves      9 crate      10 metal      11 lava
    12 cloud      13 spikes     14 bonus      15 ladder
"""

import os
import random

from PIL import Image

T = 16
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "editor", "data", "starter_tiles.png")


def hexc(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c[:3]) + (c[3],)


class Tile:
    def __init__(self, seed):
        self.px = [[(0, 0, 0, 0)] * T for _ in range(T)]
        self.rng = random.Random(seed)

    def set(self, x, y, c):
        if 0 <= x < T and 0 <= y < T:
            self.px[y][x] = c

    def fill(self, c, x0=0, y0=0, x1=T - 1, y1=T - 1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def speckle(self, colors, amount, x0=0, y0=0, x1=T - 1, y1=T - 1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if self.rng.random() < amount:
                    self.set(x, y, self.rng.choice(colors))


def grass(t):
    dirt, dark = hexc("#8f563b"), hexc("#663931")
    t.fill(dirt)
    t.speckle([dark, hexc("#a0694a")], 0.18, y0=5)
    g, g2, g3 = hexc("#6abe30"), hexc("#4b8f24"), hexc("#99e550")
    t.fill(g, y1=3)
    t.fill(g3, y1=0)
    for x in range(T):  # a wavy fringe of grass over the dirt
        depth = 4 + (1 if (x * 7 + 3) % 5 < 2 else 0) + (1 if x % 6 == 2 else 0)
        for y in range(4, depth + 1):
            t.set(x, y, g2 if y == depth else g)


def dirt(t):
    t.fill(hexc("#8f563b"))
    t.speckle([hexc("#663931"), hexc("#a0694a")], 0.2)
    for _ in range(3):  # small pebbles
        x, y = t.rng.randrange(1, T - 2), t.rng.randrange(1, T - 2)
        t.set(x, y, hexc("#9badb7"))
        t.set(x + 1, y, hexc("#847e87"))


def stone(t):
    base, dark, light = hexc("#847e87"), hexc("#595652"), hexc("#9badb7")
    t.fill(base)
    t.speckle([hexc("#76707a")], 0.25)
    # Cracks between big stones.
    for x in range(T):
        t.set(x, 7, dark)
    for y in range(0, 7):
        t.set(5, y, dark)
    for y in range(8, T):
        t.set(11, y, dark)
    for x in range(T):
        t.set(x, 8, light)
    for y in range(0, 7):
        t.set(6, y, light)
    t.fill(dark, y0=T - 1)


def brick(t):
    mortar, b, b2 = hexc("#d9c7b0"), hexc("#ac3232"), hexc("#8f2a2a")
    t.fill(mortar)
    for row in range(4):
        y0 = row * 4
        off = 0 if row % 2 == 0 else 4
        for bx in range(-1, 3):
            x0 = bx * 8 + off
            for y in range(y0, y0 + 3):
                for x in range(x0, x0 + 7):
                    t.set(x, y, b if y < y0 + 2 else b2)
    t.speckle([hexc("#c24545")], 0.06)


def planks(t):
    wood, dark, light = hexc("#c28b4f"), hexc("#8a5a2b"), hexc("#d9a066")
    t.fill(wood)
    for y in (0, 5, 10, 15):
        t.fill(dark, y0=y, y1=y)
    for y in (1, 6, 11):
        t.fill(light, y0=y, y1=y)
    for (x, y0) in ((4, 1), (11, 6), (6, 11)):
        for y in range(y0, y0 + 4):
            t.set(x, y, dark)
    t.speckle([hexc("#b37d45")], 0.08)
    for (x, y) in ((2, 3), (13, 3), (8, 8), (2, 13), (13, 13)):
        t.set(x, y, hexc("#5a3a1c"))  # nails


def sand(t):
    t.fill(hexc("#eec39a"))
    t.speckle([hexc("#d9a066"), hexc("#f5d6b0")], 0.22)
    for x in range(T):
        t.set(x, 0, hexc("#f7dfc0"))


def water(t):
    deep, mid, foam = hexc("#306082", 220), hexc("#5b6ee1", 220), hexc("#cbdbfc", 235)
    for y in range(T):
        for x in range(T):
            t.set(x, y, mid if y < 7 else deep)
    for x in range(T):  # waves on top
        y = 1 if (x // 4) % 2 == 0 else 2
        t.set(x, y, foam)
        t.set(x, y + 1, hexc("#639bff", 230))
    for (x, y) in ((3, 9), (4, 9), (10, 12), (11, 12), (7, 5)):
        t.set(x, y, hexc("#639bff", 230))


def ice(t):
    t.fill(hexc("#a6d8f5"))
    t.speckle([hexc("#cbe8fa")], 0.1)
    light = hexc("#ffffff")
    for i in range(5):  # a shine streak
        t.set(3 + i, 2 + i, light)
        t.set(4 + i, 2 + i, hexc("#e4f4fd"))
    t.fill(hexc("#6fb3dd"), y0=T - 1)
    t.fill(hexc("#6fb3dd"), x0=T - 1)
    t.fill(light, y1=0)


def leaves(t):
    t.fill(hexc("#37946e"))
    t.speckle([hexc("#4b692f"), hexc("#6abe30"), hexc("#2a7055")], 0.35)
    for _ in range(4):
        x, y = t.rng.randrange(1, T - 2), t.rng.randrange(1, T - 2)
        t.set(x, y, hexc("#99e550"))


def crate(t):
    wood, dark, light = hexc("#d9a066"), hexc("#8a5a2b"), hexc("#eec39a")
    t.fill(wood)
    t.fill(dark, y1=1)
    t.fill(dark, y0=T - 2)
    t.fill(dark, x1=1)
    t.fill(dark, x0=T - 2)
    for i in range(2, T - 2):  # the X brace
        t.set(i, i, dark)
        t.set(T - 1 - i, i, dark)
        t.set(i, i + 1, light)
    t.fill(light, x0=2, y0=2, x1=T - 3, y1=2)


def metal(t):
    base, dark, light = hexc("#9badb7"), hexc("#696a6a"), hexc("#cbdbfc")
    t.fill(base)
    t.fill(light, y1=0)
    t.fill(light, x1=0)
    t.fill(dark, y0=T - 1)
    t.fill(dark, x0=T - 1)
    for (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):  # rivets
        t.set(x, y, dark)
        t.set(x - 1, y - 1, light)
    for i in range(4, 12):
        t.set(i, 7, hexc("#847e87"))
        t.set(i, 8, light)


def lava(t):
    t.fill(hexc("#d95763"))
    t.speckle([hexc("#df7126"), hexc("#ac3232")], 0.3)
    for (x, y) in ((3, 5), (4, 5), (10, 9), (11, 10), (6, 12)):
        t.set(x, y, hexc("#fbf236"))
    for x in range(T):
        t.set(x, 0 if x % 5 else 1, hexc("#fbf236"))


def cloud(t):
    white, soft, edge = hexc("#ffffff"), hexc("#dbe7f5"), hexc("#9fb4cc")
    blobs = ((4.5, 8.5, 3.6), (8.5, 6.5, 4.6), (12.0, 8.5, 3.4))

    def inside(x, y):
        if 2 <= x <= 13 and 8 <= y <= 12:
            return True
        return any((x - cx) ** 2 + (y - cy) ** 2 <= r * r for cx, cy, r in blobs)

    for y in range(T):
        for x in range(T):
            if inside(x + 0.5, y + 0.5):
                near_edge = not all(inside(x + 0.5 + dx, y + 0.5 + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                t.set(x, y, edge if near_edge else (soft if y >= 10 else white))


def spikes(t):
    light, mid, dark = hexc("#e4ecf7"), hexc("#9badb7"), hexc("#595652")
    apex, base = 2, T - 3
    for s in range(2):
        cx = s * 8 + 4
        for y in range(apex, base + 1):
            half = (y - apex + 1) / (base - apex + 1) * 4
            for x in range(s * 8, s * 8 + 8):
                d = x + 0.5 - cx
                if abs(d) <= half:
                    t.set(x, y, light if d < 0 else mid)
    t.fill(dark, y0=T - 2)
    t.fill(hexc("#847e87"), y0=T - 2, y1=T - 2)


def bonus(t):
    gold, dark, light = hexc("#fbbf24"), hexc("#b45309"), hexc("#fff3c4")
    t.fill(gold)
    t.fill(dark, y0=T - 1)
    t.fill(dark, x0=T - 1)
    t.fill(light, y1=0)
    t.fill(light, x1=0)
    q = ["..XXXX..", ".XX..XX.", ".....XX.", "....XX..", "...XX...", "........", "...XX...", "...XX..."]
    for y, row in enumerate(q):
        for x, ch in enumerate(row):
            if ch == "X":
                t.set(x + 4, y + 4, dark)
                t.set(x + 3, y + 3, hexc("#ffffff"))
    for (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):
        t.set(x, y, dark)


def ladder(t):
    wood, dark = hexc("#c28b4f"), hexc("#8a5a2b")
    for y in range(T):
        for x in (2, 3, 12, 13):
            t.set(x, y, wood if x in (2, 12) else dark)
    for y in (2, 7, 12):
        for x in range(4, 12):
            t.set(x, y, wood)
            t.set(x, y + 1, dark)


def main():
    painters = [grass, dirt, stone, brick, planks, sand, water, ice, leaves, crate, metal, lava, cloud, spikes, bonus, ladder]
    sheet = Image.new("RGBA", (T * 4, T * 4))
    for i, paint in enumerate(painters):
        t = Tile(seed=100 + i)
        paint(t)
        img = Image.new("RGBA", (T, T))
        img.putdata([c for row in t.px for c in row])
        sheet.paste(img, ((i % 4) * T, (i // 4) * T))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
