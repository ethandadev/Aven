"""3D starter templates: Blank 3D, 3D Obby and Crystal Forest (first-person explorer)."""

import math
import random

from common import T, Scene, Template, rgba


def cube(color, scale, pos, rot=None, collider=True, trigger=False, emission=None, roughness=0.6, metallic=0.0):
    comps = {
        "Transform": T(*pos, rot=rot, scale=scale),
        "MeshRenderer": {"mesh": "Cube", "color": rgba(color), "roughness": roughness, "metallic": metallic},
    }
    if emission:
        comps["MeshRenderer"]["emission"] = rgba(emission)
        comps["MeshRenderer"]["emission_strength"] = 2.5
    if collider:
        comps["BoxCollider"] = {"size": [1, 1, 1], "is_trigger": trigger}
    return comps


def base_3d_scene(s, camera_pos, camera_rot, sky_top="#3a78c9", horizon="#cfe3f5", fog=False, fog_color="#cfe3f5",
                  fog_density=0.01, sun_rot=(-50, -30, 0), sun_color="#fff5e0", sun_intensity=1.2, camera_extra=None,
                  bloom=0.6):
    cam = {
        "Transform": T(*camera_pos, rot=camera_rot),
        "Camera": {"projection": "Perspective", "field_of_view": 60},
        "PostProcessing": {"tonemapper": "ACES", "bloom": True, "bloom_intensity": bloom, "ssao": True, "fxaa": True,
                           "vignette": True, "vignette_intensity": 0.25},
        "AudioListener": {},
    }
    cam.update(camera_extra or {})
    camera = s.add("Camera", cam)
    s.add("Environment", {"Environment": {"sky": "Procedural", "sky_top": rgba(sky_top), "sky_horizon": rgba(horizon),
                                          "fog": fog, "fog_color": rgba(fog_color), "fog_density": fog_density}})
    s.add("Sun", {"Transform": T(0, 10, 0, rot=sun_rot),
                  "Light": {"type": "Directional", "color": rgba(sun_color), "intensity": sun_intensity, "cast_shadows": True}})
    return camera


# ---------------------------------------------------------------- Blank 3D


def blank_3d(root):
    t = Template(root, "blank-3d", "Blank 3D",
                 "An empty 3D world with a sky, sunlight and shadows. Add shapes, a player and scripts to build your own game.",
                 "Blocks or EasyScript", "Beginner", True, "#0EA5E9")
    t.project()
    s = Scene("Main")
    base_3d_scene(s, (0, 3, 8), (-15, 0, 0))
    s.add("Ground", {"Transform": T(0, 0, 0, scale=(30, 1, 30)),
                     "MeshRenderer": {"mesh": "Plane", "color": rgba("#7a9a6a")},
                     "BoxCollider": {"size": [1, 0.02, 1]}})
    s.add("Cube", {"Transform": T(0, 0.5, 0, rot=(0, 30, 0)),
                   "MeshRenderer": {"mesh": "Cube", "color": rgba("#3b82f6"), "roughness": 0.4},
                   "Script": {"path": "scripts/spin.es"}})
    t.scene("scenes/main.scene", s)
    t.script("scripts/spin.es", '''
# Spins the object around. Try changing the speed in the Inspector!

speed = 45  # degrees per second

def on_update(dt):
    self.rotation_y += speed * dt
''')
    for folder in ("images", "sounds", "prefabs", "models"):
        t.write_text(f"{folder}/.keep", "")
    t.tutorial("Your first 3D game", [
        {"title": "Welcome to 3D!",
         "text": "Hold the right mouse button in the Scene view and use WASD to fly around. Alt + drag orbits, the "
                 "scroll wheel zooms, and F focuses on the selected object."},
        {"title": "Add a player",
         "text": "Open Create > Player 3D. It comes with a CharacterController, so it already walks with WASD and "
                 "jumps with Space. Press Play to try it."},
        {"title": "Make the camera follow",
         "text": "Select the Camera, click '+ Add Component' and add CameraFollow. Drag the Player from the Hierarchy "
                 "onto its Target field, set Offset to (0, 4, 7) and tick Look At Target."},
        {"title": "Build a level",
         "text": "Create > 3D Shape > Cube adds a box. Use the gizmo to move it (W), rotate it (E) and scale it (R). "
                 "Add a BoxCollider so the player can stand on it."},
        {"title": "Script it",
         "text": "The spinning cube uses spin.es. Open it to see how rotation_y changes every frame.",
         "open": "scripts/spin.es", "select": "Cube"},
    ])


# ---------------------------------------------------------------- 3D Obby


def obby_3d(root):
    t = Template(root, "obby-3d", "3D Obby",
                 "A classic obstacle course: jump across platforms, avoid the glowing lava, ride moving platforms and "
                 "reach the trophy. Checkpoints save your progress.",
                 "EasyScript", "Beginner", True, "#EF4444")
    t.project()
    t.sounds("hit.wav", "checkpoint.wav", "win.wav", "jump.wav")

    s = Scene("Obby")
    player_id = s.reserve()
    base_3d_scene(s, (0, 5, 8), (-20, 0, 0), sky_top="#2f6fd6", horizon="#a9d2f5", fog=True, fog_color="#a9d2f5",
                  fog_density=0.004, bloom=0.8, camera_extra={
        "CameraFollow": {"target": player_id, "offset": [0, 4.5, 7.5], "smoothness": 6, "follow_z": True,
                         "look_at_target": True}})

    gray, blue, pink = "#cbd5e1", "#60a5fa", "#f472b6"
    # Spawn.
    s.add("Spawn", cube("#5cb85c", (8, 1, 8), (0, -0.5, 0)))
    # Stage 1: jumps.
    for i, z in enumerate([-7, -11, -15]):
        s.add("Jump", cube([blue, pink, blue][i], (2.5, 0.5, 2.5), (0, -0.25, z)))
    # Checkpoint 1 and the lava run.
    s.add("Platform", cube(gray, (5, 0.5, 5), (0, -0.25, -21)))
    s.add("Lava Run", cube(gray, (4, 0.5, 14), (0, -0.25, -31)))
    for z in [-27, -31, -35]:
        s.add("Lava", cube("#dc2626", (4, 0.3, 1), (0, 0.1, z), trigger=True, emission="#ff3b1f"), tag="lava")
    # Checkpoint 2 and the moving platforms.
    s.add("Platform", cube(gray, (5, 0.5, 5), (0, -0.25, -42)))
    for z, dist, spd in [(-48.5, 2.5, 1.0), (-54, 2.5, 1.3)]:
        comps = cube("#a78bfa", (2.6, 0.5, 2.6), (0, -0.25, z))
        comps["RigidBody"] = {"type": "Kinematic"}
        comps["Script"] = {"path": "scripts/mover.es"}
        s.add("Moving Platform", comps)
    # The spinner: a round platform with a sweeping lava bar.
    s.add("Spinner Base", {"Transform": T(0, -0.25, -62, scale=(7, 0.5, 7)),
                           "MeshRenderer": {"mesh": "Cylinder", "color": rgba(gray)},
                           "BoxCollider": {"size": [0.9, 1, 0.9]}})
    bar = cube("#dc2626", (6.5, 0.4, 0.4), (0, 0.45, -62), trigger=True, emission="#ff3b1f")
    bar["RigidBody"] = {"type": "Kinematic"}
    bar["Script"] = {"path": "scripts/spinner.es"}
    s.add("Spinning Lava", bar, tag="lava")
    # Finish.
    s.add("Finish Platform", cube("#fbbf24", (6, 0.5, 6), (0, -0.25, -71), metallic=0.6, roughness=0.35))
    s.add("Trophy", {"Transform": T(0, 1.6, -72.5, rot=(90, 0, 0), scale=(1.2, 1.2, 1.2)),
                     "MeshRenderer": {"mesh": "Torus", "color": rgba("#fbbf24"), "metallic": 1, "roughness": 0.25,
                                      "emission": rgba("#f59e0b"), "emission_strength": 0.6},
                     "Script": {"path": "scripts/spinner.es"}})
    s.add("Finish", {"Transform": T(0, 0.6, -71), "BoxCollider": {"size": [5, 1.2, 5], "is_trigger": True},
                     "ParticleEmitter": {"emitting": False, "rate": 0, "lifetime": 2.5, "speed": 8, "spread": 40,
                                         "gravity": [0, -6, 0], "start_color": rgba("#fde047"), "end_color": rgba("#ec4899", 0),
                                         "start_size": 0.25, "end_size": 0.1, "additive": False},
                     "Script": {"path": "scripts/finish.es"}})

    for n, z in [(1, -21), (2, -42)]:
        s.add(f"Checkpoint {n}", {"Transform": T(0, 0.05, z, scale=(1.6, 0.1, 1.6)),
                                  "MeshRenderer": {"mesh": "Cylinder", "color": rgba("#166534"),
                                                   "emission": rgba("#22c55e"), "emission_strength": 0.3},
                                  "BoxCollider": {"size": [1, 12, 1], "is_trigger": True},
                                  "Script": {"path": "scripts/checkpoint.es"}}, tag="checkpoint")

    player = s.add("Player", {"Transform": T(0, 1, 0, scale=(0.8, 0.9, 0.8)),
                              "MeshRenderer": {"mesh": "Capsule", "color": rgba("#3b82f6"), "roughness": 0.4},
                              "CharacterController": {"speed": 6, "jump_height": 2.2, "gravity": 22},
                              "Script": {"path": "scripts/player.es"}}, tag="player", eid=player_id)
    for x in (-0.18, 0.18):
        s.add("Eye", {"Transform": T(x, 0.45, -0.42, scale=(0.14, 0.18, 0.08)),
                      "MeshRenderer": {"mesh": "Sphere", "color": rgba("#111827"), "roughness": 0.2}}, parent=player)

    s.add("HUD", {"UIElement": {"anchor": "TopLeft", "offset": [24, -20], "size": [700, 50]},
                  "UIText": {"text": "Time: 0.0", "font_size": 36, "align": "Left"},
                  "Script": {"path": "scripts/hud.es"}})
    s.add("Win", {"UIElement": {"anchor": "Center", "offset": [0, 120], "size": [1000, 110]},
                  "UIText": {"text": "You win!", "font_size": 84, "color": rgba("#fde047")},
                  "Script": {"path": "scripts/win.es"}})
    s.add("Help", {"UIElement": {"anchor": "Bottom", "offset": [0, 16], "size": [1000, 40]},
                   "UIText": {"text": "WASD or arrow keys to move, Space to jump. Don't touch the lava!", "font_size": 26},
                   "Script": {"path": "scripts/fade_out.es"}})
    t.scene("scenes/main.scene", s)

    t.script("scripts/player.es", '''
# Walking and jumping come from the CharacterController component, so this
# script only handles falling, lava and checkpoints.

fall_limit = -12  # respawn when falling below this height

_checkpoint = vec(0, 1, 0)

def on_start():
    _checkpoint = self.position
    game.checkpoint = 0
    game.finished = False
    game.start_time = time()

def on_update(dt):
    if self.y < fall_limit:
        respawn()
    if key_pressed("space"):
        play_sound("sounds/jump.wav", 0.4)

def respawn():
    self.position = _checkpoint
    self.velocity = vec(0, 0, 0)
    play_sound("sounds/hit.wav")
    camera_shake(0.3, 0.3)

def on_trigger(other):
    if other.tag == "lava":
        respawn()

# Checkpoints call this with other.send("set_checkpoint", position).
def set_checkpoint(position):
    _checkpoint = position
''')
    t.script("scripts/checkpoint.es", '''
# Touch it to respawn here instead of at the start.

number = 1  # which checkpoint this is

_reached = False

def on_trigger(other):
    if other.tag == "player" and not _reached:
        _reached = True
        other.send("set_checkpoint", self.position + vec(0, 1.2, 0))
        game.checkpoint = number
        play_sound("sounds/checkpoint.wav")
        self.color = "#22c55e"
''')
    t.script("scripts/mover.es", '''
# Slides back and forth. Players standing on it ride along.

distance = 2.5  # how far it moves to each side
speed = 1  # higher is faster

_start_x = 0

def on_start():
    _start_x = self.x

def on_update(dt):
    self.x = _start_x + sin(time() * 60 * speed) * distance
''')
    t.script("scripts/spinner.es", '''
# Spins around the vertical axis.

speed = 90  # degrees per second

def on_update(dt):
    self.rotation_y += speed * dt
''')
    t.script("scripts/finish.es", '''
# The end of the course.

def on_trigger(other):
    if other.tag == "player" and not game.finished:
        game.finished = True
        game.time = time() - game.start_time
        play_sound("sounds/win.wav")
        self.emit(120)
        broadcast("win")
''')
    t.script("scripts/hud.es", '''
def on_update(dt):
    if not game.finished:
        seconds = time() - game.start_time
        self.text = f"Time: {seconds:.1f}    Checkpoint: {game.checkpoint} / 2"
''')
    t.script("scripts/win.es", '''
def on_start():
    self.hide()

def on_message(name, data):
    if name == "win":
        self.text = f"You win! {game.time:.1f} seconds"
        self.show()
''')
    t.script("scripts/fade_out.es", '''
def on_start():
    wait(6)
    self.tween("alpha", 0, 1.5)
''')

    t.tutorial("Build your own obby", [
        {"title": "Play first!",
         "text": "Press Play. Move with WASD, jump with Space. The camera follows you. Reach the golden trophy!",
         "play": True},
        {"title": "No movement code needed",
         "text": "Select the Player: its CharacterController component already walks and jumps. Try a higher "
                 "Jump Height or Speed in the Inspector.",
         "select": "Player"},
        {"title": "Lava",
         "text": "Every object tagged 'lava' is a trigger. player.es checks the tag in on_trigger and respawns you at "
                 "the last checkpoint.",
         "open": "scripts/player.es",
         "code": "def on_trigger(other):\n    if other.tag == \"lava\":\n        respawn()"},
        {"title": "Moving parts",
         "text": "Moving platforms use mover.es and the spinning bar uses spinner.es. They are Kinematic rigid bodies: "
                 "scripts move them, and physics makes things standing on them ride along.",
         "open": "scripts/mover.es"},
        {"title": "Add a stage",
         "text": "Select a Jump platform, press Ctrl+D and drag the copy with the gizmo. Duplicate a Lava brick to make "
                 "new danger. Move the Finish Platform, Trophy and Finish further away to make room."},
        {"title": "Challenge",
         "text": "Make a disappearing platform: a script that waits 1 second after the player touches it, then hides it "
                 "and removes its BoxCollider with self.remove_component(\"BoxCollider\")."},
    ])


# ---------------------------------------------------------------- Crystal Forest (first person)


def explorer_3d(root):
    t = Template(root, "explorer-3d", "Crystal Forest",
                 "Explore a misty forest at sunset in first person. Find the glowing crystals to open the ancient gate. "
                 "Shows off lights, fog, shadows and post-processing.",
                 "EasyScript", "Intermediate", True, "#8B5CF6")
    t.project()
    t.sounds("collect.wav", "win.wav", "click.wav")

    rng = random.Random(11)
    s = Scene("Forest")
    camera = base_3d_scene(
        s, (0, 1.6, 8), (0, 0, 0), sky_top="#2e2a6b", horizon="#f59e6b", fog=True, fog_color="#6d5a8c",
        fog_density=0.03, sun_rot=(-10, 60, 0), sun_color="#ffb27a", sun_intensity=1.4, bloom=0.9,
        camera_extra={"Camera": {"projection": "Perspective", "field_of_view": 70}})
    s.add("Flashlight", {"Transform": T(0.25, -0.2, 0),
                         "Light": {"type": "Spot", "color": rgba("#fff3d6"), "intensity": 0, "range": 20,
                                   "spot_angle": 45, "cast_shadows": True}}, parent=camera)
    s.add("Ground", {"Transform": T(0, 0, 0, scale=(90, 1, 90)),
                     "MeshRenderer": {"mesh": "Plane", "color": rgba("#3d5c34"), "roughness": 0.95},
                     "BoxCollider": {"size": [1, 0.02, 1]}})

    def clear_spot(x, z):
        return math.hypot(x, z - 8) > 4 and math.hypot(x, z + 30) > 6 and abs(x) > 1.5 or z > 12

    placed = 0
    while placed < 45:
        x, z = rng.uniform(-38, 38), rng.uniform(-40, 30)
        if not clear_spot(x, z):
            continue
        placed += 1
        h = rng.uniform(0.9, 1.4)
        tree = s.add("Tree", {"Transform": T(round(x, 2), 0, round(z, 2), rot=(0, round(rng.uniform(0, 360)), 0))})
        s.add("Trunk", {"Transform": T(0, 1.2 * h, 0, scale=(0.4, 1.2 * h, 0.4)),
                        "MeshRenderer": {"mesh": "Cylinder", "color": rgba("#5b3a21"), "roughness": 0.9},
                        "BoxCollider": {"size": [1, 1, 1]}}, parent=tree)
        s.add("Leaves", {"Transform": T(0, 2.4 * h + 1.4, 0, scale=(2.4 * h, 3.2 * h, 2.4 * h)),
                         "MeshRenderer": {"mesh": "Cone", "color": rgba(rng.choice(["#2f6b3a", "#285e33", "#3a7a44"])),
                                          "roughness": 0.85}}, parent=tree)
    for _ in range(14):
        x, z = rng.uniform(-35, 35), rng.uniform(-38, 28)
        if not clear_spot(x, z):
            continue
        sc = rng.uniform(0.7, 1.8)
        s.add("Rock", {"Transform": T(round(x, 2), sc * 0.25, round(z, 2), scale=(round(sc * 1.4, 2), round(sc * 0.8, 2), round(sc, 2))),
                       "MeshRenderer": {"mesh": "Sphere", "color": rgba("#6b7280"), "roughness": 0.9},
                       "SphereCollider": {"radius": 0.5}})

    crystal_spots = [(-12, -6), (14, -2), (-20, -24), (22, -26), (4, -18), (-6, 18)]
    for x, z in crystal_spots:
        c = s.add("Crystal", {"Transform": T(x, 1.3, z), "SphereCollider": {"radius": 1.2, "is_trigger": True},
                              "Script": {"path": "scripts/crystal.es"}}, tag="crystal")
        s.add("Gem", {"Transform": T(0, 0, 0, scale=(0.5, 1.1, 0.5)),
                      "MeshRenderer": {"mesh": "Cone", "color": rgba("#67e8f9"), "metallic": 0.2, "roughness": 0.15,
                                       "emission": rgba("#22d3ee"), "emission_strength": 3}}, parent=c)
        s.add("Glow", {"Transform": T(0, 0.3, 0),
                       "Light": {"type": "Point", "color": rgba("#67e8f9"), "intensity": 3, "range": 6, "cast_shadows": False}},
              parent=c)

    # The ancient gate at the end of the path.
    gate = s.add("Gate", {"Transform": T(0, 0, -32)})
    for x in (-2.2, 2.2):
        s.add("Pillar", cube("#78716c", (1, 5, 1), (x, 2.5, 0), roughness=0.9), parent=gate)
    s.add("Lintel", cube("#78716c", (5.6, 0.9, 1.2), (0, 5.4, 0), roughness=0.9), parent=gate)
    s.add("Portal", {"Transform": T(0, 2.5, 0, rot=(90, 0, 0), scale=(3.4, 1, 5)),
                     "MeshRenderer": {"mesh": "Plane", "color": rgba("#1f1433"), "emission": rgba("#a855f7"),
                                      "emission_strength": 0, "cast_shadows": False},
                     "BoxCollider": {"size": [1, 0.5, 1], "is_trigger": True},
                     "Script": {"path": "scripts/portal.es"}}, parent=gate)
    s.add("Portal Light", {"Transform": T(0, 2.5, 1),
                           "Light": {"type": "Point", "color": rgba("#c084fc"), "intensity": 0, "range": 12}}, parent=gate)
    s.add("Fireflies", {"Transform": T(0, 1.5, -6),
                        "ParticleEmitter": {"rate": 10, "lifetime": 5, "speed": 0.3, "spread": 180, "gravity": [0, 0.05, 0],
                                            "shape_type": "Box", "shape_size": 18, "start_color": rgba("#d9f99d"),
                                            "end_color": rgba("#facc15", 0), "start_size": 0.08, "end_size": 0.04,
                                            "additive": True}})
    s.add("Player", {"Transform": T(0, 1, 8),
                     "CharacterController": {"first_person": True, "speed": 5, "jump_height": 1.2},
                     "Script": {"path": "scripts/player.es"}}, tag="player")

    s.add("Crosshair", {"UIElement": {"anchor": "Center", "size": [10, 10]},
                        "UIImage": {"shape": "Circle", "color": rgba("#ffffff", 0.7)}})
    s.add("Crystals", {"UIElement": {"anchor": "TopLeft", "offset": [24, -20], "size": [500, 50]},
                       "UIText": {"text": "Crystals: 0", "font_size": 36, "align": "Left", "color": rgba("#a5f3fc")},
                       "Script": {"path": "scripts/crystal_count.es"}})
    s.add("Message", {"UIElement": {"anchor": "Center", "offset": [0, 140], "size": [1100, 80]},
                      "UIText": {"text": "", "font_size": 44},
                      "Script": {"path": "scripts/message.es"}})
    s.add("Help", {"UIElement": {"anchor": "Bottom", "offset": [0, 16], "size": [1200, 40]},
                   "UIText": {"text": "WASD to walk, mouse to look, Space to jump, F for the flashlight, Esc to free the mouse",
                              "font_size": 24, "color": rgba("#e2e8f0")},
                   "Script": {"path": "scripts/fade_out.es"}})
    t.scene("scenes/main.scene", s)

    t.script("scripts/player.es", '''
# First-person controls come from the CharacterController (first_person is on).
# This script handles the mouse, the flashlight and falling out of the world.

flashlight_power = 6  # brightness when the flashlight is on

def on_start():
    lock_mouse(True)
    game.crystals = 0
    game.total_crystals = count("crystal")

def on_update(dt):
    if key_pressed("escape"):
        lock_mouse(False)
    if mouse_pressed("left"):
        lock_mouse(True)
    if key_pressed("f"):
        light = find("Flashlight").get_component("Light")
        if light.intensity > 0:
            light.intensity = 0
        else:
            light.intensity = flashlight_power
        play_sound("sounds/click.wav", 0.5)
    if self.y < -10:
        self.position = vec(0, 1, 8)
''')
    t.script("scripts/crystal.es", '''
# A glowing crystal that floats and spins. Walk into it to collect it.

_start_y = 0

def on_start():
    _start_y = self.y

def on_update(dt):
    self.y = _start_y + sin(time() * 90) * 0.15
    self.rotation_y += 60 * dt

def on_trigger(other):
    if other.tag == "player":
        game.crystals += 1
        play_sound("sounds/collect.wav")
        if game.crystals == game.total_crystals:
            broadcast("gate_open")
        else:
            broadcast("message", f"{game.total_crystals - game.crystals} crystals left")
        self.destroy()
''')
    t.script("scripts/portal.es", '''
# The gate lights up once every crystal has been found.

_open = False

def on_message(name, data):
    if name == "gate_open":
        _open = True
        self.get_component("MeshRenderer").emission_strength = 4
        find("Portal Light").get_component("Light").intensity = 6
        broadcast("message", "The gate is open! Find it at the end of the path.")

def on_trigger(other):
    if other.tag == "player" and _open:
        play_sound("sounds/win.wav")
        broadcast("message", "You found the way home. Thanks for playing!")
        _open = False
''')
    t.script("scripts/crystal_count.es", '''
def on_update(dt):
    self.text = f"Crystals: {game.crystals} / {game.total_crystals}"
''')
    t.script("scripts/message.es", '''
# Shows messages sent with broadcast("message", "some text").

_hide_at = 0

def on_message(name, data):
    if name == "message":
        self.text = data
        self.alpha = 1
        _hide_at = time() + 4

def on_update(dt):
    if _hide_at > 0 and time() > _hide_at:
        self.tween("alpha", 0, 1)
        _hide_at = 0
''')
    t.script("scripts/fade_out.es", '''
def on_start():
    wait(8)
    self.tween("alpha", 0, 1.5)
''')

    t.tutorial("Crystal Forest", [
        {"title": "Explore!",
         "text": "Press Play, then click in the game view to capture the mouse. Walk with WASD, look with the mouse. "
                 "Find all six crystals. Press Esc to get the mouse back.",
         "play": True},
        {"title": "Lighting and mood",
         "text": "Select Environment and Sun. The low orange sun, purple fog and procedural sky make the sunset. "
                 "Try moving the sun higher (change its X rotation to -60) for daytime.",
         "select": "Sun"},
        {"title": "Post-processing",
         "text": "The Camera has a PostProcessing component: bloom makes the crystals glow, SSAO adds soft shadows where "
                 "things meet, and the vignette darkens the edges. Turn on Advanced mode to see all the options.",
         "select": "Camera"},
        {"title": "Changing components from code",
         "text": "portal.es turns on the gate by changing its MeshRenderer and Light components.",
         "open": "scripts/portal.es",
         "code": "self.get_component(\"MeshRenderer\").emission_strength = 4\nfind(\"Portal Light\").get_component(\"Light\").intensity = 6"},
        {"title": "Challenge",
         "text": "Add a creature that follows the player: a Sphere with a script that calls "
                 "self.move_toward(find(\"Player\"), 2 * dt) every frame. Then make it glow!"},
    ])
