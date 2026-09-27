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
