"""2D starter templates: Space Shooter (blocks), Gem Quest (EasyScript) and Cookie Clicker (blocks)."""

import random

import art
from common import T, Scene, Template, blk, rgba


def var(name):
    return {"type": "var", "inputs": {"var": name}}


def game(name):
    return {"type": "game_var", "inputs": {"name": name}}


def op(kind, a, b):
    return {"type": kind, "inputs": {"a": a, "b": b}}


def my(prop):
    return {"type": "my_property", "inputs": {"property": prop}}


def saved(key, default):
    return {"type": "load", "inputs": {"key": key, "default": default}}


def rand(lo, hi):
    return {"type": "random", "inputs": {"min": lo, "max": hi}}


AXIS_X = {"type": "axis", "inputs": {"axis": "horizontal"}}
AXIS_Y = {"type": "axis", "inputs": {"axis": "vertical"}}


# ---------------------------------------------------------------- Space Shooter


def space_shooter(root):
    t = Template(root, "space-shooter", "Space Shooter",
                 "Fly a spaceship, blast asteroids and beat your high score. Made 100% with blocks: "
                 "a great way to learn clones, prefabs, messages and saving.",
                 "Blocks", "Beginner", False, "#6366F1")
    t.project()
    art.write_all(t, ["ship", "asteroid"])
    t.sounds("shoot.wav", "explosion.wav", "lose.wav")

    s = Scene("Space")
    s.add("Camera", {"Transform": T(0, 0, 10), "Camera": {"size": 6, "background": rgba("#060a1c")}})
    s.add("Stars", {
        "Transform": T(0, 2),
        "ParticleEmitter": {"rate": 18, "burst": 70, "lifetime": 5, "speed": 3, "spread": 0, "direction": [0, -1, 0],
                            "gravity": [0, 0, 0], "shape_type": "Box", "shape_size": 11,
                            "start_color": rgba("#ffffff", 0.9), "end_color": rgba("#93c5fd", 0.2),
                            "start_size": 0.07, "end_size": 0.05, "additive": True},
    })
    ship = s.add("Ship", {
        "Transform": T(0, -4.5),
        "SpriteRenderer": {"texture": "images/ship.png", "size": [1.2, 1.2], "order": 5},
        "RigidBody2D": {"gravity_scale": 0, "fixed_rotation": True},
        "BoxCollider2D": {"size": [0.9, 0.9]},
        "Script": {"path": "scripts/ship.blocks"},
    }, tag="player")
    s.add("Flame", {
        "Transform": T(0, -0.55),
        "ParticleEmitter": {"rate": 45, "lifetime": 0.25, "speed": 3, "spread": 12, "direction": [0, -1, 0],
                            "gravity": [0, 0, 0], "start_color": rgba("#fde047"), "end_color": rgba("#ef4444", 0),
                            "start_size": 0.3, "end_size": 0.02, "additive": True, "world_space": False},
    }, parent=ship)
    s.add("Rock Spawner", {"Transform": T(0, 8), "Script": {"path": "scripts/spawner.blocks"}})
    s.add("Score", {
        "UIElement": {"anchor": "TopLeft", "offset": [24, -20], "size": [400, 56]},
        "UIText": {"text": "Score: 0", "font_size": 40, "align": "Left"},
        "Script": {"path": "scripts/score.blocks"},
    })
    s.add("Best", {
        "UIElement": {"anchor": "TopRight", "offset": [-24, -20], "size": [400, 56]},
        "UIText": {"text": "Best: 0", "font_size": 32, "align": "Right", "color": rgba("#a5b4fc")},
        "Script": {"path": "scripts/best.blocks"},
    })
    s.add("Game Over", {
        "UIElement": {"anchor": "Center", "offset": [0, 40], "size": [900, 120]},
        "UIText": {"text": "GAME OVER", "font_size": 110, "color": rgba("#f87171")},
        "Script": {"path": "scripts/game_over.blocks"},
    })
    s.add("Help", {
        "UIElement": {"anchor": "Bottom", "offset": [0, 16], "size": [900, 40]},
        "UIText": {"text": "Arrow keys to fly, Space to shoot", "font_size": 26, "color": rgba("#cbd5e1")},
        "Script": {"path": "scripts/help.blocks"},
    })
    t.scene("scenes/main.scene", s)

    # Prefabs: things the game creates while it runs.
    laser = Scene("Laser")
    laser.add("Laser", {
        "SpriteRenderer": {"shape": "RoundedSquare", "color": rgba("#7dd3fc"), "size": [0.14, 0.6], "order": 3},
        "RigidBody2D": {"gravity_scale": 0, "fixed_rotation": True, "continuous": True},
        "BoxCollider2D": {"size": [0.14, 0.6], "is_trigger": True},
        "Script": {"path": "scripts/laser.blocks"},
    }, tag="laser")
    t.prefab("prefabs/laser.prefab", laser)
    rock = Scene("Rock")
    rock.add("Rock", {
        "SpriteRenderer": {"texture": "images/asteroid.png", "size": [1.4, 1.4], "order": 2},
        "RigidBody2D": {"gravity_scale": 0},
        "CircleCollider2D": {"radius": 0.6},
        "Script": {"path": "scripts/rock.blocks"},
    }, tag="rock")
    t.prefab("prefabs/rock.prefab", rock)
    boom = Scene("Boom")
    boom.add("Boom", {
        "ParticleEmitter": {"rate": 0, "burst": 35, "lifetime": 0.8, "speed": 5, "spread": 180, "gravity": [0, 0, 0],
                            "start_color": rgba("#fde047"), "end_color": rgba("#ef4444", 0), "start_size": 0.35,
                            "end_size": 0.02, "additive": True},
        "Script": {"path": "scripts/boom.blocks"},
    })
    t.prefab("prefabs/boom.prefab", boom)

    t.blocks("scripts/ship.blocks", {"speed": 8}, [
        [blk("when_update"),
         blk("set_velocity", vx=op("multiply", AXIS_X, var("speed")), vy=op("multiply", AXIS_Y, var("speed"))),
         blk("if", condition=op("less", my("x"), -9), body=[blk("set_x", x=-9)]),
         blk("if", condition=op("greater", my("x"), 9), body=[blk("set_x", x=9)]),
         blk("if", condition=op("less", my("y"), -5.3), body=[blk("set_y", y=-5.3)]),
         blk("if", condition=op("greater", my("y"), 3), body=[blk("set_y", y=3)])],
        [blk("when_key", key="space"),
         blk("spawn", prefab="prefabs/laser.prefab", x=my("x"), y=op("add", my("y"), 0.7)),
         blk("play_sound", sound="sounds/shoot.wav")],
        [blk("when_touch", tag="rock"),
         blk("play_sound", sound="sounds/explosion.wav"),
         blk("spawn", prefab="prefabs/boom.prefab", x=my("x"), y=my("y")),
         blk("broadcast", message="game_over"),
         blk("destroy")],
    ])
    t.blocks("scripts/spawner.blocks", {}, [
        [blk("when_timer", seconds=0.7),
         blk("spawn", prefab="prefabs/rock.prefab", x=rand(-9, 9), y=my("y"))],
    ])
    t.blocks("scripts/laser.blocks", {}, [
        [blk("when_start"), blk("set_velocity", vx=0, vy=16)],
        [blk("when_update"), blk("if", condition=op("greater", my("y"), 8), body=[blk("destroy")])],
        [blk("when_touch", tag="rock"), blk("destroy")],
    ])
    t.blocks("scripts/rock.blocks", {}, [
        [blk("when_start"), blk("set_velocity", vx=rand(-1, 1), vy=rand(-6, -3))],
        [blk("when_update"), blk("if", condition=op("less", my("y"), -8), body=[blk("destroy")])],
        [blk("when_touch", tag="laser"),
         blk("change_game", name="score", value=10),
         blk("play_sound", sound="sounds/explosion.wav"),
         blk("shake", amount=0.15),
         blk("spawn", prefab="prefabs/boom.prefab", x=my("x"), y=my("y")),
         blk("destroy")],
    ])
    t.blocks("scripts/boom.blocks", {}, [
        [blk("when_start"), blk("wait", seconds=1.2), blk("destroy")],
    ])
    t.blocks("scripts/score.blocks", {}, [
        [blk("when_start"), blk("set_game", name="score", value=0)],
        [blk("when_update"), blk("set_text", text={"type": "join", "inputs": {"a": "Score: ", "b": game("score")}})],
    ])
    t.blocks("scripts/best.blocks", {}, [
        [blk("when_update"), blk("set_text", text={"type": "join", "inputs": {"a": "Best: ", "b": saved("best", 0)}})],
    ])
    t.blocks("scripts/game_over.blocks", {}, [
        [blk("when_start"), blk("hide")],
        [blk("when_receive", message="game_over"),
         blk("show"),
         blk("play_sound", sound="sounds/lose.wav"),
         blk("if", condition=op("greater", game("score"), saved("best", 0)),
             body=[blk("save", value=game("score"), key="best")]),
         blk("wait", seconds=2.5),
         blk("restart")],
    ])
    t.blocks("scripts/help.blocks", {}, [
        [blk("when_start"), blk("wait", seconds=4), blk("fade_to", alpha=0, seconds=1)],
    ])

    t.tutorial("How the shooter works", [
        {"title": "Play first!",
         "text": "Press Play. Fly with the arrow keys and shoot with Space. How long can you survive?", "play": True},
        {"title": "The ship",
         "text": "Open the ship's blocks. 'every frame' sets its speed from the arrow keys and keeps it on the screen. "
                 "'when space key pressed' spawns a laser just above the ship.",
         "open": "scripts/ship.blocks", "select": "Ship"},
        {"title": "Prefabs",
         "text": "Lasers, rocks and explosions are prefabs: saved objects in the prefabs folder that 'spawn' creates "
                 "while the game runs. Double-click a prefab in the Assets panel to put one in the scene and see it."},
        {"title": "Messages",
         "text": "When a rock hits the ship, the ship broadcasts 'game_over'. The Game Over text listens with "
                 "'when I receive game_over', shows itself, saves the best score and restarts.",
         "open": "scripts/game_over.blocks"},
        {"title": "Make it harder",
         "text": "Open spawner.blocks and change 'every 0.7 seconds' to 0.4. Or open rock.blocks and make rocks fall faster.",
         "open": "scripts/spawner.blocks"},
        {"title": "Challenge",
         "text": "Add power-ups: make a new prefab with a star shape and a trigger collider, spawn it now and then, and "
                 "when the ship touches it, change the ship's speed variable."},
    ])


# ---------------------------------------------------------------- Top-down: Gem Quest


def top_down(root):
    t = Template(root, "top-down", "Gem Quest",
                 "A top-down adventure: explore the meadow, collect every gem, dodge the slimes and find the portal. "
                 "Written in EasyScript, with health, enemies that chase you and a win screen.",
                 "EasyScript", "Beginner", False, "#F59E0B")
    t.project()
    art.write_all(t, ["hero", "gem", "slime", "tree", "portal"])
    t.sounds("collect.wav", "hit.wav", "lose.wav", "win.wav")

    rng = random.Random(4)
    s = Scene("Meadow")
    player_id = s.reserve()
    s.add("Camera", {
        "Transform": T(0, 0, 10),
        "Camera": {"size": 6, "background": rgba("#2f6b3a")},
        "CameraFollow": {"target": player_id, "smoothness": 5},
    })
    s.add("Grass", {"Transform": T(0, 0), "SpriteRenderer": {"color": rgba("#62b861"), "size": [34, 24], "order": -20}})
    s.add("Path", {"Transform": T(-3, -0.5), "SpriteRenderer": {"shape": "RoundedSquare", "color": rgba("#c9a66b"), "size": [22, 1.4], "order": -18}})
    s.add("Path", {"Transform": T(6, -4.5), "SpriteRenderer": {"shape": "RoundedSquare", "color": rgba("#c9a66b"), "size": [1.4, 8], "order": -18}})
    for _ in range(60):
        x, y = rng.uniform(-16, 16), rng.uniform(-11, 11)
        c = rng.choice(["#fef08a", "#f9a8d4", "#ffffff", "#c4b5fd"])
        s.add("Flower", {"Transform": T(round(x, 2), round(y, 2)), "SpriteRenderer": {"shape": "Circle", "color": rgba(c), "size": [0.18, 0.18], "order": -17}})

    # Hedges around the edge keep the hero inside.
    for x, y, w, h in [(0, 12, 36, 1), (0, -12, 36, 1), (-17.5, 0, 1, 24), (17.5, 0, 1, 24)]:
        s.add("Hedge", {"Transform": T(x, y), "SpriteRenderer": {"shape": "RoundedSquare", "color": rgba("#1f5130"), "size": [w, h]},
                        "BoxCollider2D": {"size": [w, h]}})
    s.add("Pond", {"Transform": T(-9, 6), "SpriteRenderer": {"shape": "Circle", "color": rgba("#38bdf8"), "size": [6, 4]},
                   "CircleCollider2D": {"radius": 1.9}})
    for x, y in [(-12, -6), (-6, 3), (2, 6), (9, 8), (12, 2), (-2, -7), (11, -8), (-14, 9), (4, -2.5), (14, -3)]:
        s.add("Tree", {"Transform": T(x, y), "SpriteRenderer": {"texture": "images/tree.png", "size": [1.6, 1.6], "order": 3},
                       "CircleCollider2D": {"radius": 0.3, "offset": [0, -0.35]}})
    for x, y in [(-8, -2.5), (7, 4.5), (0, 3)]:
        s.add("Rock", {"Transform": T(x, y), "SpriteRenderer": {"shape": "Circle", "color": rgba("#8b8f98"), "size": [1.1, 0.9]},
                       "CircleCollider2D": {"radius": 0.45}})

    for x, y in [(-13, -9), (-10, 2), (-3, 8.5), (8, -9.5), (14.5, 9.5), (13, -1)]:
        s.add("Gem", {"Transform": T(x, y), "SpriteRenderer": {"texture": "images/gem.png", "size": [0.8, 0.8], "order": 2},
                      "CircleCollider2D": {"radius": 0.35, "is_trigger": True}, "Script": {"path": "scripts/gem.es"}}, tag="gem")
    for x, y in [(-6, -8), (9, 1), (-12, 5), (4, 9)]:
        s.add("Slime", {"Transform": T(x, y), "SpriteRenderer": {"texture": "images/slime.png", "size": [1, 1], "order": 3},
                        "RigidBody2D": {"gravity_scale": 0, "fixed_rotation": True, "linear_damping": 3},
                        "CircleCollider2D": {"radius": 0.35, "offset": [0, -0.2]},
                        "Script": {"path": "scripts/slime.es"}}, tag="enemy")
    s.add("Portal", {"Transform": T(0, -9), "SpriteRenderer": {"texture": "images/portal.png", "size": [1.8, 1.8], "order": 1},
                     "CircleCollider2D": {"radius": 0.6, "is_trigger": True},
                     "ParticleEmitter": {"rate": 25, "lifetime": 1, "speed": 1.2, "spread": 180, "gravity": [0, 0, 0],
                                         "start_color": rgba("#e9d5ff"), "end_color": rgba("#9333ea", 0),
                                         "start_size": 0.2, "end_size": 0, "emitting": False},
                     "Script": {"path": "scripts/portal.es"}})
    s.add("Player", {"Transform": T(0, 0), "SpriteRenderer": {"texture": "images/hero.png", "size": [1, 1], "order": 5},
                     "RigidBody2D": {"gravity_scale": 0, "fixed_rotation": True},
                     "BoxCollider2D": {"size": [0.6, 0.45], "offset": [0, -0.25], "friction": 0},
                     "Script": {"path": "scripts/player.es"}}, tag="player", eid=player_id)

    hearts = s.add("Hearts", {"UIElement": {"anchor": "TopLeft", "offset": [24, -20], "size": [180, 50]},
                              "Script": {"path": "scripts/hearts.es"}})
    for i in range(3):
        s.add(f"Heart{i + 1}", {"UIElement": {"anchor": "Left", "offset": [i * 56, 0], "size": [48, 44]},
                                "UIImage": {"shape": "Heart", "color": rgba("#ef4444")}}, parent=hearts)
    s.add("Gem Count", {"UIElement": {"anchor": "TopRight", "offset": [-24, -20], "size": [360, 56]},
                        "UIText": {"text": "Gems: 0", "font_size": 38, "align": "Right", "color": rgba("#a5f3fc")},
                        "Script": {"path": "scripts/gem_count.es"}})
    s.add("Message", {"UIElement": {"anchor": "Center", "offset": [0, 120], "size": [1000, 90]},
                      "UIText": {"text": "Collect all the gems!", "font_size": 48},
                      "Script": {"path": "scripts/message.es"}})
    t.scene("scenes/main.scene", s)

    t.script("scripts/player.es", '''
# The hero. Walks with the arrow keys or WASD.

speed = 5  # walking speed
max_health = 3  # hearts at the start

_hurt_time = 0

def on_start():
    game.health = max_health
    game.gems = 0
    game.total_gems = count("gem")

def on_update(dt):
    if game.health <= 0:
        self.velocity = vec(0, 0)
        return
    x = axis("horizontal")
    y = axis("vertical")
    self.velocity = vec(x, y) * speed
    if x < 0:
        self.flip_x = True
    elif x > 0:
        self.flip_x = False

    # Blink for a moment after getting hurt.
    if _hurt_time > 0:
        _hurt_time -= dt
        if int(_hurt_time * 10) % 2 == 0:
            self.alpha = 0.3
        else:
            self.alpha = 1
    else:
        self.alpha = 1

# Slimes call this with other.send("hurt", 1).
def hurt(amount):
    if _hurt_time > 0 or game.health <= 0:
        return
    _hurt_time = 1.2
    game.health -= amount
    play_sound("sounds/hit.wav")
    camera_shake(0.3, 0.3)
    if game.health <= 0:
        play_sound("sounds/lose.wav")
        broadcast("game_over")
''')
    t.script("scripts/slime.es", '''
# A slime that wanders around and chases the hero when it gets close.

speed = 2.5  # chase speed
sight = 4  # how close the hero must be before the slime notices

_wander = vec(0, 0)
_stunned = 0

def on_start():
    pick_direction()
    every(2, pick_direction)

def pick_direction():
    _wander = vec(random_range(-1, 1), random_range(-1, 1))

def on_update(dt):
    if _stunned > 0:
        _stunned -= dt
        return
    player = find("Player")
    if player and self.distance_to(player) < sight:
        self.velocity = self.direction_to(player) * speed
    else:
        self.velocity = _wander
    self.flip_x = self.velocity_x < 0

def on_collide(other):
    if other.tag == "player":
        other.send("hurt", 1)
        # Bounce back so the hero can get away.
        self.velocity = self.direction_to(other) * -6
        _stunned = 1
''')
    t.script("scripts/gem.es", '''
# A gem that bobs up and down. Collect them all to open the portal!

_start_y = 0

def on_start():
    _start_y = self.y

def on_update(dt):
    self.y = _start_y + sin(time() * 180) * 0.1

def on_trigger(other):
    if other.tag == "player":
        game.gems += 1
        play_sound("sounds/collect.wav")
        if game.gems == game.total_gems:
            broadcast("all_gems")
        self.destroy()
''')
    t.script("scripts/portal.es", '''
# The way out. It appears when every gem has been collected.

def on_start():
    self.hide()

def on_update(dt):
    self.angle += 90 * dt

def on_message(name, data):
    if name == "all_gems":
        self.show()
        self.get_component("ParticleEmitter").emitting = True

def on_trigger(other):
    if other.tag == "player" and self.visible:
        play_sound("sounds/win.wav")
        broadcast("win")
        other.hide()
''')
    t.script("scripts/hearts.es", '''
# Shows one heart for each point of health.

def on_update(dt):
    for i in range(1, 4):
        heart = self.find_child(f"Heart{i}")
        heart.visible = game.health >= i
''')
    t.script("scripts/gem_count.es", '''
def on_update(dt):
    self.text = f"Gems: {game.gems} / {game.total_gems}"
''')
    t.script("scripts/message.es", '''
# Big text in the middle of the screen for important moments.

_game_over = False
_hide_at = 3  # time() when the text disappears; 0 keeps it on screen

def on_message(name, data):
    if name == "all_gems":
        show_text("All gems found! Find the portal.", 3)
    elif name == "win":
        show_text("You escaped the meadow!", 0)
    elif name == "game_over":
        show_text("Oh no! Press R to try again", 0)
        _game_over = True

def show_text(text, seconds):
    self.text = text
    self.show()
    if seconds > 0:
        _hide_at = time() + seconds
    else:
        _hide_at = 0

def on_update(dt):
    if _hide_at > 0 and time() > _hide_at:
        self.hide()
        _hide_at = 0
    if _game_over and key_pressed("r"):
        restart_scene()
''')

    t.tutorial("Gem Quest", [
        {"title": "Play first!",
         "text": "Press Play. Walk with the arrow keys or WASD, collect the six gems and find the portal. Watch out for slimes!",
         "play": True},
        {"title": "The hero's script",
         "text": "Open player.es. on_update runs every frame: it reads the arrow keys with axis() and sets the velocity. "
                 "The hurt() function is called by slimes when they bump into the hero.",
         "open": "scripts/player.es", "select": "Player"},
        {"title": "Variables you can tweak",
         "text": "Variables at the top of a script (like speed and max_health) show up in the Inspector, so you can "
                 "change them without touching the code. Try giving the hero 5 hearts."},
        {"title": "Enemies",
         "text": "slime.es wanders in a random direction every 2 seconds, and chases the hero when it's closer than "
                 "'sight'. Change sight to 8 for scarier slimes.",
         "open": "scripts/slime.es"},
        {"title": "Talking between objects",
         "text": "Gems broadcast 'all_gems' when the last one is collected. The portal and the message text both have "
                 "on_message(name, data), so they react to it.",
         "code": "def on_message(name, data):\n    if name == \"all_gems\":\n        self.show()"},
        {"title": "Challenge",
         "text": "Add a heart pickup that gives back health: copy a gem, change its image to a heart shape, and write a "
                 "script that does game.health += 1 when the player touches it."},
    ])


# ---------------------------------------------------------------- Cookie Clicker


def clicker(root):
    t = Template(root, "clicker", "Cookie Clicker",
                 "Click the cookie, buy upgrades and watch the numbers grow. The simplest template: all blocks, no physics, "
                 "and your progress is saved between plays.",
                 "Blocks", "Beginner", False, "#D97706")
    t.project()
    art.write_all(t, ["cookie"])
    t.sounds("click.wav", "buy.wav")

    s = Scene("Bakery")
    s.add("Camera", {"Transform": T(0, 0, 10), "Camera": {"size": 5, "background": rgba("#3b2416")}})
    s.add("Glow", {"Transform": T(-3, -0.3), "SpriteRenderer": {"shape": "Star", "color": rgba("#fbbf24", 0.25), "size": [6.5, 6.5], "order": -1},
                   "Script": {"path": "scripts/glow.blocks"}})
    s.add("Cookie", {"Transform": T(-3, -0.3), "SpriteRenderer": {"texture": "images/cookie.png", "size": [4, 4]},
                     "ParticleEmitter": {"emitting": False, "rate": 0, "lifetime": 0.7, "speed": 4, "spread": 180,
                                         "gravity": [0, -8, 0], "start_color": rgba("#d9a066"), "end_color": rgba("#5b3413", 0),
                                         "start_size": 0.25, "end_size": 0.05, "additive": False},
                     "Script": {"path": "scripts/cookie.blocks"}})
    s.add("Bank", {"Script": {"path": "scripts/bank.blocks"}})
    s.add("Cookies", {"UIElement": {"anchor": "Top", "offset": [-200, -30], "size": [700, 80]},
                      "UIText": {"text": "0 cookies", "font_size": 64, "color": rgba("#fde68a")},
                      "Script": {"path": "scripts/cookies_text.blocks"}})
    s.add("Per Second", {"UIElement": {"anchor": "Top", "offset": [-200, -105], "size": [700, 40]},
                         "UIText": {"text": "per second: 0", "font_size": 28, "color": rgba("#f5d0a9")},
                         "Script": {"path": "scripts/per_second_text.blocks"}})
    shop = s.add("Shop", {"UIElement": {"anchor": "Right", "offset": [-30, 0], "size": [440, 380]},
                          "UIImage": {"shape": "RoundedSquare", "color": rgba("#1c110a", 0.7)}})
    s.add("Shop Title", {"UIElement": {"anchor": "Top", "offset": [0, -16], "size": [400, 50]},
                         "UIText": {"text": "Shop", "font_size": 40, "color": rgba("#fde68a")}}, parent=shop)
    upgrades = [
        ("Auto Baker", "auto_baker_cost", 15, "per_second", 1, 1.4, "+1 cookie every second"),
        ("Rolling Pin", "rolling_pin_cost", 40, "per_click", 1, 2.0, "+1 cookie per click"),
        ("Bakery", "bakery_cost", 200, "per_second", 10, 1.5, "+10 cookies every second"),
    ]
    for i, (name, key, cost, stat, amount, growth, help_text) in enumerate(upgrades):
        file = "scripts/" + name.lower().replace(" ", "_") + ".blocks"
        s.add(name, {"UIElement": {"anchor": "Top", "offset": [0, -80 - i * 95], "size": [400, 80]},
                     "UIButton": {"text": name, "font_size": 26, "normal_color": rgba("#7c4a1e"), "hover_color": rgba("#9a5c26"),
                                  "pressed_color": rgba("#5b3413"), "text_color": rgba("#fff7ed")},
                     "Script": {"path": file}}, parent=shop)
        t.blocks(file, {"cost": cost}, [
            [blk("when_start"), blk("set_var", var="cost", value=saved(key, cost))],
            [blk("when_update"),
             blk("set_text", text={"type": "join", "inputs": {
                 "a": f"{name}: ", "b": {"type": "join", "inputs": {"a": var("cost"), "b": f" cookies\n{help_text}"}}}})],
            [blk("when_clicked"),
             blk("if_else", condition=op("less", game("cookies"), var("cost")),
                 body=[blk("play_sound", sound="sounds/click.wav")],
                 else_body=[
                     blk("change_game", name="cookies", value=op("subtract", 0, var("cost"))),
                     blk("change_game", name=stat, value=amount),
                     blk("set_var", var="cost", value={"type": "math", "inputs": {"func": "round", "x": op("multiply", var("cost"), growth)}}),
                     blk("save", value=var("cost"), key=key),
                     blk("play_sound", sound="sounds/buy.wav")])],
        ])
    s.add("Start Over", {"UIElement": {"anchor": "BottomLeft", "offset": [20, 20], "size": [180, 48]},
                         "UIButton": {"text": "Start over", "font_size": 22, "normal_color": rgba("#44291a"),
                                      "hover_color": rgba("#5b3413"), "pressed_color": rgba("#2b1a10"), "text_color": rgba("#f5d0a9")},
                         "Script": {"path": "scripts/start_over.blocks"}})
    s.add("Help", {"UIElement": {"anchor": "Bottom", "offset": [0, 24], "size": [900, 40]},
                   "UIText": {"text": "Click the cookie! Buy upgrades in the shop. Your progress is saved.", "font_size": 24,
                              "color": rgba("#f5d0a9")}})
    t.scene("scenes/main.scene", s)

    t.blocks("scripts/cookie.blocks", {}, [
        [blk("when_clicked"),
         blk("change_game", name="cookies", value=game("per_click")),
         blk("play_sound", sound="sounds/click.wav"),
         blk("emit", count=6),
         blk("set_size", size=1.08),
         blk("wait", seconds=0.06),
         blk("set_size", size=1)],
    ])
    t.blocks("scripts/glow.blocks", {}, [
        [blk("when_update"), blk("turn", degrees=0.3)],
    ])
    t.blocks("scripts/bank.blocks", {}, [
        [blk("when_start"),
         blk("set_game", name="cookies", value=saved("cookies", 0)),
         blk("set_game", name="per_click", value=saved("per_click", 1)),
         blk("set_game", name="per_second", value=saved("per_second", 0))],
        [blk("when_timer", seconds=1),
         blk("change_game", name="cookies", value=game("per_second"))],
        [blk("when_timer", seconds=3),
         blk("save", value=game("cookies"), key="cookies"),
         blk("save", value=game("per_click"), key="per_click"),
         blk("save", value=game("per_second"), key="per_second")],
    ])
    t.blocks("scripts/cookies_text.blocks", {}, [
        [blk("when_update"), blk("set_text", text={"type": "join", "inputs": {"a": game("cookies"), "b": " cookies"}})],
    ])
    t.blocks("scripts/per_second_text.blocks", {}, [
        [blk("when_update"), blk("set_text", text={"type": "join", "inputs": {"a": "per second: ", "b": game("per_second")}})],
    ])
    reset = [blk("when_clicked")]
    for key, value in [("cookies", 0), ("per_click", 1), ("per_second", 0)] + [(u[1], u[2]) for u in upgrades]:
        reset.append(blk("save", value=value, key=key))
    reset.append(blk("restart"))
    t.blocks("scripts/start_over.blocks", {}, [reset])

    t.tutorial("Cookie Clicker", [
        {"title": "Play first!", "text": "Press Play and click the cookie. Buy an Auto Baker when you have 15 cookies.",
         "play": True},
        {"title": "Clicking",
         "text": "The cookie's blocks run 'when this object is clicked': add cookies, play a sound, burst some crumbs "
                 "and squish the cookie for a moment.",
         "open": "scripts/cookie.blocks", "select": "Cookie"},
        {"title": "Game variables",
         "text": "'game cookies' is a game variable: every script can read and change it. The Bank adds "
                 "'game per_second' cookies every second.",
         "open": "scripts/bank.blocks"},
        {"title": "Saving",
         "text": "'save ... as cookies' remembers a value even after the game closes, and 'saved cookies or 0' reads it "
                 "back. That's how your bakery is still there next time."},
        {"title": "Challenge",
         "text": "Add a fourth upgrade: select Auto Baker in the Hierarchy, duplicate it with Ctrl+D, move it down, and "
                 "make a copy of its blocks that gives +100 per second for 1000 cookies."},
    ])
