#!/usr/bin/env python3
"""Regenerates every starter template in templates/.

    python3 tools/templates/generate.py
    python3 tools/templates/generate.py --thumbnails build/bin/aven-player   # also render thumbnail.png files

Templates are ordinary Aven projects plus a template.json (shown in the editor's
new-project gallery) and a tutorial.json (shown in the Learn panel).
"""

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import art  # noqa: E402
from common import T, Scene, Template, blk, rgba  # noqa: E402
from templates_2d import clicker, space_shooter, top_down  # noqa: E402
from templates_3d import blank_3d, explorer_3d, obby_3d  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "templates")


# ---------------------------------------------------------------- Blank 2D


def blank_2d(root):
    t = Template(root, "blank-2d", "Blank 2D",
                 "An empty 2D world with a camera. Start from scratch and build anything you can imagine.",
                 "Blocks or EasyScript", "Beginner", False, "#64748B")
    t.project()
    s = Scene("Main")
    s.add("Camera", {"Transform": T(0, 0, 10), "Camera": {"size": 5, "background": rgba("#1e2533")}})
    s.add("Welcome", {"Transform": T(0, 0.6), "TextRenderer": {"text": "Your game starts here!", "font_size": 0.7}})
    s.add("Hint", {"Transform": T(0, -0.4), "TextRenderer": {
        "text": "Click + Add in the Hierarchy to add objects, then press Play.",
        "font_size": 0.3, "color": rgba("#94a3b8")}})
    t.scene("scenes/main.scene", s)
    for folder in ("images", "sounds", "prefabs", "scripts"):
        os.makedirs(t.path(folder), exist_ok=True)
        t.write_text(f"{folder}/.keep", "")
    t.tutorial("Your first game", [
        {"title": "Welcome to Aven!",
         "text": "This is an empty 2D game. The big area in the middle is the Scene view: it shows your game world. "
                 "On the left is the Hierarchy (every object in the scene) and on the right is the Inspector "
                 "(the settings of the selected object)."},
        {"title": "Add a player",
         "text": "Click '+ Add' at the top of the Hierarchy and choose 'Square'. Rename it to Player in the Inspector. "
                 "You can drag it around in the Scene view, and change its color and shape in the SpriteRenderer."},
        {"title": "Make it move with blocks",
         "text": "Right-click the Player in the Hierarchy and choose 'Add blocks script'. The block editor opens. "
                 "Drag 'every frame' from Events, then 'move with arrow keys' from Motion and snap it inside."},
        {"title": "Play it!",
         "text": "Press the green Play button (or Ctrl+P). Use the arrow keys to move. Press Play again to stop: "
                 "anything that changed while playing goes back to how it was.",
         "play": True},
        {"title": "Or write code",
         "text": "Every block is really a line of EasyScript. Tick 'Show code' in the block editor to see it, or make a "
                 "new EasyScript file in the Assets panel and type this:",
         "code": "speed = 5  # how fast the player moves\n\ndef on_update(dt):\n    self.x += axis(\"horizontal\") * speed * dt\n    self.y += axis(\"vertical\") * speed * dt"},
        {"title": "Keep going",
         "text": "Open the Reference (top right) to see everything scripts can do. When you're ready, look at the other "
                 "templates: they are full little games you can take apart."},
    ])


# ---------------------------------------------------------------- Platformer


def platformer(root):
    t = Template(root, "platformer", "Platformer",
                 "Run, jump and collect coins! Squash slimes, dodge spikes and reach the flag. "
                 "The player is made with blocks; everything else uses a few lines of EasyScript.",
                 "Blocks + EasyScript", "Beginner", False, "#22C55E")
    t.project()
    art.write_all(t, ["hero", "coin", "slime", "flag", "spikes"])
    t.sounds("jump.wav", "coin.wav", "hit.wav", "stomp.wav", "win.wav")

    s = Scene("Level 1")
    player_id = s.reserve()  # the camera follows the player, which is added later

    s.add("Camera", {
        "Transform": T(-6, 0, 10),
        "Camera": {"size": 6, "background": rgba("#8fd3ff")},
        "CameraFollow": {"target": player_id, "offset": [0, 1.5, 0], "smoothness": 4, "follow_z": False},
    })

    # Scenery: hills and clouds far behind everything.
    for i, (x, y, r, c) in enumerate([(-10, -7, 6, "#6cc56c"), (2, -8, 7, "#5fb85f"), (16, -7.5, 6.5, "#6cc56c"),
                                      (30, -8, 7, "#5fb85f"), (44, -7, 6, "#6cc56c"), (58, -8, 7, "#5fb85f")]):
        s.add("Hill", {"Transform": T(x, y), "SpriteRenderer": {"shape": "Circle", "color": rgba(c), "size": [r * 2, r * 2], "order": -20}})
    for x, y in [(-12, 4), (-2, 5.5), (9, 4.5), (21, 5.2), (33, 4.2), (46, 5.5)]:
        cloud = s.add("Cloud", {"Transform": T(x, y), "SpriteRenderer": {"shape": "Circle", "color": rgba("#ffffff", 0.9), "size": [1.6, 1.6], "order": -15}})
        s.add("Puff", {"Transform": T(0.9, -0.2), "SpriteRenderer": {"shape": "Circle", "color": rgba("#ffffff", 0.9), "size": [1.3, 1.3], "order": -15}}, parent=cloud)
        s.add("Puff", {"Transform": T(-0.9, -0.25), "SpriteRenderer": {"shape": "Circle", "color": rgba("#ffffff", 0.9), "size": [1.2, 1.2], "order": -15}}, parent=cloud)

    # Ground: dirt with a grass top. The top of the ground is at y = -2.
    for x0, x1 in [(-14, 8), (11, 25), (28, 54)]:
        w = x1 - x0
        g = s.add("Ground", {
            "Transform": T((x0 + x1) / 2, -4),
            "SpriteRenderer": {"color": rgba("#9a6534"), "size": [w, 4]},
            "BoxCollider2D": {"size": [w, 4], "friction": 0.6},
        })
        s.add("Grass", {"Transform": T(0, 1.8), "SpriteRenderer": {"color": rgba("#4cc35b"), "size": [w, 0.4], "order": 1}}, parent=g)

    # Floating platforms.
    for x, y, w in [(4, 1, 3), (13, 1.2, 3), (19.5, 3, 3), (31, 1.2, 2.5), (35.5, 3.2, 3), (43, 2, 4)]:
        p = s.add("Platform", {
            "Transform": T(x, y),
            "SpriteRenderer": {"shape": "RoundedSquare", "color": rgba("#b7793a"), "size": [w, 0.5]},
            "BoxCollider2D": {"size": [w, 0.5], "friction": 0.6},
        })
        s.add("Top", {"Transform": T(0, 0.18), "SpriteRenderer": {"color": rgba("#4cc35b"), "size": [w - 0.1, 0.16], "order": 1}}, parent=p)

    # Coins.
    for x, y in [(-4, -1.2), (-2.8, -1.2), (-1.6, -1.2), (4, 2), (13, 2.2), (19.5, 4), (26.5, 0.5),
                 (31, 2.2), (35.5, 4.2), (42, 3), (44, 3)]:
        s.add("Coin", {
            "Transform": T(x, y),
            "SpriteRenderer": {"texture": "images/coin.png", "size": [0.7, 0.7], "order": 2},
            "CircleCollider2D": {"radius": 0.3, "is_trigger": True},
            "Script": {"path": "scripts/coin.es"},
        }, tag="coin")

    # Spikes and slimes.
    for x in [16, 37, 38]:
        s.add("Spikes", {
            "Transform": T(x, -1.5),
            "SpriteRenderer": {"texture": "images/spikes.png", "size": [1, 1], "order": 2},
            "BoxCollider2D": {"size": [0.8, 0.4], "offset": [0, -0.25], "is_trigger": True},
        }, tag="hazard")
    for x, dist in [(21, 2), (47, 1.5)]:
        s.add("Slime", {
            "Transform": T(x, -1.6),
            "SpriteRenderer": {"texture": "images/slime.png", "size": [1, 1], "order": 3},
            "RigidBody2D": {"type": "Kinematic"},
            "BoxCollider2D": {"size": [0.8, 0.55], "offset": [0, -0.2], "is_trigger": True},
            "Script": {"path": "scripts/slime.es"},
        }, tag="enemy")

    s.add("Flag", {
        "Transform": T(51, -1),
        "SpriteRenderer": {"texture": "images/flag.png", "size": [2, 2], "order": 2},
        "BoxCollider2D": {"size": [1, 2], "offset": [-0.4, 0], "is_trigger": True},
        "ParticleEmitter": {"emitting": False, "rate": 0, "lifetime": 1.5, "speed": 6, "spread": 60,
                            "gravity": [0, -6, 0], "start_color": rgba("#fde047"), "end_color": rgba("#ef4444", 0),
                            "start_size": 0.25, "end_size": 0.05},
        "Script": {"path": "scripts/goal.es"},
    })

    s.add("Player", {
        "Transform": T(-8, -1.4),
        "SpriteRenderer": {"texture": "images/hero.png", "size": [1, 1], "order": 5},
        "RigidBody2D": {"fixed_rotation": True, "gravity_scale": 2.5},
        "BoxCollider2D": {"size": [0.7, 0.9], "offset": [0, -0.05], "friction": 0},
        "ParticleEmitter": {"emitting": False, "rate": 0, "lifetime": 0.6, "speed": 4, "spread": 180,
                            "gravity": [0, -6, 0], "start_color": rgba("#fde047"), "end_color": rgba("#f59e0b", 0),
                            "start_size": 0.2, "end_size": 0},
        "Script": {"path": "scripts/player.blocks"},
    }, tag="player", eid=player_id)

    # Screen UI.
    s.add("Score", {
        "UIElement": {"anchor": "TopLeft", "offset": [24, -20], "size": [360, 56]},
        "UIText": {"text": "Coins: 0", "font_size": 40, "align": "Left"},
        "Script": {"path": "scripts/score.es"},
    })
    s.add("Help", {
        "UIElement": {"anchor": "Bottom", "offset": [0, 16], "size": [900, 40]},
        "UIText": {"text": "Arrow keys or A/D to run, Space to jump. Jump on slimes to squash them!", "font_size": 24,
                   "color": rgba("#1e293b")},
        "Script": {"path": "scripts/fade_out.es"},
    })
    s.add("WinText", {
        "UIElement": {"anchor": "Center", "offset": [0, 60], "size": [800, 120]},
        "UIText": {"text": "You win!", "font_size": 96, "color": rgba("#fde047")},
        "Script": {"path": "scripts/win_text.es"},
    })
    t.scene("scenes/main.scene", s)

    speed = {"type": "var", "inputs": {"var": "speed"}}
    horizontal = {"type": "axis", "inputs": {"axis": "horizontal"}}
    t.blocks("scripts/player.blocks", {"speed": 6, "jump_power": 14}, [
        [blk("when_update"),
         blk("set_velocity_x", vx={"type": "multiply", "inputs": {"a": horizontal, "b": speed}}),
         blk("if", condition={"type": "less", "inputs": {"a": horizontal, "b": 0}}, body=[blk("flip", side="left")]),
         blk("if", condition={"type": "greater", "inputs": {"a": horizontal, "b": 0}}, body=[blk("flip", side="right")]),
         blk("if", condition={"type": "less", "inputs": {"a": {"type": "my_property", "inputs": {"property": "y"}}, "b": -10}},
             body=[blk("broadcast", message="player_hit")])],
        [blk("when_key", key="space"),
         blk("if", condition={"type": "on_ground"}, body=[
             blk("jump", power={"type": "var", "inputs": {"var": "jump_power"}}),
             blk("play_sound", sound="sounds/jump.wav")])],
        [blk("when_touch", tag="hazard"),
         blk("broadcast", message="player_hit")],
        [blk("when_receive", message="player_hit"),
         blk("play_sound", sound="sounds/hit.wav"),
         blk("shake", amount=0.4),
         blk("go_to", x=-8, y=-1.4),
         blk("set_velocity", vx=0, vy=0)],
    ])

    t.script("scripts/coin.es", '''
# A coin. When the player touches it, add 1 to the score and disappear.

def on_trigger(other):
    if other.tag == "player":
        game.coins += 1
        play_sound("sounds/coin.wav")
        other.emit(12)  # sparkles from the player's particle emitter
        self.destroy()
''')
    t.script("scripts/slime.es", '''
# A slime that walks back and forth. Jump on top of it to squash it!

speed = 1.5  # how fast it walks
distance = 2  # how far it walks from where it started

_start_x = 0
_direction = 1

def on_start():
    _start_x = self.x

def on_update(dt):
    self.x += speed * _direction * dt
    if self.x > _start_x + distance:
        _direction = -1
    elif self.x < _start_x - distance:
        _direction = 1

def on_trigger(other):
    if other.tag != "player":
        return
    # Landing on top squashes the slime; touching it from the side hurts.
    if other.velocity_y < 0 and other.y > self.y + 0.3:
        other.velocity_y = 11
        play_sound("sounds/stomp.wav")
        game.coins += 2
        self.destroy()
    else:
        broadcast("player_hit")
''')
    t.script("scripts/goal.es", '''
# The flag at the end of the level.

def on_trigger(other):
    if other.tag == "player" and not game.won:
        game.won = True
        play_sound("sounds/win.wav")
        self.emit(60)
        broadcast("win")
''')
    t.script("scripts/score.es", '''
# Shows the number of coins. `game` holds values every script can share.

def on_start():
    game.coins = 0
    game.won = False

def on_update(dt):
    self.text = f"Coins: {game.coins}"
''')
    t.script("scripts/fade_out.es", '''
# Shows the controls for a few seconds, then fades away.

def on_start():
    wait(6)
    self.tween("alpha", 0, 1.5)
''')
    t.script("scripts/win_text.es", '''
# Hidden until someone broadcasts "win".

def on_start():
    self.hide()

def on_message(name, data):
    if name == "win":
        self.show()
        self.text = f"You win! {game.coins} coins"
''')

    t.tutorial("Make the platformer your own", [
        {"title": "Play first!",
         "text": "Press Play and try the level: run with the arrow keys, jump with Space, collect coins and reach the flag.",
         "play": True},
        {"title": "The player is made of blocks",
         "text": "Open the player's blocks. 'every frame' sets the speed from the arrow keys, and 'when space key pressed' "
                 "makes it jump, but only when it's on the ground.",
         "open": "scripts/player.blocks", "select": "Player"},
        {"title": "Change how it feels",
         "text": "Select the Player: the Script section in the Inspector shows 'Speed' and 'Jump Power'. Try a speed of 9 "
                 "and a jump power of 15, then play again. These come from the variables in the blocks."},
        {"title": "Coins use EasyScript",
         "text": "Open coin.es. When something touches the coin, it checks whether it's the player, adds 1 to "
                 "game.coins, plays a sound and deletes itself.",
         "open": "scripts/coin.es",
         "code": "def on_trigger(other):\n    if other.tag == \"player\":\n        game.coins += 1\n        self.destroy()"},
        {"title": "Build more level",
         "text": "Select a Platform and press Ctrl+D to duplicate it, then drag the copy somewhere new. Duplicate coins and "
                 "slimes the same way. Everything with the 'hazard' tag hurts the player.",
         "select": "Platform"},
        {"title": "Challenge",
         "text": "Make a second level: right-click main.scene in the Assets panel and choose Duplicate, rename the copy "
                 "to level2.scene and change it. Then make goal.es load it after a short wait:",
         "code": "def on_trigger(other):\n    if other.tag == \"player\":\n        wait(2)\n        load_scene(\"scenes/level2.scene\")"},
    ])


def main():
    root = os.path.abspath(ROOT)
    os.makedirs(root, exist_ok=True)
    blank_2d(root)
    blank_3d(root)
    platformer(root)
    space_shooter(root)
    top_down(root)
    clicker(root)
    obby_3d(root)
    explorer_3d(root)
    print("Templates written to", root)
    if "--thumbnails" in sys.argv:
        player = sys.argv[sys.argv.index("--thumbnails") + 1]
        render_thumbnails(root, os.path.abspath(player))


def render_thumbnails(root, player):
    """Runs every template for a moment in the player and saves a small screenshot for the new-project gallery."""
    prefix = []
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        prefix = ["xvfb-run", "-a"]
    # Some games look better after a little play (e.g. the ship flying up into view).
    inputs = {
        "space-shooter": ["--press", "5:up:45", "--press", "60:space", "--press", "75:space", "--press", "90:space"],
        "platformer": ["--press", "10:right:60"],
    }
    for name in sorted(os.listdir(root)):
        folder = os.path.join(root, name)
        if not os.path.isfile(os.path.join(folder, "project.aven")) or name.startswith("blank"):
            continue
        out = os.path.join(folder, "thumbnail.png")
        subprocess.run(prefix + [player, folder, "--screenshot", out, "--frames", "100", "--size", "480x270", "--hidden"] +
                       inputs.get(name, []),
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("thumbnail", name, "ok" if os.path.exists(out) else "FAILED")


if __name__ == "__main__":
    main()
