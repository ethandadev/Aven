"""Helpers for generating Aven starter templates: tiny PNG/WAV writers and a scene builder.

Only the Python standard library is used, so anyone can regenerate the templates.
"""

import json
import math
import os
import random
import shutil
import struct
import wave
import zlib

# ---------------------------------------------------------------- colors


def rgba(hex_color, alpha=1.0):
    """'#3b82f6' -> [r, g, b, a] with floats rounded for readable scene files."""
    h = hex_color.lstrip("#")
    return [round(int(h[i:i + 2], 16) / 255, 3) for i in (0, 2, 4)] + [alpha]


def _rgb255(hex_color):
    h = hex_color.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


# ---------------------------------------------------------------- images


def write_png(path, width, height, pixels):
    """pixels: list of rows (top to bottom) of (r, g, b, a) tuples, 0-255."""
    raw = bytearray()
    for row in pixels:
        raw.append(0)  # no filter
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def pixel_art(path, art, palette, scale=1):
    """Draws ASCII art. Each character is looked up in `palette` ('.' is transparent)."""
    height = len(art)
    width = len(art[0])
    for i, row in enumerate(art):
        assert len(row) == width, f"{path}: row {i} has {len(row)} pixels, expected {width}"
    rows = []
    for row in art:
        out = []
        for ch in row:
            if ch == "." or ch == " ":
                px = (0, 0, 0, 0)
            else:
                px = _rgb255(palette[ch]) + (255,)
            out += [px] * scale
        for _ in range(scale):
            rows.append(list(out))
    write_png(path, width * scale, height * scale, rows)


# ---------------------------------------------------------------- sounds

RATE = 22050


def write_wav(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = bytearray()
        for s in samples:
            v = max(-1.0, min(1.0, s))
            frames += struct.pack("<h", int(v * 32000))
        w.writeframes(bytes(frames))


def _square(phase):
    return 1.0 if (phase % 1.0) < 0.5 else -1.0


def tone(freq_from, freq_to, seconds, volume=0.3, wave_shape="square", decay=3.0):
    """A pitch sweep with a fading envelope."""
    out = []
    phase = 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / n
        f = freq_from + (freq_to - freq_from) * t
        phase += f / RATE
        if wave_shape == "square":
            s = _square(phase)
        elif wave_shape == "triangle":
            s = 4 * abs((phase % 1.0) - 0.5) - 1
        else:
            s = math.sin(phase * 2 * math.pi)
        env = math.exp(-decay * t) * min(1.0, i / 60)
        out.append(s * env * volume)
    return out


def noise(seconds, volume=0.4, decay=4.0, smooth=0.0, seed=1):
    rng = random.Random(seed)
    out = []
    last = 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / n
        v = rng.uniform(-1, 1)
        last = last * smooth + v * (1 - smooth)  # simple low-pass
        out.append(last * math.exp(-decay * t) * volume)
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] for p in parts if i < len(p)) for i in range(n)]


def sounds_library():
    """Every sound effect the templates use, by file name."""
    return {
        "jump.wav": tone(280, 720, 0.16, 0.25),
        "coin.wav": tone(988, 988, 0.07, 0.22, decay=0.5) + tone(1319, 1319, 0.25, 0.22, decay=4),
        "hit.wav": mix(tone(220, 70, 0.28, 0.3, decay=3), noise(0.2, 0.25, 6)),
        "stomp.wav": tone(420, 120, 0.12, 0.3, decay=2),
        "shoot.wav": tone(1100, 220, 0.12, 0.18, decay=2.5),
        "explosion.wav": noise(0.7, 0.55, 4.5, smooth=0.7, seed=7),
        "click.wav": tone(700, 500, 0.05, 0.3, "sine", decay=6),
        "buy.wav": tone(523, 523, 0.06, 0.2, decay=0.5) + tone(784, 784, 0.06, 0.2, decay=0.5) + tone(1047, 1047, 0.18, 0.2),
        "win.wav": sum((tone(f, f, 0.11, 0.22, decay=1) for f in (523, 659, 784)), []) + tone(1047, 1047, 0.45, 0.22, decay=3),
        "checkpoint.wav": tone(660, 660, 0.08, 0.22, "triangle", decay=0.5) + tone(990, 990, 0.25, 0.22, "triangle"),
        "collect.wav": tone(600, 1400, 0.18, 0.2, "triangle", decay=2),
        "lose.wav": tone(440, 440, 0.15, 0.22, decay=1) + tone(370, 370, 0.15, 0.22, decay=1) + tone(311, 200, 0.5, 0.22, decay=2),
    }


# ---------------------------------------------------------------- scenes


def T(x=0.0, y=0.0, z=0.0, rot=None, scale=None):
    t = {"position": [x, y, z]}
    if rot is not None:
        t["rotation"] = list(rot)
    if scale is not None:
        t["scale"] = list(scale)
    return t


class Scene:
    """Builds a scene file. Entities are written in the order they are added, so add parents first."""

    def __init__(self, name):
        self.name = name
        self.entities = []
        self._next = 1

    def reserve(self):
        """An id for an entity added later, so other objects can refer to it first (e.g. a camera target)."""
        eid = f"{self._next:016x}"
        self._next += 1
        return eid

    def add(self, name, components=None, parent=None, tag=None, active=True, eid=None):
        eid = eid or self.reserve()
        e = {"id": eid, "name": name}
        if tag:
            e["tag"] = tag
        if not active:
            e["active"] = False
        if parent:
            e["parent"] = parent
        comps = dict(components or {})
        comps.setdefault("Transform", T())
        e["components"] = comps
        self.entities.append(e)
        return eid

    def data(self):
        return {"aven": "scene", "version": 1, "name": self.name, "entities": self.entities}

    def prefab(self):
        """The whole builder as a prefab (the first entity is the root, placed at the origin)."""
        return {"entities": self.entities}


# ---------------------------------------------------------------- writing a template


class Template:
    def __init__(self, root, folder, name, description, style, difficulty, is3d, color, width=1280, height=720):
        self.dir = os.path.join(root, folder)
        if os.path.isdir(self.dir):
            shutil.rmtree(self.dir)
        os.makedirs(self.dir)
        self.name = name
        self.write_json("template.json", {
            "name": name,
            "description": description,
            "style": style,
            "difficulty": difficulty,
            "3d": is3d,
            "color": color.lstrip("#").upper(),
        })
        self.width = width
        self.height = height

    def path(self, rel):
        return os.path.join(self.dir, rel)

    def write_text(self, rel, text):
        p = self.path(rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", newline="\n") as f:
            f.write(text)

    def write_json(self, rel, data):
        self.write_text(rel, json.dumps(data, indent=2) + "\n")

    def project(self, start_scene="scenes/main.scene", pixel_perfect=False):
        self.write_json("project.aven", {
            "aven": "project",
            "name": self.name,
            "version": "1.0",
            "start_scene": start_scene,
            "window": {
                "width": self.width,
                "height": self.height,
                "resizable": True,
                "fullscreen": False,
                "vsync": True,
                "pixel_perfect": pixel_perfect,
            },
            "advanced_mode": False,
        })

    def scene(self, rel, scene):
        self.write_json(rel, scene.data())

    def prefab(self, rel, scene):
        self.write_json(rel, scene.prefab())

    def script(self, rel, code):
        self.write_text(rel, code.strip("\n") + "\n")

    def blocks(self, rel, variables, scripts):
        """variables: {name: value}; scripts: list of block lists (each starts with a hat)."""
        doc = {
            "aven": "blocks",
            "version": 1,
            "variables": [{"name": k, "value": v} for k, v in variables.items()],
            "scripts": [],
        }
        y = 20
        for s in scripts:
            doc["scripts"].append({"x": 20, "y": y, "blocks": s})
            y += 60 + 42 * _count_blocks(s)
        self.write_json(rel, doc)

    def sounds(self, *names):
        lib = sounds_library()
        for n in names:
            write_wav(self.path("sounds/" + n), lib[n])

    def tutorial(self, title, steps):
        self.write_json("tutorial.json", {"title": title, "steps": steps})


def _count_blocks(blocks):
    n = 0
    for b in blocks:
        n += 1
        n += _count_blocks(b.get("body", []))
        n += _count_blocks(b.get("else", []))
    return n


# ---------------------------------------------------------------- block helpers


def blk(type_, body=None, else_body=None, **inputs):
    b = {"type": type_}
    if inputs:
        b["inputs"] = inputs
    if body is not None:
        b["body"] = body
    if else_body is not None:
        b["else"] = else_body
    return b
