"""Builds Rynax's asset library (editor/data/library): sprites, animations, backgrounds, 3D textures
and 3D models, all made here from code, so everything is original and free to use (CC0).

    python3 tools/library/generate.py [--thumbnails build/bin/rynax-player]

--thumbnails renders a picture of every 3D model with the player (otherwise the old pictures are
kept). The editor's Asset Library window lists what library.json describes.
"""

import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "templates"))

from common import write_png  # noqa: E402
import models  # noqa: E402
import sprites  # noqa: E402
import textures  # noqa: E402

OUT = os.path.normpath(os.path.join(HERE, "..", "..", "editor", "data", "library"))


def png_bytes(rows):
    fd, tmp = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    write_png(tmp, len(rows[0]), len(rows), rows)
    with open(tmp, "rb") as f:
        data = f.read()
    os.remove(tmp)
    return data


def crate_texture():
    """Planks with a dark frame and a diagonal brace, for the crate model."""
    base = textures.planks(128, 3)
    for y in range(128):
        for x in range(128):
            border = x < 10 or y < 10 or x > 117 or y > 117
            brace = abs(x - y) < 7 and 10 <= x <= 117
            if border or brace:
                r, g, b, a = base[y][x]
                base[y][x] = (int(r * 0.55), int(g * 0.5), int(b * 0.45), 255)
    return png_bytes(base)


def glb_bounds(data):
    js_len = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + js_len])
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for acc in doc["accessors"]:
        if "min" in acc:
            lo = [min(lo[i], acc["min"][i]) for i in range(3)]
            hi = [max(hi[i], acc["max"][i]) for i in range(3)]
    return lo, hi


def render_thumbnail(player, glb_path, out_png):
    """A 256x256 picture of a model, rendered by the engine itself."""
    with open(glb_path, "rb") as f:
        lo, hi = glb_bounds(f.read())
    center = [(lo[i] + hi[i]) / 2 for i in range(3)]
    radius = max(0.3, math.dist(lo, hi) / 2)
    yaw, pitch = math.radians(35), math.radians(22)
    dist = radius / math.tan(math.radians(22)) * 1.05
    cam = [center[0] + math.sin(yaw) * math.cos(pitch) * dist, center[1] + math.sin(pitch) * dist,
           center[2] + math.cos(yaw) * math.cos(pitch) * dist]
    with tempfile.TemporaryDirectory() as proj:
        os.makedirs(os.path.join(proj, "scenes"))
        os.makedirs(os.path.join(proj, "models"))
        shutil.copy(glb_path, os.path.join(proj, "models", "m.glb"))
        with open(os.path.join(proj, "project.rynax"), "w") as f:
            json.dump({"rynax": "project", "name": "thumb", "start_scene": "scenes/main.scene",
                       "window": {"width": 256, "height": 256}}, f)

        def e(i, name, comps):
            return {"id": f"{i:016x}", "name": name, "components": comps}

        scene = {"rynax": "scene", "version": 1, "name": "Thumb", "entities": [
            e(1, "Camera", {"Transform": {"position": cam, "rotation": [-22, 35, 0]},
                            "Camera": {"projection": "Perspective", "field_of_view": 44},
                            "PostProcessing": {"tonemapper": "ACES", "ssao": True, "fxaa": True}}),
            e(2, "Environment", {"Environment": {"sky": "Procedural", "sky_top": [0.55, 0.63, 0.75, 1],
                                                 "sky_horizon": [0.86, 0.88, 0.92, 1]}}),
            e(3, "Sun", {"Transform": {"position": [0, 10, 0], "rotation": [-50, 30, 0]},
                         "Light": {"type": "Directional", "intensity": 1.4, "cast_shadows": True}}),
            e(4, "Ground", {"Transform": {"position": [center[0], lo[1] - 0.001, center[2]], "scale": [30, 1, 30]},
                            "MeshRenderer": {"mesh": "Plane", "color": [0.8, 0.82, 0.85, 1], "roughness": 0.9}}),
            e(5, "Model", {"MeshRenderer": {"mesh": "Model", "model": "models/m.glb"}}),
        ]}
        with open(os.path.join(proj, "scenes", "main.scene"), "w") as f:
            json.dump(scene, f)
        subprocess.run([player, proj, "--screenshot", out_png, "--frames", "6", "--size", "256x256", "--hidden"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    player = None
    if "--thumbnails" in sys.argv:
        player = os.path.abspath(sys.argv[sys.argv.index("--thumbnails") + 1])
    old_thumbs = os.path.join(OUT, "thumbs")
    keep = None
    if not player and os.path.isdir(old_thumbs):
        keep = tempfile.mkdtemp()
        shutil.copytree(old_thumbs, os.path.join(keep, "thumbs"))
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    items = []

    def item(kind, stem, name, file, dest, tags, **extra):
        d = {"id": f"{kind}/{stem}", "name": name, "kind": kind, "file": file, "dest": dest, "tags": tags}
        d.update(extra)
        items.append(d)

    for stem, name, a, pal, tags in sprites.SPRITES:
        rel = f"2d/sprites/{stem}.png"
        write_png(os.path.join(OUT, rel), 16, 16, sprites.to_pixels(a, pal))
        item("sprite", stem, name, rel, f"images/{stem}.png", tags)
    for stem, name, frames_fn, pal, tags in sprites.SHEETS:
        frames = frames_fn()
        rel = f"2d/animations/{stem}.png"
        write_png(os.path.join(OUT, rel), 16 * len(frames), 16, sprites.sheet(frames, pal))
        item("animation", stem, name, rel, f"images/{stem}.png", tags, frames=len(frames))
    for stem, name, fn, tags in sprites.BACKGROUNDS:
        rel = f"2d/backgrounds/{stem}.png"
        write_png(os.path.join(OUT, rel), sprites.W, sprites.H, fn())
        item("background", stem, name, rel, f"images/backgrounds/{stem}.png", tags)
        print("  background", stem)
    for stem, name, fn, rough, metal, tags in textures.TEXTURES:
        rows = fn()
        rel = f"3d/textures/{stem}.png"
        write_png(os.path.join(OUT, rel), len(rows[0]), len(rows), rows)
        item("texture", stem, name, rel, f"textures/{stem}.png", tags, roughness=rough, metallic=metal)
        print("  texture", stem)
    for stem, name, model, tags in models.models(crate_texture()):
        rel = f"3d/models/{stem}.glb"
        path = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(model.glb())
        thumb = f"thumbs/{stem}.png"
        if player:
            os.makedirs(os.path.join(OUT, "thumbs"), exist_ok=True)
            render_thumbnail(player, path, os.path.join(OUT, thumb))
        elif keep and os.path.exists(os.path.join(keep, thumb)):
            os.makedirs(os.path.join(OUT, "thumbs"), exist_ok=True)
            shutil.copy(os.path.join(keep, thumb), os.path.join(OUT, thumb))
        item("model", stem, name, rel, f"models/{stem}.glb", tags,
             thumb=thumb if os.path.exists(os.path.join(OUT, thumb)) else "")
        print("  model", stem)
    if keep:
        shutil.rmtree(keep)

    with open(os.path.join(OUT, "library.json"), "w", newline="\n") as f:
        json.dump({"about": "Rynax's asset library. Made by tools/library/generate.py; free to use (CC0).",
                   "items": items}, f, indent=1)
        f.write("\n")
    with open(os.path.join(OUT, "LICENSE.txt"), "w", newline="\n") as f:
        f.write("Everything in this folder was made from code by tools/library/generate.py for Rynax.\n"
                "It is dedicated to the public domain (CC0 1.0): use it in any game, commercial or not,\n"
                "with no need to give credit. https://creativecommons.org/publicdomain/zero/1.0/\n")
    print(f"{len(items)} items in {OUT}")


if __name__ == "__main__":
    main()
