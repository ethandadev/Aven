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
- **Installers**: a Setup program for Windows (no administrator needed, Start menu shortcut,
  uninstaller), a .dmg for macOS (drag Aven into Applications) and a Flatpak for Linux. The zips
  are still there.
- **A real Mac app**: Aven.app, with its icon and its name in the Dock and the menu bar (not
  "Terminal"), signed and notarized when the release has a Developer ID set up.
- **The Mac menu bar**: on a Mac, Aven's menus are in the menu bar at the top of the screen, with
  the usual Aven menu (About, Settings, Check for Updates, Hide, Quit). Shortcuts read Cmd and
  Option there, and code editor replace is Cmd+Option+F (Cmd+H hides the app).
- On a Mac, the Window menu has Minimize (Cmd+M), Zoom and Enter Full Screen (Ctrl+Cmd+F), and
  the disk image shows Aven's icon.
- Release builds can be code signed on Windows too (see docs/releasing.md).
- The editor's Windows program has Aven's icon.
- **Go back after an update**: Preferences > Behavior > Updates > Go back to Aven ... restarts with
  the version the last update replaced.
- **Crash recovery**: while you have unsaved changes, Aven keeps a copy of them (the scene and any
  edited scripts) every 30 seconds, away from your files. If it closes unexpectedly, opening the
  project again offers them back.
- **`import` in EasyScript**: put shared functions in one script and use them from others with
  `import utils`, `from utils import jump`, `import utils as u` or `import "folder/utils.es"`.
  Every importer shares one copy, so its variables are shared too. Mistakes (a missing script, two
  scripts importing each other) get plain messages.
- **A script debugger**: click a line number (or F9) for a breakpoint. When the game reaches it,
  everything stops and the Debugger shows the values there and the calls that led to it. Continue
  (F5), Step Over (F10), Step Into (F11), Step Out (Shift+F11).
- `refresh_paths()` for scripts that move walls; paths already notice walls and floors that are
  made or destroyed, straight away.

### Security
- **Updates are signed with a key that isn't on GitHub.** The editor installs a download only if
  its signature (Ed25519, over its name and SHA-256) checks out with the key built into it, so
  publishing a release isn't enough to reach anyone's computer. The name has the version in it, so
  an old signed download can't pose as a new one. (docs/releasing.md: making the key.)
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
- A build folder made before a version change kept showing the old version number.
- Automated editor runs (tests) no longer add their throwaway projects to the Recent list.
- Unpacking a zip now keeps programs runnable on macOS and Linux.
- After an update, Settings > Apps on Windows shows the new version; on a Mac, running Aven from
  the disk image explains that it has to go into Applications before it can update.

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
