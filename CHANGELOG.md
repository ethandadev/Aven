# Changes

What's new in each version. When a version is released, its section here becomes the notes on its
GitHub release, and the editor's Update window shows them. New changes go under **Next**; releasing
renames it to the version (see [CONTRIBUTING.md](CONTRIBUTING.md#making-a-release)).

## Next

### New
- **Updates inside Aven**: when a new version is out, an "Update to ..." button appears in the menu
  bar and on the start screen. It shows what's new, downloads the new version, checks it's the real
  one (its SHA-256 has to match the release's), and puts it in place when you restart or close Aven.
  Your games and anything else you keep in Aven's folder are left alone. Help > Check for Updates
  looks any time; Preferences > Behavior > Updates turns the check off or adds beta versions.

### Fixed
- A build folder made before a version change kept showing the old version number.
- Automated editor runs (tests) no longer add their throwaway projects to the Recent list.
- Unpacking a zip now keeps programs runnable on macOS and Linux.

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
