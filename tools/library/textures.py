"""Tileable surface textures for 3D (and 2D backgrounds), drawn with noise in plain Python.

Every texture wraps around: the left edge continues the right one, and the top the bottom, so
they can repeat across a floor (MeshRenderer tiling). Colors are sRGB.
"""

import math
import random


# ---------------------------------------------------------------- periodic noise


class Noise:
    """Value noise that repeats every `period` cells, and fractal sums of it."""

    def __init__(self, seed, period=8):
        rng = random.Random(seed)
        self.period = period
        self.values = [[rng.random() for _ in range(period)] for _ in range(period)]

    def at(self, x, y):
        """x, y in cells; wraps around every `period` cells."""
        p = self.period
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        v = self.values
        a = v[y0 % p][x0 % p]
        b = v[y0 % p][(x0 + 1) % p]
        c = v[(y0 + 1) % p][x0 % p]
        d = v[(y0 + 1) % p][(x0 + 1) % p]
        top = a + (b - a) * sx
        bottom = c + (d - c) * sx
        return top + (bottom - top) * sy


class Fractal:
    """Octaves of periodic noise. u, v in 0..1 cover the texture once."""

    def __init__(self, seed, base=4, octaves=5, gain=0.5):
        self.layers = [Noise(seed + i * 101, base * (2 ** i)) for i in range(octaves)]
        self.gain = gain

    def at(self, u, v):
        total, amp, norm = 0.0, 1.0, 0.0
        for n in self.layers:
            total += n.at(u * n.period, v * n.period) * amp
            norm += amp
            amp *= self.gain
        return total / norm


class Cells:
    """Worley (cellular) noise on a wrapping grid: distance to the nearest random point."""

    def __init__(self, seed, cells=8):
        rng = random.Random(seed)
        self.n = cells
        self.points = [[(rng.random(), rng.random()) for _ in range(cells)] for _ in range(cells)]
        self.ids = [[rng.random() for _ in range(cells)] for _ in range(cells)]

    def at(self, u, v):
        """Returns (nearest distance, second nearest distance, id of the nearest cell), in cells."""
        n = self.n
        x, y = u * n, v * n
        cx, cy = math.floor(x), math.floor(y)
        best, second, bid = 9.0, 9.0, 0.0
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                gx, gy = cx + dx, cy + dy
                px, py = self.points[gy % n][gx % n]
                d = math.hypot(gx + px - x, gy + py - y)
                if d < best:
                    best, second, bid = d, best, self.ids[gy % n][gx % n]
                elif d < second:
                    second = d
        return best, second, bid


# ---------------------------------------------------------------- colors


def hexc(h):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def scale(c, f):
    return tuple(c[i] * f for i in range(3))


def ramp(stops, t):
    """stops: [(position, color)], positions increasing."""
    t = max(0.0, min(1.0, t))
    for i in range(len(stops) - 1):
        p0, c0 = stops[i]
        p1, c1 = stops[i + 1]
        if t <= p1:
            return mix(c0, c1, (t - p0) / max(p1 - p0, 1e-6))
    return stops[-1][1]


def image(size, shader):
    """Runs shader(u, v) -> (r, g, b[, a]) floats 0-255 for every pixel; returns PNG rows."""
    rows = []
    for y in range(size):
        v = (y + 0.5) / size
        row = []
        for x in range(size):
            c = shader((x + 0.5) / size, v)
            a = c[3] if len(c) > 3 else 255
            row.append((int(max(0, min(255, c[0]))), int(max(0, min(255, c[1]))), int(max(0, min(255, c[2]))),
                        int(max(0, min(255, a)))))
        rows.append(row)
    return rows


# ---------------------------------------------------------------- the textures


def grass(size=256):
    f = Fractal(11, 4, 5)
    fine = Fractal(12, 32, 2)
    blades = Cells(13, 48)
    dark, mid, light = hexc("#2f6b25"), hexc("#4f9a35"), hexc("#86c24a")

    def shade(u, v):
        n = f.at(u, v)
        c = ramp([(0.25, dark), (0.55, mid), (0.85, light)], n)
        d, _, _ = blades.at(u, v)
        c = scale(c, 0.85 + 0.3 * fine.at(u, v) + (0.12 if d < 0.18 else 0.0))
        return c

    return image(size, shade)


def dirt(size=256):
    f = Fractal(21, 4, 5)
    stones = Cells(22, 20)
    base = [(0.2, hexc("#4a3222")), (0.6, hexc("#6e4b31")), (0.95, hexc("#8a6443"))]

    def shade(u, v):
        c = ramp(base, f.at(u, v))
        d, _, i = stones.at(u, v)
        if d < 0.22 and i > 0.6:
            c = mix(c, hexc("#9a8d7e"), 0.7 - d * 2)
        return c

    return image(size, shade)


def sand(size=256):
    f = Fractal(31, 4, 4)
    grain = Fractal(32, 64, 1)

    def shade(u, v):
        ripple = 0.5 + 0.5 * math.sin((v * 12 + f.at(u, v) * 3) * 2 * math.pi)
        c = mix(hexc("#d6bd84"), hexc("#ecd9a6"), ripple * 0.5 + f.at(u, v) * 0.5)
        return scale(c, 0.92 + 0.16 * grain.at(u, v))

    return image(size, shade)


def rock(size=256):
    f = Fractal(41, 4, 6)
    ridges = Fractal(42, 6, 4)

    def shade(u, v):
        n = f.at(u, v)
        r = 1 - abs(ridges.at(u, v) * 2 - 1)  # ridged: bright lines, dark cracks
        c = ramp([(0.2, hexc("#4b4e52")), (0.55, hexc("#74777b")), (0.9, hexc("#9c9ea0"))], n)
        return scale(c, 0.7 + 0.4 * r ** 3)

    return image(size, shade)


def cobblestone(size=256):
    cells = Cells(51, 7)
    f = Fractal(52, 8, 4)

    def shade(u, v):
        d1, d2, i = cells.at(u, v)
        edge = d2 - d1
        stone = ramp([(0, hexc("#5d5f63")), (1, hexc("#8e9094"))], i * 0.6 + f.at(u, v) * 0.4)
        stone = scale(stone, 0.8 + 0.3 * min(1, edge * 4))  # rounded: lighter in the middle
        if edge < 0.08:
            return mix(hexc("#2f2d2a"), stone, edge / 0.08)
        return stone

    return image(size, shade)


def bricks(size=256, rows=8, cols=4):
    f = Fractal(61, 8, 4)
    rng = random.Random(62)
    tint = [[rng.uniform(0.82, 1.1) for _ in range(cols)] for _ in range(rows)]

    def shade(u, v):
        row = int(v * rows)
        offset = 0.5 if row % 2 else 0.0
        x = u * cols + offset
        col = int(x) % cols
        fx, fy = x - math.floor(x), v * rows - row
        mortar = 0.06
        n = f.at(u, v)
        if fx < mortar or fy < mortar * 2:
            return scale(hexc("#b8b0a2"), 0.85 + 0.25 * n)
        c = scale(hexc("#9c4a32"), tint[row][col] * (0.85 + 0.3 * n))
        return c

    return image(size, shade)


def planks(size=256, count=4):
    grain = Fractal(71, 4, 4)
    rng = random.Random(72)
    shades = [rng.uniform(0.85, 1.12) for _ in range(count)]

    def shade(u, v):
        p = int(u * count)
        fx = u * count - p
        if fx < 0.035:
            return hexc("#3a2616")
        w = grain.at(u * 0.5 + p * 0.37, v)
        rings = 0.5 + 0.5 * math.sin((fx * 3 + w * 6) * math.pi * 2)
        c = mix(hexc("#8a5a33"), hexc("#b07c4c"), rings * 0.6 + w * 0.4)
        return scale(c, shades[p])

    return image(size, shade)


def metal(size=256):
    streak = Fractal(81, 2, 5)
    fine = Fractal(82, 64, 1)

    def shade(u, v):
        s = streak.at(u * 0.05, v)  # stretched along u: brushed lines
        c = scale(hexc("#8c9299"), 0.85 + 0.25 * s + 0.08 * fine.at(u, v))
        # Plate seams and rivets every half texture.
        fx, fy = (u * 2) % 1, (v * 2) % 1
        if fx < 0.012 or fy < 0.012:
            return scale(c, 0.55)
        for rx in (0.06, 0.94):
            for ry in (0.06, 0.94):
                d = math.hypot(fx - rx, fy - ry)
                if d < 0.025:
                    return scale(hexc("#b8bec5"), 1.1 - d * 12)
        return c

    return image(size, shade)


def concrete(size=256):
    f = Fractal(91, 4, 6)
    spots = Cells(92, 40)

    def shade(u, v):
        c = scale(hexc("#9a9a96"), 0.85 + 0.25 * f.at(u, v))
        d, _, i = spots.at(u, v)
        if d < 0.1 and i > 0.7:
            c = scale(c, 0.8)
        return c

    return image(size, shade)


def floor_tiles(size=256, count=4):
    f = Fractal(101, 4, 4)

    def shade(u, v):
        fx, fy = (u * count) % 1, (v * count) % 1
        if fx < 0.04 or fy < 0.04:
            return hexc("#6b6660")
        checker = (int(u * count) + int(v * count)) % 2
        base = hexc("#e7e2d8") if checker else hexc("#cfc8bb")
        return scale(base, 0.95 + 0.08 * f.at(u, v))

    return image(size, shade)


def water(size=256):
    caustics = Cells(111, 6)
    f = Fractal(112, 4, 4)

    def shade(u, v):
        d1, d2, _ = caustics.at(u + 0.05 * f.at(u, v), v)
        line = max(0.0, 1 - (d2 - d1) * 8)
        c = mix(hexc("#1d5b8f"), hexc("#2f86c0"), f.at(u, v))
        return mix(c, hexc("#bfe6ff"), line * 0.6)

    return image(size, shade)


def snow(size=256):
    f = Fractal(121, 4, 5)
    sparkle = Cells(122, 64)

    def shade(u, v):
        c = mix(hexc("#c9d6e6"), hexc("#f4f8fc"), f.at(u, v) * 1.2)
        d, _, i = sparkle.at(u, v)
        if d < 0.08 and i > 0.85:
            c = (255, 255, 255)
        return c

    return image(size, shade)


def lava(size=256):
    cells = Cells(131, 6)
    f = Fractal(132, 4, 4)

    def shade(u, v):
        d1, d2, _ = cells.at(u, v)
        crack = max(0.0, 1 - (d2 - d1) * 5)
        crust = scale(hexc("#2b1a16"), 0.8 + 0.5 * f.at(u, v))
        glow = ramp([(0, hexc("#b8260b")), (0.6, hexc("#ff7a1a")), (1, hexc("#ffe06a"))], crack)
        return mix(crust, glow, crack ** 0.7)

    return image(size, shade)


def bark(size=256):
    f = Fractal(141, 4, 5)

    def shade(u, v):
        n = f.at(u * 3 % 1, v * 0.25)  # long vertical ridges
        ridge = 0.5 + 0.5 * math.sin((u * 10 + n * 2) * 2 * math.pi)
        c = mix(hexc("#3d2a1c"), hexc("#6d4c33"), ridge * 0.7 + f.at(u, v) * 0.3)
        return c

    return image(size, shade)


def leaves(size=256):
    cells = Cells(151, 14)
    f = Fractal(152, 4, 4)

    def shade(u, v):
        d, _, i = cells.at(u, v)
        c = ramp([(0, hexc("#1f4d1d")), (0.5, hexc("#3a7d2c")), (1, hexc("#6fae3f"))], i * 0.6 + f.at(u, v) * 0.4)
        return scale(c, 1.1 - d * 0.6)

    return image(size, shade)


def prototype_grid(color, size=256):
    """The "grey box" texture for blocking out levels: 1 m squares with a thicker line every 4."""
    base = hexc(color)

    def shade(u, v):
        fx, fy = (u * 4) % 1, (v * 4) % 1
        line = fx < 0.02 or fy < 0.02 or fx > 0.98 or fy > 0.98
        major = u < 0.008 or v < 0.008 or u > 0.992 or v > 0.992
        c = scale(base, 0.55 if major else 0.75 if line else 1.0)
        return c

    return image(size, shade)


TEXTURES = [
    # (file stem, label, function, suggested roughness, metallic, tags)
    ("grass", "Grass", grass, 0.9, 0.0, "ground nature"),
    ("dirt", "Dirt", dirt, 0.95, 0.0, "ground nature"),
    ("sand", "Sand", sand, 0.9, 0.0, "ground desert beach"),
    ("rock", "Rock", rock, 0.85, 0.0, "stone cliff nature"),
    ("cobblestone", "Cobblestone", cobblestone, 0.8, 0.0, "stone path street"),
    ("bricks", "Bricks", bricks, 0.85, 0.0, "wall building"),
    ("planks", "Wood planks", planks, 0.7, 0.0, "wood floor wall"),
    ("bark", "Tree bark", bark, 0.9, 0.0, "wood nature"),
    ("leaves", "Leaves", leaves, 0.8, 0.0, "nature bush hedge"),
    ("metal", "Metal plates", metal, 0.35, 1.0, "sci-fi industrial floor"),
    ("concrete", "Concrete", concrete, 0.9, 0.0, "city wall floor"),
    ("tiles", "Floor tiles", floor_tiles, 0.3, 0.0, "floor indoor"),
    ("water", "Water", water, 0.1, 0.0, "liquid sea"),
    ("snow", "Snow", snow, 0.7, 0.0, "ground winter"),
    ("lava", "Lava", lava, 0.6, 0.0, "hazard fire"),
    ("grid_gray", "Prototype grid (gray)", lambda: prototype_grid("#b7bcc4"), 0.8, 0.0, "prototype blockout level design"),
    ("grid_orange", "Prototype grid (orange)", lambda: prototype_grid("#f0a050"), 0.8, 0.0, "prototype blockout level design"),
    ("grid_blue", "Prototype grid (blue)", lambda: prototype_grid("#6fa8e8"), 0.8, 0.0, "prototype blockout level design"),
]
