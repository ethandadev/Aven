"""Low-poly 3D models written as binary glTF (.glb), built from boxes, cylinders, cones and spheres.

Models stand on their origin (y = 0 is the ground), use meters, face +Z, and are flat shaded.
Each material becomes one glTF primitive; a material can carry an embedded PNG texture.
"""

import json
import math
import random
import struct


# ---------------------------------------------------------------- vectors


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


# ---------------------------------------------------------------- building meshes


class Material:
    def __init__(self, name, color, metallic=0.0, roughness=0.8, emissive=None, texture=None):
        self.name = name
        self.color = color  # (r, g, b) 0-1, linear
        self.metallic = metallic
        self.roughness = roughness
        self.emissive = emissive
        self.texture = texture  # PNG bytes, or None


def srgb(hex_color):
    """A #rrggbb color as linear floats, which glTF expects for base colors."""
    h = hex_color.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return tuple(out)


class Model:
    def __init__(self):
        self.parts = {}  # material -> list of triangles ((p0, p1, p2), (uv0, uv1, uv2))

    def tris(self, material, triangles, center):
        """Adds triangles of a convex part, turning each to face away from `center`."""
        out = self.parts.setdefault(material, [])
        for (p, uv) in triangles:
            n = cross(sub(p[1], p[0]), sub(p[2], p[0]))
            mid = tuple((p[0][i] + p[1][i] + p[2][i]) / 3 for i in range(3))
            if dot(n, sub(mid, center)) < 0:
                p = (p[0], p[2], p[1])
                uv = (uv[0], uv[2], uv[1])
            out.append((p, uv))

    # -- shapes; `at` is the bottom center unless said otherwise

    def box(self, material, at, size, rot_y=0.0):
        cx, cy, cz = at
        w, h, d = size[0] / 2, size[1], size[2] / 2
        c, s = math.cos(rot_y), math.sin(rot_y)

        def P(x, y, z):
            return (cx + x * c + z * s, cy + y, cz - x * s + z * c)

        corners = [P(x, y, z) for y in (0, h) for z in (-d, d) for x in (-w, w)]
        faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        tris = []
        for a, b, cc, dd in faces:
            q = [corners[a], corners[b], corners[cc], corners[dd]]
            uv = [(0, 1), (1, 1), (1, 0), (0, 0)]
            tris.append(((q[0], q[1], q[2]), (uv[0], uv[1], uv[2])))
            tris.append(((q[0], q[2], q[3]), (uv[0], uv[2], uv[3])))
        self.tris(material, tris, (cx, cy + h / 2, cz))

    def cylinder(self, material, at, radius, height, segments=12, top_radius=None, caps=True):
        cx, cy, cz = at
        top = radius if top_radius is None else top_radius
        ring = lambda r, y: [(cx + r * math.cos(2 * math.pi * i / segments), cy + y,
                              cz + r * math.sin(2 * math.pi * i / segments)) for i in range(segments)]
        bottom_ring, top_ring = ring(radius, 0), ring(top, height)
        tris = []
        for i in range(segments):
            j = (i + 1) % segments
            u0, u1 = i / segments, (i + 1) / segments
            a, b, c, d = bottom_ring[i], bottom_ring[j], top_ring[j], top_ring[i]
            tris.append(((a, b, c), ((u0, 1), (u1, 1), (u1, 0))))
            if top > 1e-6:
                tris.append(((a, c, d), ((u0, 1), (u1, 0), (u0, 0))))
        if caps:
            bc, tc = (cx, cy, cz), (cx, cy + height, cz)
            for i in range(segments):
                j = (i + 1) % segments
                tris.append(((bc, bottom_ring[j], bottom_ring[i]), ((0.5, 0.5), (0, 0), (1, 0))))
                if top > 1e-6:
                    tris.append(((tc, top_ring[i], top_ring[j]), ((0.5, 0.5), (0, 0), (1, 0))))
        self.tris(material, tris, (cx, cy + height / 2, cz))

    def cone(self, material, at, radius, height, segments=10):
        self.cylinder(material, at, radius, height, segments, top_radius=0.0)

    def sphere(self, material, center, radius, detail=1, jitter=0.0, seed=0, squash=(1, 1, 1)):
        """A flat-shaded icosphere around `center`; `jitter` roughens it (rocks, bushes)."""
        t = (1 + math.sqrt(5)) / 2
        verts = [norm(v) for v in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                   (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
        faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
                 (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
                 (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        for _ in range(detail):
            cache, new = {}, []

            def mid(a, b):
                key = (min(a, b), max(a, b))
                if key not in cache:
                    verts.append(norm(add(verts[a], verts[b])))
                    cache[key] = len(verts) - 1
                return cache[key]

            for a, b, c in faces:
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
                new += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
            faces = new
        rng = random.Random(seed)
        r = [radius * (1 + rng.uniform(-jitter, jitter)) for _ in verts]
        pts = [(center[0] + v[0] * r[i] * squash[0], center[1] + v[1] * r[i] * squash[1],
                center[2] + v[2] * r[i] * squash[2]) for i, v in enumerate(verts)]
        tris = []
        for a, b, c in faces:
            uv = lambda v: (0.5 + math.atan2(v[2], v[0]) / (2 * math.pi), 0.5 - math.asin(max(-1, min(1, v[1]))) / math.pi)
            tris.append(((pts[a], pts[b], pts[c]), (uv(verts[a]), uv(verts[b]), uv(verts[c]))))
        self.tris(material, tris, center)

    def prism_roof(self, material, at, width, depth, height, overhang=0.15):
        """A gable roof: a triangular prism along X, sitting on `at`."""
        cx, cy, cz = at
        w, d = width / 2 + overhang, depth / 2 + overhang
        a, b = (cx - w, cy, cz - d), (cx - w, cy, cz + d)
        c, dd = (cx + w, cy, cz - d), (cx + w, cy, cz + d)
        e, f = (cx - w, cy + height, cz), (cx + w, cy + height, cz)
        uv = ((0, 1), (1, 1), (0.5, 0))
        tris = [((a, b, e), uv), ((c, f, dd), uv),
                ((a, e, f), ((0, 1), (0, 0), (1, 0))), ((a, f, c), ((0, 1), (1, 0), (1, 1))),
                ((b, dd, f), ((0, 1), (1, 1), (1, 0))), ((b, f, e), ((0, 1), (1, 0), (0, 0))),
                ((a, c, dd), ((0, 0), (1, 0), (1, 1))), ((a, dd, b), ((0, 0), (1, 1), (0, 1)))]
        self.tris(material, tris, (cx, cy + height / 3, cz))

    def rotated(self, angle_z, pivot):
        """Rotates everything added so far around the Z axis (e.g. to stand a coin up)."""
        c, s = math.cos(angle_z), math.sin(angle_z)

        def R(p):
            x, y = p[0] - pivot[0], p[1] - pivot[1]
            return (pivot[0] + x * c - y * s, pivot[1] + x * s + y * c, p[2])

        for mat, tris in self.parts.items():
            self.parts[mat] = [((R(p[0]), R(p[1]), R(p[2])), uv) for p, uv in tris]

    def rotated_x(self, angle, pivot):
        c, s = math.cos(angle), math.sin(angle)

        def R(p):
            y, z = p[1] - pivot[1], p[2] - pivot[2]
            return (p[0], pivot[1] + y * c - z * s, pivot[2] + y * s + z * c)

        for mat, tris in self.parts.items():
            self.parts[mat] = [((R(p[0]), R(p[1]), R(p[2])), uv) for p, uv in tris]

    # ---------------------------------------------------------------- writing

    def glb(self):
        bin_data = bytearray()
        views, accessors, primitives, materials, images, textures = [], [], [], [], [], []

        def view(data, target=None):
            while len(bin_data) % 4:
                bin_data.append(0)
            v = {"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(data)}
            if target:
                v["target"] = target
            bin_data.extend(data)
            views.append(v)
            return len(views) - 1

        for mi, (mat, tris) in enumerate(self.parts.items()):
            positions, normals, uvs = [], [], []
            for p, uv in tris:
                n = norm(cross(sub(p[1], p[0]), sub(p[2], p[0])))
                for k in range(3):
                    positions.append(p[k])
                    normals.append(n)
                    uvs.append(uv[k])
            count = len(positions)
            pos_bytes = b"".join(struct.pack("<3f", *v) for v in positions)
            nrm_bytes = b"".join(struct.pack("<3f", *v) for v in normals)
            uv_bytes = b"".join(struct.pack("<2f", *v) for v in uvs)
            idx_bytes = b"".join(struct.pack("<H" if count < 65536 else "<I", i) for i in range(count))
            lo = [min(v[i] for v in positions) for i in range(3)]
            hi = [max(v[i] for v in positions) for i in range(3)]
            base = len(accessors)
            accessors.append({"bufferView": view(pos_bytes, 34962), "componentType": 5126, "count": count,
                              "type": "VEC3", "min": lo, "max": hi})
            accessors.append({"bufferView": view(nrm_bytes, 34962), "componentType": 5126, "count": count, "type": "VEC3"})
            accessors.append({"bufferView": view(uv_bytes, 34962), "componentType": 5126, "count": count, "type": "VEC2"})
            accessors.append({"bufferView": view(idx_bytes, 34963), "componentType": 5123 if count < 65536 else 5125,
                              "count": count, "type": "SCALAR"})
            primitives.append({"attributes": {"POSITION": base, "NORMAL": base + 1, "TEXCOORD_0": base + 2},
                               "indices": base + 3, "material": mi})
            m = {"name": mat.name, "pbrMetallicRoughness": {"baseColorFactor": list(mat.color) + [1.0],
                                                            "metallicFactor": mat.metallic,
                                                            "roughnessFactor": mat.roughness}}
            if mat.emissive:
                m["emissiveFactor"] = list(mat.emissive)
            if mat.texture:
                images.append({"bufferView": view(mat.texture), "mimeType": "image/png"})
                textures.append({"source": len(images) - 1, "sampler": 0})
                m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(textures) - 1}
            materials.append(m)

        while len(bin_data) % 4:
            bin_data.append(0)
        doc = {
            "asset": {"version": "2.0", "generator": "Rynax asset library (tools/library)"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": primitives}],
            "materials": materials,
            "accessors": accessors,
            "bufferViews": views,
            "buffers": [{"byteLength": len(bin_data)}],
        }
        if images:
            doc["images"] = images
            doc["textures"] = textures
            doc["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        js = json.dumps(doc, separators=(",", ":")).encode()
        while len(js) % 4:
            js += b" "
        total = 12 + 8 + len(js) + 8 + len(bin_data)
        out = struct.pack("<III", 0x46546C67, 2, total)
        out += struct.pack("<II", len(js), 0x4E4F534A) + js
        out += struct.pack("<II", len(bin_data), 0x004E4942) + bytes(bin_data)
        return out


# ---------------------------------------------------------------- the models


def M(name, hex_color, **kw):
    return Material(name, srgb(hex_color), **kw)


def tree_round(seed=1):
    m = Model()
    trunk, leaves = M("Bark", "#6b4a2f", roughness=0.9), M("Leaves", "#4f9a35", roughness=0.8)
    m.cylinder(trunk, (0, 0, 0), 0.18, 1.6, 7, top_radius=0.13)
    m.sphere(leaves, (0, 2.0, 0), 0.9, 1, 0.12, seed)
    m.sphere(leaves, (0.45, 1.75, 0.2), 0.6, 1, 0.12, seed + 1)
    m.sphere(leaves, (-0.4, 1.8, -0.25), 0.62, 1, 0.12, seed + 2)
    return m


def tree_pine():
    m = Model()
    trunk, needles = M("Bark", "#5a3d27", roughness=0.9), M("Needles", "#2d6a3a", roughness=0.85)
    m.cylinder(trunk, (0, 0, 0), 0.15, 0.8, 6)
    for i, (r, h) in enumerate([(1.0, 1.4), (0.8, 1.2), (0.55, 1.0)]):
        m.cone(needles, (0, 0.6 + i * 0.75, 0), r, h, 9)
    return m


def rock(seed, size=1.0):
    m = Model()
    stone = M("Stone", "#7d8084", roughness=0.9)
    m.sphere(stone, (0, 0.35 * size, 0), 0.6 * size, 1, 0.25, seed, squash=(1.2, 0.7, 1.0))
    return m


def bush():
    m = Model()
    leaves = M("Leaves", "#3f8a33", roughness=0.85)
    for i, (x, z, r) in enumerate([(0, 0, 0.55), (0.45, 0.1, 0.4), (-0.4, -0.1, 0.42), (0.1, -0.35, 0.38)]):
        m.sphere(leaves, (x, r * 0.8, z), r, 1, 0.15, 30 + i)
    return m


def crate(texture):
    m = Model()
    m.box(Material("Crate", (1, 1, 1), roughness=0.8, texture=texture), (0, 0, 0), (1, 1, 1))
    return m


def barrel():
    m = Model()
    wood, band = M("Wood", "#8a5a33", roughness=0.8), M("Iron", "#3b3f45", metallic=1.0, roughness=0.45)
    m.cylinder(wood, (0, 0, 0), 0.38, 0.5, 14, top_radius=0.44)
    m.cylinder(wood, (0, 0.5, 0), 0.44, 0.5, 14, top_radius=0.38)
    for y in (0.12, 0.84):
        m.cylinder(band, (0, y, 0), 0.43, 0.06, 14)
    return m


def fence():
    m = Model()
    wood = M("Wood", "#9b6b3e", roughness=0.85)
    for x in (-0.9, 0.9):
        m.box(wood, (x, 0, 0), (0.14, 1.0, 0.14))
    for y in (0.35, 0.7):
        m.box(wood, (0, y, 0), (2.0, 0.12, 0.06))
    return m


def lamp_post():
    m = Model()
    iron = M("Iron", "#2b2f36", metallic=1.0, roughness=0.5)
    glow = M("Light", "#fff1c1", roughness=0.3, emissive=(1.0, 0.85, 0.45))
    m.cylinder(iron, (0, 0, 0), 0.12, 0.2, 8)
    m.cylinder(iron, (0, 0.2, 0), 0.05, 2.6, 8)
    m.box(iron, (0, 2.8, 0), (0.34, 0.06, 0.34))
    m.box(glow, (0, 2.86, 0), (0.24, 0.3, 0.24))
    m.cone(iron, (0, 3.16, 0), 0.26, 0.18, 4)
    return m


def coin():
    m = Model()
    # Fully metallic surfaces need something to reflect; halfway reads as gold under any sky.
    gold = M("Gold", "#ffcc33", metallic=0.45, roughness=0.3, emissive=(0.08, 0.05, 0.0))
    m.cylinder(gold, (0, 0, 0), 0.35, 0.08, 16)
    m.rotated_x(math.pi / 2, (0, 0.04, 0))
    # Lift it so it stands on the ground.
    m.parts = {k: [(tuple((p[0], p[1] + 0.35, p[2]) for p in ps), uv) for ps, uv in v] for k, v in m.parts.items()}
    return m


def mushroom():
    m = Model()
    stem, cap, spot = M("Stem", "#efe6d2"), M("Cap", "#d6362f", roughness=0.6), M("Spots", "#ffffff")
    m.cylinder(stem, (0, 0, 0), 0.12, 0.35, 8, top_radius=0.1)
    m.sphere(cap, (0, 0.35, 0), 0.32, 1, 0.0, 0, squash=(1, 0.6, 1))
    for i in range(5):
        a = i * 2 * math.pi / 5
        m.sphere(spot, (0.22 * math.cos(a), 0.47, 0.22 * math.sin(a)), 0.05, 0)
    return m


def house():
    m = Model()
    wall, roof = M("Walls", "#e8dcc4", roughness=0.9), M("Roof", "#a8452f", roughness=0.8)
    wood, glass = M("Door", "#6b4428", roughness=0.8), M("Windows", "#9fd3ff", roughness=0.1, emissive=(0.15, 0.2, 0.25))
    m.box(wall, (0, 0, 0), (3.0, 2.2, 2.4))
    m.prism_roof(roof, (0, 2.2, 0), 3.0, 2.4, 1.2)
    m.box(wood, (0, 0, 1.21), (0.7, 1.4, 0.06))
    for x in (-0.95, 0.95):
        m.box(glass, (x, 1.0, 1.21), (0.6, 0.6, 0.05))
    m.box(M("Chimney", "#7a7470"), (0.8, 2.6, -0.4), (0.35, 1.0, 0.35))
    return m


def bench():
    m = Model()
    wood, iron = M("Wood", "#9b6b3e", roughness=0.8), M("Iron", "#2b2f36", metallic=1.0, roughness=0.5)
    for x in (-0.7, 0.7):
        m.box(iron, (x, 0, 0), (0.06, 0.45, 0.45))
        m.box(iron, (x, 0.45, -0.2), (0.06, 0.45, 0.06))
    for z in (-0.12, 0.0, 0.12):
        m.box(wood, (0, 0.45, z), (1.6, 0.05, 0.1))
    for y in (0.62, 0.78):
        m.box(wood, (0, y, -0.2), (1.6, 0.1, 0.04))
    return m


def chest():
    m = Model()
    wood, gold = M("Wood", "#a8683a", roughness=0.8), M("Gold", "#ffcc33", metallic=0.45, roughness=0.35)
    m.box(wood, (0, 0, 0), (1.0, 0.5, 0.65))
    m.box(M("Lid", "#8e5530", roughness=0.8), (0, 0.5, 0), (1.02, 0.25, 0.67))
    for x in (-0.42, 0.42):
        m.box(gold, (x, -0.005, 0), (0.08, 0.785, 0.7))
    m.box(gold, (0, 0.4, 0.345), (0.14, 0.18, 0.03))
    return m


def flower(color="#f25f8a"):
    m = Model()
    stem, petal, middle = M("Stem", "#3c8a2e"), M("Petals", color, roughness=0.6), M("Middle", "#f7d13b")
    m.cylinder(stem, (0, 0, 0), 0.02, 0.4, 5)
    for i in range(6):
        a = i * math.pi / 3
        m.sphere(petal, (0.07 * math.cos(a), 0.42, 0.07 * math.sin(a)), 0.05, 0, squash=(1.3, 0.4, 1.3))
    m.sphere(middle, (0, 0.44, 0), 0.04, 0)
    return m


def grass_tuft():
    m = Model()
    blade = M("Grass", "#5aa33b", roughness=0.9)
    rng = random.Random(7)
    for i in range(7):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0, 0.12)
        m.cone(blade, (r * math.cos(a), 0, r * math.sin(a)), 0.03, rng.uniform(0.25, 0.45), 4)
    return m


def platform(top_hex, side_hex, name):
    """A 2 x 0.5 x 2 block with a colored top, for platforms and obby courses."""
    m = Model()
    side, top = M("Sides", side_hex, roughness=0.9), M("Top", top_hex, roughness=0.85)
    m.box(side, (0, 0, 0), (2.0, 0.4, 2.0))
    m.box(top, (0, 0.4, 0), (2.02, 0.1, 2.02))
    return m


def robot():
    """A blocky stand-in character (1.8 m tall), facing +Z."""
    m = Model()
    body, dark = M("Body", "#4f86d9", metallic=0.3, roughness=0.5), M("Joints", "#2b2f36", metallic=0.6, roughness=0.5)
    eyes = M("Eyes", "#bff4ff", emissive=(0.4, 0.9, 1.0))
    for x in (-0.15, 0.15):
        m.box(dark, (x, 0, 0), (0.18, 0.8, 0.22))
    m.box(body, (0, 0.8, 0), (0.62, 0.6, 0.36))
    for x in (-0.4, 0.4):
        m.box(dark, (x, 0.75, 0), (0.14, 0.6, 0.16))
    m.box(body, (0, 1.42, 0), (0.44, 0.38, 0.38))
    for x in (-0.1, 0.1):
        m.box(eyes, (x, 1.56, 0.19), (0.09, 0.07, 0.02))
    m.cylinder(dark, (0, 1.8, 0), 0.02, 0.15, 5)
    return m


def models(crate_texture):
    return [
        # (file stem, label, model, tags)
        ("tree", "Tree", tree_round(1), "nature forest"),
        ("tree_pine", "Pine tree", tree_pine(), "nature forest winter"),
        ("rock", "Rock", rock(3, 1.0), "nature stone"),
        ("rock_small", "Small rock", rock(9, 0.5), "nature stone"),
        ("bush", "Bush", bush(), "nature garden"),
        ("grass_tuft", "Grass tuft", grass_tuft(), "nature ground detail"),
        ("flower", "Flower", flower(), "nature garden"),
        ("mushroom", "Mushroom", mushroom(), "nature forest"),
        ("crate", "Crate", crate(crate_texture), "props wood"),
        ("barrel", "Barrel", barrel(), "props wood"),
        ("chest", "Treasure chest", chest(), "props treasure"),
        ("fence", "Fence", fence(), "props farm"),
        ("bench", "Bench", bench(), "props park city"),
        ("lamp_post", "Lamp post", lamp_post(), "props city light"),
        ("house", "House", house(), "buildings village"),
        ("coin", "Coin", coin(), "pickups items"),
        ("platform_grass", "Grass platform", platform("#5aa33b", "#7a5236", "grass"), "level platform obby"),
        ("platform_stone", "Stone platform", platform("#9a9ca0", "#6d6f73", "stone"), "level platform obby"),
        ("robot", "Robot (stand-in character)", robot(), "character player"),
    ]
