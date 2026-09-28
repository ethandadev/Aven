# Things to test by hand

Automated tests cover a lot (`aven_tests`, `tests/editor/run.py`, the sanitizer and fuzz runs in CI),
but not how things feel, how they look on your screen, or real hardware (Windows display scaling,
gamepads, phones, a Mac). This is the list for a manual pass: tick what works, and report what
doesn't with the steps to see it.

Try it at your normal display scaling, and once at a bigger one (Windows: Settings > Display >
Scale, 150%), and with Preferences > Look > Text size turned up. Nothing should be cut off.

## Start and projects
- [ ] The start screen: make a game from each template; open a recent one; **Open a game** browser.
- [ ] **File > Export Project as .zip**, then open that zip from **Open a game** (and by dropping it
      on the window). It opens with everything there.
- [ ] Close with unsaved changes: you're asked to save; Cancel really cancels.
- [ ] Autosave (Preferences > Behavior) saves after the set minutes.

## Hierarchy
- [ ] Click, Ctrl+click, Shift+click ranges (also across open folders).
- [ ] Drag several selected objects into a folder, between objects (above/below), and to the empty
      space at the bottom. All of them move, in order.
- [ ] Arrow keys: Up/Down move, Shift extends, Right opens/enters, Left closes/goes to parent,
      Home/End, Esc clears. F2 renames; Enter/Esc finish renaming.
- [ ] Right-click with several selected: Duplicate, Delete, Group into a folder, Move up/down,
      Move out of parent, Save each as a Prefab, Add blocks script/EasyScript, Align.
- [ ] Copy/Cut/Paste (Ctrl+C/X/V) and **Paste in Place** (Ctrl+Shift+V); pasted objects land in the
      folder they came from. Preferences > Behavior > Paste in the same place.
- [ ] Search box and filters (`t:RigidBody2D`, `tag:enemy`), Select all in results.

## Inspector
- [ ] With several objects selected: change position/scale (only the changed axis changes), tag,
      layer, a component setting, a script variable (e.g. Speed); all selected objects change.
- [ ] **+ Add Component** with several selected adds to all; the command palette's Add component too.
- [ ] **Ask Aven**: type "bigger and red", press Ask, then Do it (and Ctrl+Z undoes it). Press Ask
      with an empty box: a list of ideas appears.
- [ ] Right-click a setting: reset, copy/paste, revert to prefab. The lock keeps one object shown.
- [ ] Prefab copies: Open, Apply, Revert (with several copies selected too).

## Scene view
- [ ] Move/rotate/scale handles on one and on several objects; Ctrl snaps; the magnet toggles snapping.
- [ ] Click-cycling through overlapping objects; box select in 2D; F to focus; Alt+drag orbit (3D).
- [ ] Drag several pictures/prefabs from Assets into the scene: all placed, one undo step.
- [ ] Drag UI elements (several at once too) far across the screen; they keep up with the mouse.
- [ ] The 2D/3D switch and toolbar buttons: Save, Undo, Redo, World/Local, snap, grid.
- [ ] Preferences > Look > Colorblind-friendly axes changes the handle/field/grid colors.

## Assets
- [ ] Click/Ctrl/Shift select, drag files into folders and onto breadcrumbs, rename (F2), delete
      (goes to the trash, lists what uses it), Enter opens, Backspace goes up a folder.
- [ ] Drop files from your desktop onto the editor (pictures, sounds, a .gltf with its files).
- [ ] Import Settings (pixel art, max size, model scale, sound volume) apply straight away.

## Playing in the editor
- [ ] Play, Pause (click things and change them live), Step, Stop; keep or throw away live changes.
- [ ] Each template plays: controls, jumping, collecting, winning, respawning.
- [ ] Errors: the Console, the Doctor's explanation and Fix button; Bug Replay.

## Tools
- [ ] Pixel Editor (draw, undo, save, use on the selected object), Sprite Sheet, Tile Painter,
      Sound Maker, particle presets, Terrain brushes, Animator, Asset Library (click, drag, apply texture).
- [ ] Code editor: autocomplete, errors while typing, go to definition, find/replace; the block editor.
- [ ] Tools > Editor tools: make one, run it, Ctrl+Z undoes it.
- [ ] Native code: build a module; opening someone else's project asks before running its native code.

## Build & Share
- [ ] **Game details**: name, version, description, made by, icon picture, "Made with Aven", title screen.
- [ ] **Desktop apps**: export Windows/macOS/Linux (release downloads can make all three). Unzip each
      on its system: it runs, shows the icon (in Explorer / the Dock / the window), the splash, the
      title screen (Play, Quit, Enter, arrow keys, mouse, gamepad), and the game.
- [ ] Signing, if you have certificates: a Mac with your Team ID (+ notary profile), Windows with a .pfx.
- [ ] **Web browser**: build, open it, play with the keyboard (jumping!), mouse, a gamepad; on a phone
      with touch controls; install it to the home screen; play offline.
- [ ] **Share**: the game card, the Wi-Fi link on your phone, the itch.io zip.

## Updates
- [ ] With a release download of the previous version: the **Update to ...** button shows in the menu
      bar and on the start screen; the Update window shows the notes; **Download and install**
      shows progress (and Cancel stops it); **Restart now** opens the new version with the same game.
- [ ] **Later**, then close Aven: the next start is the new version. Your projects, and a file of your
      own in Aven's folder, are untouched.
- [ ] Windows: works with Aven in Downloads, and says what to do when it's in Program Files.
- [ ] **Skip this version**; Preferences > Behavior > Updates (off, betas, "Don't skip it").
- [ ] Offline: Help > Check for Updates says it couldn't reach GitHub; nothing else breaks.

## Multiplayer
- [ ] Two computers on the same Wi-Fi: host, find the game, join, see each other move, leave.

## Hardware
- [ ] A gamepad (and one that's plugged in but not touched: nothing should move by itself).
- [ ] Two monitors, different scaling on each; a laptop's touch screen; a high refresh-rate monitor.
