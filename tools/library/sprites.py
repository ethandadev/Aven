"""2D art for the asset library: 16x16 pixel sprites, small animation sheets and parallax backgrounds."""

import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "templates"))
import art  # noqa: E402  the templates' sprites (hero, coin, slime...)
from textures import Fractal, hexc, mix  # noqa: E402

# ---------------------------------------------------------------- new sprites ('.' is transparent)

HEART = [
    "................",
    "................",
    "...KKK....KKK...",
    "..KRRRK..KRRRK..",
    ".KRWWRRKKRRRRRK.",
    ".KRWRRRRRRRRRRK.",
    ".KRRRRRRRRRRRRK.",
    ".KRRRRRRRRRRRDK.",
    "..KRRRRRRRRRDK..",
    "...KRRRRRRRDK...",
    "....KRRRRRDK....",
    ".....KRRRDK.....",
    "......KRDK......",
    ".......KK.......",
    "................",
    "................",
]
HEART_PAL = {"K": "#5a0d18", "R": "#ef4444", "D": "#b91c1c", "W": "#fecaca"}

KEY = [
    "................",
    "................",
    "................",
    "...KKKK.........",
    "..KYYYYK........",
    ".KYWKKYYK.......",
    ".KYK..KYKKKKKKK.",
    ".KYK..KYYYYYYYYK",
    ".KYK..KYKKKKYKYK",
    ".KYYKKYYK...KKK.",
    "..KYYYYK........",
    "...KKKK.........",
    "................",
    "................",
    "................",
    "................",
]
KEY_PAL = {"K": "#6b4204", "Y": "#facc15", "W": "#fef9c3"}

POTION = [
    "................",
    "......KKKK......",
    "......KBBK......",
    ".......KK.......",
    "......KGGK......",
    ".....KGGGGK.....",
    "....KGWGGGGK....",
    "...KRRWRRRRRK...",
    "...KRWRRRRRRK...",
    "...KRRRRRRRRK...",
    "...KRRRRRRRDK...",
    "...KRRRRRRDDK...",
    "....KRRRRDDK....",
    ".....KKKKKK.....",
    "................",
    "................",
]
POTION_PAL = {"K": "#1f2433", "B": "#92400e", "G": "#cbd5e1", "W": "#ffffff", "R": "#e11d48", "D": "#9f1239"}

STAR = [
    "................",
    ".......KK.......",
    "......KYYK......",
    "......KYYK......",
    ".....KYYYYK.....",
    "KKKKKKYWYYKKKKKK",
    "KYYYYYWYYYYYYYYK",
    ".KYYYYYYYYYYYYK.",
    "..KYYYYYYYYYYK..",
    "...KYYYYYYYYK...",
    "...KYYYYYYYYK...",
    "..KYYYYKKYYYYK..",
    "..KYYYK..KYYYK..",
    ".KYYKK....KKYYK.",
    ".KKK........KKK.",
    "................",
]
STAR_PAL = {"K": "#8a4b08", "Y": "#fde047", "W": "#fffbe0"}

BOMB = [
    "................",
    "..........Y.O...",
    "...........Y....",
    "..........K.....",
    ".........KK.....",
    "......KKKKKK....",
    "....KKDDDDDDKK..",
    "...KDDWDDDDDDDK.",
    "...KDWDDDDDDDDK.",
    "..KDDDDDDDDDDDDK",
    "..KDDDDDDDDDDDDK",
    "..KDDDDDDDDDDDDK",
    "...KDDDDDDDDDDK.",
    "...KDDDDDDDDDDK.",
    "....KKDDDDDDKK..",
    "......KKKKKK....",
]
BOMB_PAL = {"K": "#0b0f19", "D": "#374151", "W": "#9ca3af", "Y": "#fde047", "O": "#f97316"}

SWORD = [
    "..............KK",
    ".............KWK",
    "............KWGK",
    "...........KWGK.",
    "..........KWGK..",
    ".........KWGK...",
    "........KWGK....",
    "..KK...KWGK.....",
    "..KYK.KWGK......",
    "...KYKWGK.......",
    "....KYGK........",
    "...KBKYK........",
    "..KBK.KYK.......",
    ".KBK...KK.......",
    ".KK.............",
    "................",
]
SWORD_PAL = {"K": "#1f2433", "W": "#f1f5f9", "G": "#94a3b8", "Y": "#eab308", "B": "#7c2d12"}

SHIELD = [
    "................",
    "..KKKKKKKKKKKK..",
    "..KBBBBYYBBBBK..",
    "..KBWBBYYBBBBK..",
    "..KBBBBYYBBBBK..",
    "..KYYYYYYYYYYK..",
    "..KYYYYYYYYYYK..",
    "..KBBBBYYBBBBK..",
    "..KBBBBYYBBBBK..",
    "...KBBBYYBBBK...",
    "...KBBBYYBBBK...",
    "....KBBYYBBK....",
    ".....KBYYBK.....",
    "......KYYK......",
    ".......KK.......",
    "................",
]
SHIELD_PAL = {"K": "#1f2433", "B": "#2563eb", "W": "#93c5fd", "Y": "#facc15"}

APPLE = [
    "................",
    "........K.......",
    ".......KBK.GG...",
    "........KGGG....",
    "....KKKKKKKKK...",
    "...KRRRRRRRRRK..",
    "..KRWWRRRRRRRRK.",
    "..KRWRRRRRRRRRK.",
    "..KRRRRRRRRRRRK.",
    "..KRRRRRRRRRRDK.",
    "..KRRRRRRRRRRDK.",
    "...KRRRRRRRRDK..",
    "...KRRRRRRRDDK..",
    "....KKRRRDDKK...",
    "......KKKKK.....",
    "................",
]
APPLE_PAL = {"K": "#3f0d12", "R": "#dc2626", "D": "#991b1b", "W": "#fecaca", "B": "#5b3413", "G": "#16a34a"}

CRATE = [
    "KKKKKKKKKKKKKKKK",
    "KLLLLLLLLLLLLLLK",
    "KLKKKKKKKKKKKKLK",
    "KLKBBBBBBBBBKDLK",
    "KLKBBBBBBBBKDDLK",
    "KLKBBBBBBBKDDBLK",
    "KLKBBBBBBKDDBBLK",
    "KLKBBBBBKDDBBBLK",
    "KLKBBBBKDDBBBBLK",
    "KLKBBBKDDBBBBBLK",
    "KLKBBKDDBBBBBBLK",
    "KLKBKDDBBBBBBBLK",
    "KLKKDDKKKKKKKKLK",
    "KLLLLLLLLLLLLLLK",
    "KDDDDDDDDDDDDDDK",
    "KKKKKKKKKKKKKKKK",
]
CRATE_PAL = {"K": "#3b2412", "L": "#c08a52", "B": "#a26a3a", "D": "#7a4b28"}

BAT = [
    "................",
    "................",
    "................",
    "K......KK......K",
    "KK....KPPK....KK",
    "KPK..KPPPPK..KPK",
    "KPPKKPWKKWPKKPPK",
    "KPPPPPPPPPPPPPPK",
    "KPPPKPPPPPPKPPPK",
    ".KPK.KPPPPK.KPK.",
    "..K..KPWWPK..K..",
    ".......KK.......",
    "................",
    "................",
    "................",
    "................",
]
BAT_PAL = {"K": "#1e1033", "P": "#6d28d9", "W": "#fef3c7"}

GHOST = [
    "................",
    ".....KKKKKK.....",
    "....KWWWWWWK....",
    "...KWWWWWWWWK...",
    "..KWWWWWWWWWWK..",
    "..KWWKKWWKKWWK..",
    "..KWWKKWWKKWWK..",
    "..KWWWWWWWWWWK..",
    "..KWWWWKKWWWWK..",
    "..KWWWWWWWWWWK..",
    "..KWWWWWWWWWWK..",
    "..KWWWWWWWWWWK..",
    "..KWGWWWGWWWGK..",
    "..KWKGWKWGWKWK..",
    "..KK.KK.KK.KKK..",
    "................",
]
GHOST_PAL = {"K": "#1f2433", "W": "#f8fafc", "G": "#cbd5e1"}

CHEST = [
    "................",
    "................",
    "...KKKKKKKKKK...",
    "..KBBBBBBBBBBK..",
    ".KBLLLLLLLLLLBK.",
    ".KBBBBBBBBBBBBK.",
    ".KYYYYYYYYYYYYK.",
    ".KBBBBBYYBBBBBK.",
    ".KBBBBYKKYBBBBK.",
    ".KBBBBBYYBBBBBK.",
    ".KBBBBBBBBBBBBK.",
    ".KBBBBBBBBBBBBK.",
    ".KYYYYYYYYYYYYK.",
    ".KKKKKKKKKKKKKK.",
    "................",
    "................",
]
CHEST_PAL = {"K": "#2b1608", "B": "#8b5a2b", "L": "#b07c4c", "Y": "#facc15"}

SPRITES = [
    # (file stem, label, art, palette, tags)
    ("hero", "Hero", art.HERO, art.HERO_PAL, "character player"),
    ("slime", "Slime", art.SLIME, art.SLIME_PAL, "enemy character"),
    ("bat", "Bat", BAT, BAT_PAL, "enemy character flying"),
    ("ghost", "Ghost", GHOST, GHOST_PAL, "enemy character"),
    ("ship", "Spaceship", art.SHIP, art.SHIP_PAL, "player space vehicle"),
    ("asteroid", "Asteroid", art.ASTEROID, art.ASTEROID_PAL, "space hazard"),
    ("coin", "Coin", art.COIN, art.COIN_PAL, "pickup item money"),
    ("gem", "Gem", art.GEM, art.GEM_PAL, "pickup item treasure"),
    ("heart", "Heart", HEART, HEART_PAL, "pickup health ui"),
    ("star", "Star", STAR, STAR_PAL, "pickup bonus"),
    ("key", "Key", KEY, KEY_PAL, "pickup item door"),
    ("potion", "Potion", POTION, POTION_PAL, "pickup item health"),
    ("apple", "Apple", APPLE, APPLE_PAL, "pickup food"),
    ("cookie", "Cookie", art.COOKIE, art.COOKIE_PAL, "food clicker"),
    ("sword", "Sword", SWORD, SWORD_PAL, "weapon item"),
    ("shield", "Shield", SHIELD, SHIELD_PAL, "item armor"),
    ("bomb", "Bomb", BOMB, BOMB_PAL, "hazard weapon"),
    ("chest", "Treasure chest", CHEST, CHEST_PAL, "treasure prop"),
    ("crate", "Crate", CRATE, CRATE_PAL, "prop box"),
    ("spikes", "Spikes", art.SPIKES, art.SPIKES_PAL, "hazard trap"),
    ("flag", "Flag", art.FLAG, art.FLAG_PAL, "goal checkpoint"),
    ("portal", "Portal", art.PORTAL, art.PORTAL_PAL, "goal door"),
    ("tree", "Tree", art.TREE, art.TREE_PAL, "nature prop"),
]

for stem, _, a, pal, _ in SPRITES:
    assert len(a) == 16 and all(len(r) == 16 for r in a), stem
    assert all(ch == "." or ch in pal for r in a for ch in r), stem


def to_pixels(art_rows, palette):
    out = []
    for row in art_rows:
        line = []
        for ch in row:
            if ch == ".":
                line.append((0, 0, 0, 0))
            else:
                line.append(hexc(palette[ch]) + (255,))
        out.append(line)
    return out


# ---------------------------------------------------------------- animation sheets (frames side by side)


def hero_walk_frames():
    """Four walking frames of the hero: the legs and arms swing."""
    base = [list(r) for r in art.HERO]
    legs = {
        0: ["....KPPPPPPK....", "....KPPKKPPK....", "...KOOOKKOOOK...", "...KKKKKKKKKKK.."],
        1: ["....KPPPPPPK....", "...KPPK..KPPK...", "..KOOOK..KOOOK..", "..KKKKK..KKKKK.."],
        2: ["....KPPPPPPK....", "....KPPKKPPK....", "...KOOOKKOOOK...", "...KKKKKKKKKKK.."],
        3: ["....KPPPPPPK....", ".....KPPPPK.....", "....KOOOOOOK....", "....KKKKKKKK...."],
    }
    frames = []
    for f in range(4):
        rows = [r[:] for r in base]
        for i, line in enumerate(legs[f]):
            rows[12 + i] = list(line)
        if f in (1, 3):  # bob up a pixel on the stride
            rows = rows[1:] + [list("................")]
        frames.append(["".join(r) for r in rows])
    return frames


def coin_spin_frames():
    """The coin turning: full, narrower, edge, narrower."""
    full = art.COIN
    frames = [full]
    for width in (10, 4, 10):
        rows = []
        for r in full:
            # Squeeze horizontally around the middle.
            src = [c for c in r]
            out = ["."] * 16
            for x in range(16):
                sx = int(8 + (x - 7.5) * 16 / max(width, 1))
                if 0 <= sx < 16 and abs(x - 7.5) < width / 2:
                    out[x] = src[sx]
            rows.append("".join(out))
        frames.append(rows)
    return frames


def slime_bounce_frames():
    base = art.SLIME
    squashed = ["................"] + list(base[:-1])
    return [base, squashed]


def sheet(frames, palette):
    rows = []
    for y in range(16):
        line = []
        for f in frames:
            line += to_pixels([f[y]], palette)[0]
        rows.append(line)
    return rows


SHEETS = [
    # (file stem, label, frames, palette, tags)
    ("hero_walk", "Hero walking (4 frames)", hero_walk_frames, art.HERO_PAL, "character animation player"),
    ("coin_spin", "Coin spinning (4 frames)", coin_spin_frames, art.COIN_PAL, "pickup animation"),
    ("slime_bounce", "Slime bouncing (2 frames)", slime_bounce_frames, art.SLIME_PAL, "enemy animation"),
]

# ---------------------------------------------------------------- backgrounds (320 x 180, wrap left-right)

W, H = 320, 180


def sky(top, bottom, clouds=True, stars=False, seed=1, cloud_color="#ffffff", cloud_amount=1.0):
    rng = random.Random(seed)
    cloud = Fractal(seed, 4, 4)
    rows = []
    star_set = {(rng.randrange(W), rng.randrange(int(H * 0.7))) for _ in range(90)} if stars else set()
    for y in range(H):
        line = []
        base = mix(hexc(top), hexc(bottom), y / H)
        for x in range(W):
            c = base
            if clouds:
                n = cloud.at(x / W, y / H * 0.6)
                amount = max(0.0, (n - 0.58) * 4) * (1 - y / H) * cloud_amount
                c = mix(c, hexc(cloud_color), min(1.0, amount))
            if (x, y) in star_set:
                c = (255, 255, 240) if rng.random() > 0.3 else (190, 200, 255)
            line.append((int(c[0]), int(c[1]), int(c[2]), 255))
        rows.append(line)
    return rows


def ridge(color, base_height, amplitude, seed, jagged=False, detail=None):
    """A transparent layer with a hill or mountain silhouette."""
    f = Fractal(seed, 3 if not jagged else 2, 4 if not jagged else 5, 0.55 if not jagged else 0.45)
    heights = []
    for x in range(W):
        n = f.at(x / W, 0.5)
        if jagged:
            n = 1 - abs(n * 2 - 1)
        heights.append(base_height + amplitude * n)
    top = hexc(color)
    rows = []
    for y in range(H):
        line = []
        for x in range(W):
            h = heights[x]
            if H - y <= h:
                depth = (h - (H - y)) / max(h, 1)
                c = mix(top, tuple(v * 0.75 for v in top), depth)
                if detail and h - (H - y) < 2.5:
                    c = hexc(detail)  # a lighter edge, like snow or grass
                line.append((int(c[0]), int(c[1]), int(c[2]), 255))
            else:
                line.append((0, 0, 0, 0))
        rows.append(line)
    return rows


def forest(color, seed):
    """A row of pine silhouettes."""
    rng = random.Random(seed)
    trees = [(rng.randrange(W), rng.randint(40, 80), rng.randint(9, 16)) for _ in range(34)]
    c = hexc(color) + (255,)
    rows = [[(0, 0, 0, 0)] * W for _ in range(H)]
    for y in range(H - 20, H):
        for x in range(W):
            rows[y][x] = c
    for tx, th, tw in trees:
        for dy in range(th):
            y = H - 20 - dy
            half = tw * (1 - dy / th) * (0.7 + 0.3 * ((dy // 6) % 2))
            for dx in range(-int(half), int(half) + 1):
                rows[y][(tx + dx) % W] = c
    return rows


BACKGROUNDS = [
    # (file stem, label, function, tags)
    ("sky_day", "Day sky with clouds", lambda: sky("#5aa9e6", "#cdeefd", True, False, 3), "background sky"),
    ("sky_sunset", "Sunset sky", lambda: sky("#3b2f6b", "#f59e6b", True, False, 5, "#ffb89a", 0.55), "background sky evening"),
    ("sky_night", "Night sky with stars", lambda: sky("#070b1f", "#1d2d5c", False, True, 7), "background sky space night"),
    ("mountains", "Mountains (layer)", lambda: ridge("#6b7a99", 30, 110, 11, True, "#e7eef8"), "background layer parallax"),
    ("hills", "Hills (layer)", lambda: ridge("#4f9a35", 40, 35, 13, False, "#86c24a"), "background layer parallax"),
    ("forest", "Forest (layer)", lambda: forest("#1f4d2b", 17), "background layer parallax"),
]
