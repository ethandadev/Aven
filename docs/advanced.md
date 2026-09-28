# Aven for experienced developers

Aven starts simple for beginners, but none of that gets in your way. This page collects what
people coming from Unity, Godot or their own engines tend to look for. Set **Learn > Level** to
**Pro** to see every feature at once, and turn off **Preferences > Learning > Beginner helpers**
to hide the Ask Aven box, the Doctor buttons and the tips.

## Editor workflow

- **Command palette** (**Ctrl+K**): every command, window, object and file in one search.
  Shortcuts are rebindable in **Preferences > Shortcuts**.
- **Hierarchy search** lists matches flat, with where each one sits. Combine filters:

  | Filter | Finds |
  |---|---|
  | `slime` | names containing "slime" |
  | `t:RigidBody2D` | objects with that component (`t:body` works too) |
  | `t:prefab` | prefab copies |
  | `tag:enemy` | by tag (`tag:` alone: untagged) |
  | `layer:Bullet` | by collision layer |

  **Select all** selects every match, for editing them together in the Inspector.
- **Right-click > Select**: children, everything inside, same tag, same layer, same prefab, same
  script, or same kind (the same set of components).
- **Right-click > Align** with two or more selected: line up on X, Y (or Z in 3D) by the
  lowest, center or highest, or space them evenly. Also in the command palette.
- **Inspector**: multi-object editing, a lock (the padlock) to keep one object while selecting
  others, right-click any setting to reset, copy, paste or revert it to the prefab, and bold
  names with a bar for settings a prefab copy overrides.
- **Snapping**: hold **Ctrl** while dragging a gizmo, or turn on the magnet. Step sizes are in
  **Preferences > Scene view**.
- **Right-click an object > Copy as code** copies `find("Name")`.
- **Paste in Place** (**Ctrl+Shift+V**, Edit menu) pastes exactly where the copies were; turn on
  **Preferences > Behavior > Paste in the same place** to make plain Paste do that too. Pasted
  objects go back into the folder they were copied from.
- **Keyboard in the Hierarchy**: the arrow keys move through the list (Shift extends the
  selection), Left and Right close and open folders, Home and End jump to the ends, Esc selects
  nothing. Right-click actions (move up or down, out of the parent, save as prefab, add a script)
  and Add Component apply to every selected object.
- **Accessibility**: **Preferences > Look** has a High Contrast theme, text and UI size, and
  colorblind-friendly axis colors (orange, sky blue and pink instead of red, green and blue, for
  the move handles, X/Y/Z fields and grid lines).

## Physics: collision layers

**Project Settings > Collision layers** names up to 15 layers besides Default, with a grid of
which pairs collide. Pick an object's layer in the Inspector under Tag. It works for 2D and 3D
(including character controllers).

```easyscript
def on_start():
    self.layer = "PlayerBullet"         # changes what it collides with, keeping its motion

def on_update(dt):
    hit = raycast(self.position, self.position + vec(0, -2), layers="Ground")
    if hit:
        debug_line(self, hit["point"], "lime")
```

`raycast(..., layers=["Ground", "Enemy"])` takes a list too.

## Animation: state machines

**Add Component > Animator** picks which animation plays from parameters, like Unity's Animator
Controller. It drives the object's Sprite Animator (a range of sprite-sheet frames) or, on a 3D
model, its Model Animator (a clip by name), and adds one if it's missing.

- **Presets** set up a platformer character (idle, run, jump, fall), a top-down one (idle, walk)
  or a 3D character (picks clips named idle, walk/run, jump).
- **States** list their frames and speed (or clip), and whether they loop.
- **Transitions** are checked from the top every frame; the first one that holds switches state.
  "From" can be any state. Conditions: is true, is false, greater than, less than, triggered,
  animation finished (or after N seconds), always.
- **Parameters** are values scripts set. These are always there, read from the object:
  `speed` (sideways in 2D, along the ground in 3D), `vertical_speed`, `on_ground`, `time_in_state`.
- The picture at the top lights up the playing state while the game runs.

```easyscript
def on_key_pressed(key):
    if key == "x":
        self.trigger("attack")           # a trigger is used up by the transition it fires

def on_collide(other):
    if other.tag == "enemy":
        self.play_state("Hurt")          # jump straight to a state
    self.set_param("armed", True)
    print(self.anim_state)               # the state playing now
```

## Phones: touch controls and installing

**Project Settings > Touch controls** puts a stick (it presses the arrow keys) and up to four
buttons (each presses a key: Jump could press space) over the game. By default they show once the
screen is touched, so computers never see them; "Always" lets you try them with the mouse. Fingers
anywhere else act like the mouse, so tapping in-game buttons works, even with a thumb on the stick.
Games already written for the keyboard need no changes.

Web builds are installable apps ("progressive web apps"): each export has a manifest, icons
(pictures of the start scene) and a service worker that keeps every file. From a web host with
https (itch.io, GitHub Pages, Netlify...) phones and computers offer to install the game: it
opens full screen from the home screen, turned the right way, and plays offline. On iPhone it's
Share > Add to Home Screen; the page says so. Sharing over Wi-Fi from the editor uses plain http,
where browsers don't allow installing, but the game plays the same.

## Terrain

**Create > Terrain** makes 64 x 64 m of ground with gentle hills, painted with grass, dirt, rock
and snow from the Asset Library. It collides like ground in 3D physics, and pathfinding walks on it.

- **Sculpt**: pick Raise, Lower, Smooth or Flatten in the Inspector, then hold the left button on
  the ground in the Scene view. Shift does the opposite; Ctrl-click with Flatten picks the height to
  level to; Esc puts the brush down. Each stroke is one undo step.
- **Paint**: up to four layers, each a texture (drag one in from Assets), a color and how many
  meters one copy covers. **Paint by shape** puts layer 2 on slopes, 3 on steep sides, 4 up high.
- **Make hills** starts over with new noise hills; **Flatten all** levels everything.
- Size, resolution (height points per side) and max height are in the Inspector. Changing the
  resolution keeps the shape.

```easyscript
def on_start():
    self.y = terrain_height(self.x, self.z) + 0.5   # stand on the ground
```

## Import settings

Right-click an image, 3D model or sound in the Assets panel > **Import settings** (several at
once works too). Changes show straight away and are saved in `import.json`, which travels with
the project and its exports. Moving or renaming a file keeps its settings.

| Files | Settings |
| --- | --- |
| Images | Filter (auto, pixel, smooth), max size (shrinks big images on load), mipmaps, clamp edges |
| 3D models | Scale (0.01 for models made in centimeters), with the size in meters shown |
| Sounds | Volume, and stream from disk (for long files) |

## Pathfinding

Enemies and characters can find their way around walls, in 2D (top-down) and 3D, with no setup:
there's no navigation mesh to bake. The engine lays a grid over the world and checks only the
squares the search looks at. In 2D a square is blocked by any collider that doesn't move (walls,
tilemap tiles). In 3D it looks down for the ground, and steep slopes, cliffs and steps taller than
about half a unit block the way.

- **Chase > Walk Around Walls**: the ready-made chaser goes around walls instead of into them.
- **Blocks**: "walk to Player around walls", "stop walking", "when I get where I'm walking".
- **Debug** (next to the speed slider) draws the way each walker is going.

```easyscript
def on_click():
    self.go_to(find("Chest"), speed=4)   # or self.go_to(x, y), or a position
                                         # self.walking is True until it gets there

def on_arrive():
    self.say("Here!")

def on_update(dt):
    path = find_path(self, find("Player"))   # the points to walk through; [] if there's no way
    if len(path) > 1:
        debug_line(self, path[1], "cyan")
```

`find_path(a, b, radius=0.4)`: `radius` is how wide the walker is, so paths keep off walls. A
target inside a wall gets a path to the nearest spot next to it. Paths notice walls and floors
that are made or destroyed straight away; after a script *moves* one, call `refresh_paths()`.

## Audio: the mixer

**Project Settings > Audio mixer** has buses: Music, Effects and Voice to start with, plus any you
add. Each has a volume, mute, a low-pass filter (muffled, as if underwater or behind a wall) and
an echo. Changes apply live while the game runs. Every Audio Source picks a bus; music plays on
Music.

```easyscript
set_bus_volume("Music", 0.3)             # e.g. from an options menu
mute_bus("Effects", True)
play_sound("sounds/radio.wav", bus="Voice")
```

## Scripting

- **Inspector hints** in comments, like Unity's `[Range]` and `[Header]`:

  ```easyscript
  # @header Movement
  speed = 5         # @range(0, 20) how fast it runs
  jumps = 1         # @range(0, 3)
  hit_sound = "sounds/hit.wav"  # @sound
  _timer = 0        # names starting with _ never show
  debug_id = 7      # @hide
  ```

  `@sound`, `@image`, `@prefab` and `@scene` turn a text value into a file picker. Whatever
  else the comment says becomes the tooltip.
- **Debug drawing**: `debug_line(a, b)`, `debug_circle(center, radius)`, `debug_box(center, size)`
  and `debug_text(position, text)`. Each takes `color=` and `seconds=` (0 = this frame only).
  Positions can be vectors or objects. They show in the editor while playing (the **Debug**
  toggle above the game view), and in `aven-player --debug-draw`, never in normal exports.
- **Hot reload**: saving a script while the game runs swaps in the new code and keeps the
  variables the game changed.
- **Your own code editor**: **Preferences > Behavior > External code editor**, for example
  `code -g {file}:{line}` for VS Code. Scripts and Console errors then open there.
- **Native modules** in C or C++ for heavy lifting: see [Native code](native-code.md).
- **Code Ladder**: any script as C#, GDScript, Luau or C++, for moving to other engines.

## Multiplayer on the local network

Games on the same Wi-Fi (or cable) can play together: one hosts, the others join. It works in
desktop builds and in the editor's play mode (run a second copy with Build & Export > Run to test
on one computer); browsers can't open these connections, so web builds can't.

```easyscript
def on_key_pressed(key):
    if key == "h":
        host_game()
        say("Hosting at " + my_address())
    if key == "j":
        find_games()
        wait(1)
        games = games_found()
        if len(games) > 0:
            join_game(games[0]["address"])

def on_connected():            # also do this after host_game() for the host's own player
    spawn_networked("prefabs/player.prefab", 0, 0)

def on_receive(message, data, player):
    if message == "cheer":
        say("Player " + str(player) + " cheers!")
```

- **NetworkSync** (Add Component > Multiplayer) keeps an object's position, angle, scale and look
  (sprite frame, flip, visible) the same for everyone, about 15 times a second, smoothed.
- Objects in the scene belong to the host. `spawn_networked()` objects belong to whoever made them
  and go when that player leaves. `self.is_mine` says which is which.
- Other players' copies don't read this keyboard: their controllers (Platformer, Top-Down,
  Shooter...), `on_update` and key events pause, and their bodies follow the network.
- `send(message, data)` reaches every other player: numbers, text, lists and dictionaries.
- Changing scenes keeps the connection; stopping the game ends it.

It's made for friends in one room: no internet play (that needs a server), no cheating
protection, and physics isn't shared (each owner simulates its own objects).

## Editor tools: scripting the editor

Automate editing with EasyScript, like Unity's editor scripts. Each `.es` file in the project's
`editor_tools/` folder is a command in **Tools > Editor tools** and the command palette (Tool: ...).
Running it calls its `run()` on the scene you're editing: `find()`, `find_all()`, `spawn()`,
`terrain_height()` and every object property and action work as they do in a game, plus:

| | |
| --- | --- |
| `selection()` | the selected objects, as a list |
| `select(objects)` | select these (one object or a list) |
| `create("Cube", x, y, z)` | anything from the Create menu; gives the new object |
| `notify("Done!")` | a message when the tool finishes |

A run is one undo step. **Tools > Editor tools > New editor tool** starts from an example:

```easyscript
# Drops the selected objects onto the terrain.
def run():
    for obj in selection():
        ground = terrain_height(obj.x, obj.z)
        if ground != None:
            obj.y = ground
    notify("Put " + str(len(selection())) + " objects on the ground.")
```

Tools don't run in games, and editor tools can't add windows or buttons to the editor (yet).

## Command line and CI

```sh
aven-editor path/to/game --check           # scripts, scenes and file references; exit 1 on errors
aven-editor path/to/game --check --strict  # warnings fail too
aven-editor path/to/game --export out/     # a playable build
aven-player path/to/game --screenshot shot.png --frames 60 --hidden
aven-player path/to/game --debug-draw      # show debug_line() and friends
```

`--check` needs no window or GPU. It prints problems as `file:line:col: error: message`, which
GitHub Actions, GitLab and most editors turn into annotations. Aven's own CI runs it on every
template.

## Projects and version control

A project is a folder of plain files: `project.aven` (settings), scenes and prefabs as JSON, and
scripts as text, so they diff and merge well. New projects come with a `.gitignore` that leaves
out what Aven makes (`exports/`, `captures/`, `bug_reports/`, `.aven/`, `native/build/`).
