# Changes

What's new in each version. When a version is released, its section here becomes the notes on its
GitHub release, and the editor's Update window shows them. New changes go under **Next**; releasing
renames it to the version (see [CONTRIBUTING.md](CONTRIBUTING.md#making-a-release)).

## Next

A careful pass over every part of Aven: odd input, full disks, read-only folders, undo at the wrong
moment and scripts that do strange things now get a clear message instead of a crash or a hang.
The editor also fits better at bigger UI sizes.

### New
- **A welcome tour** the first time Aven opens, with Pip, Aven's little diamond: your name, a
  picture (ten drawn critters, or your own), pronouns, how you found Aven, how much you've coded
  (which picks the Learn mode level), whether you've used Unity, Godot or Unreal (for their keys and
  the Code Ladder's language), then a theme, size, sounds and calm mode. Confetti at the end. Skip
  it any time; Help > Welcome Tour and Preferences > Profile bring it back. Answers stay on your
  computer.
- **A greeting on the start screen**: "Good morning, Sam!" with your picture and a different line
  each day. Click your picture to change it.
- **Graphics quality: Low, Medium, High and Ultra**. Project Settings > Game picks what a game
  starts with; players change it with the title screen's new Graphics button or a script's
  `set_graphics_quality("Low")`, and their choice is kept. High is what Aven always drew; Low turns
  off shadows and effects and draws the world at 75% (the game's text stays sharp) for older
  computers; Ultra has sharper shadows. The scene view has its own setting (Preferences > Scene
  view).
- **Every shortcut can be changed** (Preferences > Shortcuts), including the code editor's, the
  debugger's, the Pixel Editor's and the keys that fly the 3D camera. Each command can have two
  sets of keys. Start from Aven's keys or ones like **Unity, Godot or Unreal**; search by name or
  keys; clashes are pointed out; right-click removes keys or puts the originals back.
- **The keys your fingers know**: Ctrl+W (Cmd+W) closes the tab or window you're in, Ctrl+N makes a
  new scene, Ctrl+O quick-opens scenes and scripts, Ctrl+Shift+S saves the scene under a new name
  (File > Save Scene As, also new), Shift+F5 stops playing, and Ctrl+= / Ctrl+- / Ctrl+0 make the
  whole editor bigger or smaller. On a Mac, Cmd+Shift+Z redoes and Cmd+Backspace deletes.
- **More editor options**: calm mode (less motion), zoom speed and direction for the scene view,
  closing brackets and quotes as you type (on or off), open the last game when Aven starts, the
  start screen greeting, the FPS counter, and little sounds (clicks and cheers) all in
  Preferences.
- **The editor rests when nothing's happening**: with no game playing and no key or mouse touched
  for a second, it draws a few frames a second instead of 60 or more, and wakes the moment you
  move the mouse. It used about a fifth of the processor time while idle in our measurements, so
  laptops stay cooler and last longer. Preferences > Behavior > "Rest when nothing's happening".
- **Scene view resolution** (Preferences > Scene view): on Retina and other high-density screens the
  scene and game view now draw up to 1.5 pixels per point by default ("Balanced") instead of every
  pixel, which is much less work for the graphics chip. "Sharpest" goes back to every pixel;
  "Fastest" draws one per point. Exported games aren't affected.
- **C API version 2** for native C/C++ behaviors: `aven_parent`, `aven_children` and
  `aven_find_child` walk an object's family, and `aven_call_with` / `aven_call_text` call methods
  that take or give text (`play_state("Run")`, or a script's own functions). Modules built for
  version 1 keep working.

### Fixed
- **Stuck with a locked mouse** (Crystal Forest and other first-person games, playing in the
  editor): a panel could take the keyboard as the game started, so the game never heard Esc,
  and a locked mouse can't click anything, not even Stop. Now Esc always gives the mouse back while
  playing in the editor, clicking another panel does too, a game that locks the mouse gets the
  keyboard, and a click outside the game view (like on Stop) is never the game's.
- **Lag on a Mac**: the menu bar at the top of the screen was built again from scratch whenever
  anything in it changed (selecting an object changes what Cut and Copy can do), and macOS redoes
  the whole menu bar, Services menu included, each time. Now only what changed is changed. The
  Editor tools menu also stopped reading every tool's file from disk a few times a second.
- **Stutter while playing**: Bug Replay's thumbnails read the whole game view back from the
  graphics chip every half second (millions of pixels on a Retina screen); now it's shrunk on the
  chip first and only the thumbnail is read.
- **Your work is never quietly lost.** When a script, scene, drawing or sound can't be saved (a
  read-only folder, a full disk), Aven says so and keeps it unsaved. Save and close, Save and
  restart (for an update), switching scenes and closing a script tab wait until it's saved.
- Autosave while editing a prefab wrote it in the scene format, which damaged the prefab.
- **Scripts**: very deep nesting and long chains give an error instead of running out of room;
  lists and dictionaries that contain themselves print, compare and save without going round
  forever; a timer stopped by another timer in the same frame stays stopped; numbers too big to
  hold, `0x` with no digits, Windows line endings and `\uXXXX` in text are handled.
- **The game can't be broken by a script's maths**: `nan` and infinity are refused for positions,
  sizes, gravity, sounds and time scale, and a `nan` position is put back before physics sees it.
- **Crashes fixed**: a 3D character destroyed by its own `on_collide`; C behaviors that destroy
  their own object; Health and Hazard after a script changes the object; the Inspector after
  Reset to defaults; blocks after undo with a popup open; the Pixel Editor after undoing a new frame.
- **Hangs fixed**: the 2D grid far from the middle of the world; sprite sheets set to thousands
  of frames; particle emitters with huge rates.
- Online multiplayer: a relay player with a tiny allowance was dropped as if they'd left, and
  `on_room_ready` could run before the room was.
- Names with accents or emoji in searches, file names and folders; Windows file names that can't
  exist (CON, NUL, ending in a dot) are refused when renaming.
- The code ladder counts down for `range(10, 0, -1)` and keeps a number's digits.
- `aven-player --size` and every `aven-relay` option check what they're given.

### Improved
- **Many copies of a model are drawn at once**: objects sharing a mesh and texture (blocks, coins,
  trees, rocks) go to the graphics chip together instead of one draw call each, in the scene and
  in shadows. A game with 2000 spinning cubes went from 481 to 35 ms a frame; the 3D templates got
  faster too (the explorer 56 to 20 ms, the obby 13 to 8, counting the shadow changes below).
- **Scripts**: building text with `+=` in a loop adds to the text in place when nothing else is
  holding it (200,000 additions: 1.2 s before, 10 ms now); whole numbers turn into text without
  printf, which also speeds up dictionaries keyed by numbers; and removing a key from a big
  dictionary no longer rebuilds its whole index.
- **Big scenes**: walking the scene (which the renderer and editor do several times a frame) no
  longer copies every object's list of children, finding the camera looks only at cameras
  instead of every object, and Hierarchy rows scrolled out of sight cost almost nothing, so a
  folder of thousands can be open.
- **Faster 3D**: shadow maps are only drawn again when something in them moved (in the editor
  with the camera still, sun and lamp shadows went from most of the frame to almost nothing; while
  playing, lamps nothing moves near stay free). Solid objects are drawn nearest first, so what's
  hidden behind them skips its lighting; the sky only fills what's left; shadows are only looked
  up on the side facing the light, with 4 lookups instead of 9; the scene's normals are only
  written when SSAO uses them; and sky and fog colors are converted once a frame, not per pixel.
  Pictures are unchanged (compared pixel by pixel).
- The editor fits at every UI size: tool windows open no bigger than the screen, and the Assets
  grid, Inspector, theme and lighting cards follow the UI size. Long names end in "..." with the
  full name on hover.
- The Inspector stacks X, Y and Z when it's too narrow for them side by side, and shows colors as
  a swatch.
- Scene view hints sit on a dark strip, so they read over any game.
- New block variables get names scripts can use ("high score" becomes high_score).
- The Pixel Editor says when a picture is too big for it (over 1024 x 1024).

## 0.4.0

Aven installs properly now (a Setup program, a Mac app, a Flatpak) and updates itself. There's a
script debugger, `import` for sharing code between scripts, online multiplayer with room codes,
and crash recovery. On a Mac, the editor is the right size on Retina screens and its menus are in
the menu bar. This release also has a round of security fixes.

**Coming from 0.3.0?** 0.3.0 can't update itself, so download 0.4.0 below once. From 0.4.0 on,
Aven offers each new version when it starts.

### New
- **A script debugger**: click a line number in the code editor (or press F9) to add a breakpoint.
  When the game reaches that line, everything stops, the script opens at the line, and the
  Debugger window shows the values there and the calls that led to it. Continue (F5), Step Over
  (F10), Step Into (F11), Step Out (Shift+F11).
- **`import` in EasyScript**: put shared functions in one script and use them from others with
  `import utils`, `from utils import jump`, `import utils as u` or `import "folder/utils.es"`.
  Every script that imports it shares one copy, so its variables are shared too. Mistakes (a
  missing script, two scripts importing each other) get plain messages.
- **Online multiplayer**: `host_online()` gives the host a room code, and friends anywhere join
  with `join_online(code)`. It goes through a small relay server, `aven-relay` (in the Linux
  download), so nobody opens ports on their router. Set its address in Project Settings > Game >
  Online relay; docs/online-multiplayer.md explains how to run one.
- **Crash recovery**: while you have unsaved changes, Aven keeps a copy of them (the scene and any
  edited scripts) every 30 seconds, away from your files. If it closes unexpectedly, opening the
  project again offers them back.
- **Updates inside Aven**: when a new version is out, an "Update to ..." button appears in the menu
  bar and on the start screen. It shows what's new, downloads the new version, checks it's the real
  one, and puts it in place when you restart or close Aven. Your games and anything else you keep
  in Aven's folder are left alone. Help > Check for Updates looks any time; Preferences > Behavior
  > Updates turns the check off, adds beta versions, or goes back to the version an update replaced.
- **Installers**: a Setup program for Windows (no administrator needed, Start menu shortcut,
  uninstaller), a .dmg for macOS (drag Aven into Applications) and a Flatpak for Linux. The zips
  are still there.
- **A real Mac app**: Aven.app, with its icon and its name in the Dock and the menu bar (not
  "Terminal"), signed and notarized when the release has a Developer ID set up.
- **The Mac menu bar**: on a Mac, Aven's menus are in the menu bar at the top of the screen, with
  the usual Aven menu (About, Settings, Check for Updates, Hide, Quit). Shortcuts show Cmd and
  Option, and the code editor's replace is Cmd+Option+F (Cmd+H hides the app). The Window menu has
  Minimize (Cmd+M), Zoom and Enter Full Screen (Ctrl+Cmd+F).
- The editor's Windows program has Aven's icon, and release builds can be code signed on Windows
  too (docs/releasing.md).
- `refresh_paths()` for scripts that move walls; paths already notice walls and floors that are
  made or destroyed, straight away.

### Security
- **Updates are signed with a key that isn't on GitHub.** The editor installs a download only if
  its signature (Ed25519, over its name and SHA-256) checks out with the key built into it, so
  publishing a release isn't enough to reach anyone's computer. The name has the version in it, so
  an old signed download can't pose as a new one.
- The release workflow pins every action to a full commit SHA, can only read the repository
  except in its last step, and gives signing secrets only to the steps that use them.
- Building a project's C/C++ code asks first when its native/CMakeLists.txt isn't the one Aven
  makes (CMake scripts can run any command), and again if it changes.
- Multiplayer: the host only lets players move, delete and spawn their own objects; only the host
  says who joined or left; a player from a different game is turned away; saying hello twice no
  longer counts twice; a player who stops reading is let go instead of queueing memory forever.
- A certificate password is no longer shown in the export log if it can't be passed to the signing
  tool, and is never put on a command line.
- Zips are unpacked one file at a time, and entries claiming impossible compression are refused.
- Compiled-library fingerprints are SHA-256 (you'll be asked once more about ones you allowed).
- AVEN_UPDATE_URL and the other test settings only exist in test builds.

### Fixed
- **Everything was twice as big on Retina Macs** (and on Linux with Wayland scaling). The layout now
  follows the screen's points and only the text is drawn at the higher resolution, so it's sharp.
  Moving the window to a screen with different scaling (a laptop and a monitor, 100% and 150%)
  resizes the editor to match.
- A game exported for Windows from a signed Aven would have had a broken signature after getting
  its icon; the export now removes it (and signs again with your certificate, if set).
- Unpacking a zip now keeps programs runnable on macOS and Linux.
- A build folder made before a version change kept showing the old version number.
- Automated editor runs (tests) no longer add their throwaway projects to the Recent list.

## 0.3.0

A big one: animation state machines, an audio mixer, terrain, pathfinding, touch controls for
phones, LAN multiplayer, editor scripting, and one-click Windows, macOS and Linux apps with your own
icon. Plus a long list of editor fixes, many of them for working with several objects at once.

### New
- **Animator**: state machines (idle, run, jump...) for sprite sheets and 3D model clips, switched by
  parameters and triggers, with presets. Scripts can use `set_param()`, `trigger()` and `play_state()`.
- **Audio mixer**: buses with volume, mute, low-pass and echo (Project Settings > Audio).
- **Terrain**: sculpt hills with brushes, paint up to four texture layers, generate hills, and walk on it.
- **Pathfinding**: characters walk around walls in 2D and 3D with nothing to set up (Chase's "Walk
  around walls", `self.go_to()`, `find_path()`, the Walk to block).
- **Import settings** for images (pixel art, max size), models (scale) and sounds (streaming, volume).
- **LAN multiplayer**: host and join games on the same Wi-Fi, find games nearby, sync objects.
- **Touch controls**: an on-screen stick and buttons for phones and tablets.
- **Desktop apps from any computer**: Build & Share > Desktop apps makes a Windows program, a macOS
  app and a Linux program, with your game's icon, optionally signed (and notarized on a Mac).
- **Game details**, an optional **title screen**, and a short **"Made with Aven"** splash.
- **Installable web builds** that play offline, and **whole projects as a .zip**.
- **Editor tools**: EasyScript that edits the open scene, one undo step per run.
- Save, Undo and Redo buttons; Paste in Place (Ctrl+Shift+V); arrow keys in the Hierarchy;
  colorblind-friendly axis colors; a clearer Ask Aven.

### Fixed
- Dragging several selected objects into a folder moved only one; Add Component, script variables,
  Move up/down, Save as Prefab and more now apply to every selected object.
- Jumping, `key_pressed()`, clicks and mouse look didn't work in the web player, and some devices
  browsers call gamepads made the player walk by itself.
- Text cut off at Windows display scaling and bigger text sizes.
- Several crashes and memory bugs (Asset Library dragging, the command palette while playing, the
  scene view after play mode, missing sounds).
- Opening someone else's project asks before running its native code; scripts stay inside the game folder.

## 0.2.0

The first release: a 2D and 3D game engine for beginners, with an editor that starts you off with
templates and blocks and teaches its way up to real code.

### The engine
- **2D**: sprites and sprite sheets, tilemaps, text, particles and Box2D physics.
- **3D**: a PBR renderer with shadows, a sky and fog, glTF models with animation, and Jolt physics
  with a ready-made character controller.
- **Post-processing**: bloom, tonemapping, color grading, vignette, SSAO and FXAA.
- **Game UI**: text, buttons, panels, images, value bars and anchors. Buttons work without code:
  **Click Actions** list what a click does.
- **Audio** (2D and 3D sound, music), **save data**, scenes, prefabs and an object hierarchy.
- **Three ways to code, one runtime**: **blocks** that snap together like Scratch; **EasyScript**,
  a Python-like language with events like `on_update(dt)` and `wait()`; and **native C/C++**
  modules through a stable C API that uses EasyScript's names.
- **Behaviors**: gameplay from settings instead of code (platformer and top-down controllers,
  collectibles, hazards, health, spawners, scene links and more).
- Runs on **Windows, macOS and Linux**, and in **web browsers** (WebGL2).

### The editor
- Hierarchy with folders, an Inspector (several objects at once, prefab overrides, copy/paste/reset
  for any setting), a scene view with move/rotate/scale handles, assets, console, undo history,
  find in project, a command palette and a profiler.
- A **built-in code editor** with colors, suggestions, parameter hints, live problem checks and go
  to definition, and a **block editor**.
- **Eight starter templates**: Blank 2D, Blank 3D, Platformer, Gem Quest (top-down), Space
  Shooter, Cookie Clicker, 3D Obby and Crystal Forest (3D exploring).
- **Asset Library**: 69 free (CC0) sprites, backgrounds, low-poly 3D models and textures to click
  or drag into a scene.
- **Creator tools**: a Pixel Editor with animation frames, a Sprite Sheet slicer, a Tile Painter,
  a Sound Maker, particle presets, and screenshot and GIF capture.
- **Ready-made UI**: Start Menu, Pause Menu, Health Bar and Score Text, working without code.
- **Export** to desktop, the web and itch.io.
- **Your way**: themes, accent colors, font sizes, UI scale, shortcuts and layouts.
- **For experienced developers**: collision layers, debug drawing from scripts, Inspector hints in
  comments (`# @range(0, 10)`), Hierarchy filters, your own code editor, and
  `aven-editor --check` for CI.

### Made for learning
- **Learn mode** opens up the editor a level at a time: Starter, Explorer, Creator, then Pro.
- **Game recipes** build a working game from a few choices ("a platformer where you collect coins
  and avoid spikes"), with every part explained.
- **The Error Doctor** explains errors in plain words, points at the line and offers a fix.
- **Play-and-edit**: pause the game, change anything, then keep or undo the changes.
- **Ask Aven** changes objects from plain words ("make it faster and bouncier"), and **Explain my
  game** says what each object, or the whole game, does.
- **The Code Ladder** shows one script as blocks, EasyScript, C, and then as C# (Unity), GDScript
  (Godot), Luau (Roblox) and C++ (Unreal).
- **Game cards**: a picture with a QR code; anyone on your Wi-Fi can play from a link.
- **Bug replay** records the last 20 seconds of play and replays a bug exactly.
- **Contributor quests**: small, guided ways to help improve Aven itself.
