"""Pixel art used by the starter templates. Each sprite is 16x16; '.' is transparent."""

from common import pixel_art

HERO = [
    "................",
    "................",
    ".....KKKKKK.....",
    "....KHHHHHHK....",
    "...KHHHHHHHHK...",
    "...KHSSSSSSHK...",
    "...KSSWESWESK...",
    "...KSSWESWESK...",
    "....KSSSSSSK....",
    "...KBBBBBBBBK...",
    "..KSBBBBBBBBSK..",
    "..KSKBBDDBBKSK..",
    "....KPPPPPPK....",
    "....KPPKKPPK....",
    "...KOOOKKOOOK...",
    "...KKKKKKKKKKK..",
]
HERO_PAL = {"K": "#1f2433", "H": "#7c3f1d", "S": "#f6c89f", "W": "#ffffff", "E": "#1f2433",
            "B": "#3b82f6", "D": "#1d4ed8", "P": "#374151", "O": "#92400e"}

COIN = [
    "................",
    ".....KKKKKK.....",
    "...KKYYYYYYKK...",
    "..KYYWWYYYYYYK..",
    "..KYWWYYYYYYYK..",
    ".KYYWYYOOYYYYYK.",
    ".KYYYYYOOYYYYYK.",
    ".KYYYYYOOYYYYYK.",
    ".KYYYYYOOYYYYYK.",
    ".KYYYYYOOYYYYYK.",
    ".KYYYYYYYYYYYYK.",
    "..KYYYYYYYYYYK..",
    "..KOYYYYYYYYOK..",
    "...KKOOOOOOKK...",
    ".....KKKKKK.....",
    "................",
]
COIN_PAL = {"K": "#8a4b08", "Y": "#fbbf24", "O": "#d97706", "W": "#fff7d6"}

SLIME = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "......KKKK......",
    "....KKGGGGKK....",
    "...KGGGGGGGGK...",
    "..KGWWGGGGWWGK..",
    "..KGWKGGGGWKGK..",
    ".KGGGGGGGGGGGGK.",
    ".KGGGGKKKKGGGGK.",
    ".KGGGGGGGGGGGGK.",
    ".KDGGGGGGGGGGDK.",
    "..KDDDDDDDDDDK..",
    "...KKKKKKKKKK...",
]
SLIME_PAL = {"K": "#1f2433", "G": "#a855f7", "D": "#7e22ce", "W": "#ffffff"}

FLAG = [
    "..KK............",
    "..KWKKKKKKKK....",
    "..KWRRRRRRRRK...",
    "..KWRRRRRRRRRK..",
    "..KWRRWWRRRRRK..",
    "..KWRRRRRRRRK...",
    "..KWRRRRRRRK....",
    "..KWKKKKKKK.....",
    "..KWK...........",
    "..KWK...........",
    "..KWK...........",
    "..KWK...........",
    "..KWK...........",
    "..KWK...........",
    ".KKKKK..........",
    "KGGGGGK.........",
]
FLAG_PAL = {"K": "#1f2433", "W": "#e5e7eb", "R": "#ef4444", "G": "#6b7280"}

SPIKES = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "...K......K.....",
    "..KWK....KWK....",
    "..KWK....KWK....",
    ".KWGGK..KWGGK...",
    ".KWGGK..KWGGK...",
    "KWGGGGKKWGGGGK..",
    "KWGGGGKKWGGGGK..",
    "KKKKKKKKKKKKKKK.",
    "KKKKKKKKKKKKKKK.",
]
SPIKES_PAL = {"K": "#1f2433", "W": "#f3f4f6", "G": "#9ca3af"}

SHIP = [
    ".......KK.......",
    "......KWWK......",
    "......KWWK......",
    ".....KWCCWK.....",
    ".....KWCCWK.....",
    "....KWWCCWWK....",
    "....KWWWWWWK....",
    "...KWWWWWWWWK...",
    "..KRKWWWWWWKRK..",
    ".KRRKWWWWWWKRRK.",
    "KRRRKWWWWWWKRRRK",
    "KRRKKWWKKWWKKRRK",
    "KKK.KWK..KWK.KKK",
    "....KOK..KOK....",
    ".....Y....Y.....",
    "................",
]
SHIP_PAL = {"K": "#111827", "W": "#e5e7eb", "C": "#38bdf8", "R": "#ef4444", "O": "#f97316", "Y": "#fde047"}

ASTEROID = [
    "................",
    ".....KKKKK......",
    "...KKGGGGGKK....",
    "..KGGGGDGGGGK...",
    "..KGDDGGGGGGGK..",
    ".KGGDDGGGGDGGK..",
    ".KGGGGGGGGGGGGK.",
    ".KGGGGGGGDDGGGK.",
    "KGGGDGGGGDDGGGK.",
    "KGGGGGGGGGGGGGK.",
    ".KGGGGGDGGGGGK..",
    ".KGDDGGGGGGDGK..",
    "..KGDGGGGGGGK...",
    "...KKGGGGGKK....",
    ".....KKKKK......",
    "................",
]
ASTEROID_PAL = {"K": "#292524", "G": "#78716c", "D": "#57534e"}

GEM = [
    "................",
    "................",
    "....KKKKKKKK....",
    "...KWCCCCCCCK...",
    "..KWWCCCCCCCCK..",
    ".KKKKKKKKKKKKKK.",
    ".KCWCCCCCCCCBCK.",
    "..KCWCCCCCCBCK..",
    "...KCCCCCCBCK...",
    "....KCCCCBCK....",
    ".....KCCBCK.....",
    "......KBCK......",
    ".......KK.......",
    "................",
    "................",
    "................",
]
GEM_PAL = {"K": "#0e3a4f", "C": "#22d3ee", "B": "#0891b2", "W": "#ecfeff"}

TREE = [
    "......KKKK......",
    "....KKGGGGKK....",
    "...KGGLGGGGGK...",
    "..KGLLGGGGGGGK..",
    "..KGLGGGGGDGGK..",
    ".KGGGGGGGGGDGGK.",
    ".KGGGGGGGGGGGGK.",
    ".KGGGDGGGGGGGGK.",
    ".KGGDDGGGGGGDGK.",
    "..KGGGGGGGDDGK..",
    "..KKGGGGGGGGKK..",
    "....KKKKKKKK....",
    "......KBBK......",
    "......KBBK......",
    ".....KBBBBK.....",
    "......KKKK......",
]
TREE_PAL = {"K": "#14301c", "G": "#22a045", "L": "#4ade80", "D": "#15803d", "B": "#7c4a1e"}

COOKIE = [
    "................",
    ".....KKKKKK.....",
    "...KKCCCCCCKK...",
    "..KCCCCLCCCCCK..",
    "..KCDDCCCCCCCK..",
    ".KCCDDCCCCDDCCK.",
    ".KCCCCCCCCDDCCK.",
    ".KCLCCCDDCCCCCK.",
    ".KCCCCCDDCCCLCK.",
    ".KCCDDCCCCCCCCK.",
    ".KCCDDCCCCDDCCK.",
    "..KCCCCCCCDDCK..",
    "..KCCCLCCCCCCK..",
    "...KKCCCCCCKK...",
    ".....KKKKKK.....",
    "................",
]
COOKIE_PAL = {"K": "#5b3413", "C": "#d9a066", "L": "#f0c890", "D": "#3f230c"}

PORTAL = [
    "................",
    ".....KKKKKK.....",
    "...KKPPPPPPKK...",
    "..KPPWWWWWWPPK..",
    "..KPWPPPPPPWPK..",
    ".KPWPPWWWWPPWPK.",
    ".KPWPWPPPPWPWPK.",
    ".KPWPWPWWPWPWPK.",
    ".KPWPWPWWPWPWPK.",
    ".KPWPWPPPPWPWPK.",
    ".KPWPPWWWWPPWPK.",
    "..KPWPPPPPPWPK..",
    "..KPPWWWWWWPPK..",
    "...KKPPPPPPKK...",
    ".....KKKKKK.....",
    "................",
]
PORTAL_PAL = {"K": "#3b0764", "P": "#9333ea", "W": "#e9d5ff"}


def write_all(template, names):
    """Writes the named sprites into template/images/."""
    sprites = {
        "hero": (HERO, HERO_PAL), "coin": (COIN, COIN_PAL), "slime": (SLIME, SLIME_PAL),
        "flag": (FLAG, FLAG_PAL), "spikes": (SPIKES, SPIKES_PAL), "ship": (SHIP, SHIP_PAL),
        "asteroid": (ASTEROID, ASTEROID_PAL), "gem": (GEM, GEM_PAL), "tree": (TREE, TREE_PAL),
        "cookie": (COOKIE, COOKIE_PAL), "portal": (PORTAL, PORTAL_PAL),
    }
    for n in names:
        art, pal = sprites[n]
        pixel_art(template.path(f"images/{n}.png"), art, pal)
