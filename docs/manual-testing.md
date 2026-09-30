# Things to test by hand

Automated tests cover a lot (`rynax_tests`, `tests/editor/run.py`, the sanitizer and fuzz runs in CI),
but not how things feel, how they look on your screen, or real hardware (Windows display scaling,
gamepads, phones, a Mac). This is the list for a manual pass: tick what works, and report what
doesn't with the steps to see it.

Try it at your normal display scaling, and once at a bigger one (Windows: Settings > Display >
Scale, 150%), and with Preferences > Look > Text size turned up. Nothing should be cut off.

## Start and projects
- [ ] First run (move your preferences file away, see Help > About): the welcome tour appears. Try
      every step: type a name, pick critters and your own picture (the chooser, and dropping one
      on the window), pronouns, each "how did you find", "Never" (skips the engines step) and
      "Some" + an engine (its keys are used), themes and sizes change right away, calm mode stops
      the bouncing, sounds play. Skip for now and Back work; Enter goes on.
- [ ] The start screen greets you by name with your picture; clicking it opens the tour.
      Preferences > Behavior turns the greeting off, and "Open my last game" starts in it.
- [ ] The start screen: make a game from each template; open a recent one; **Open a game** browser.
- [ ] **File > Export Project as .zip**, then open that zip from **Open a game** (and by dropping it
      on the window). It opens with everything there.
- [ ] Close with unsaved changes: you're asked to save; Cancel really cancels.
- [ ] Autosave (Preferences > Behavior) saves after the set minutes.
- [ ] Crash recovery: make a change, wait 30 seconds, then end Rynax from the task manager (Activity
      Monitor on a Mac). Opening the project again offers the change back; Recover brings it back
      unsaved, Throw away doesn't. Closing Rynax normally leaves nothing to recover.

## Shortcuts
- [ ] Preferences > Shortcuts: change a key (and a second key), right-click to remove / restore,
      search, a clash shows its warning, Esc cancels. Each keymap (Rynax, Unity, Godot, Unreal).
- [ ] The usual keys, with Cmd on a Mac: Ctrl+W closes a script tab and tool windows, Ctrl+N,
      Ctrl+O (quick open), Ctrl+Shift+S (Save Scene As), Ctrl+= / Ctrl+- / Ctrl+0 resize the editor;
      Ctrl+C/V/Z/Y in the code editor, the Pixel Editor and text boxes.

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
- [ ] **Ask Rynax**: type "bigger and red", press Ask, then Do it (and Ctrl+Z undoes it). Press Ask
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
- [ ] Crystal Forest (locks the mouse): Esc gives it back straight after Play and later; then Stop
      works and doesn't lock it again; clicking the game locks it again. Also with the Hierarchy
      clicked first.
- [ ] Errors: the Console, the Doctor's explanation and Fix button; Bug Replay.
- [ ] Debugger: click a line number in on_update for a breakpoint, Play: it stops there with the
      values shown; F10 steps a line, F11 goes into your own function, Shift+F11 comes back out, F5
      continues. Adding a line above a breakpoint moves it down with its line. Stop while stopped.
- [ ] Online multiplayer: run `rynax-relay` (on a server, or on this computer for a first try), put
      its address in Project Settings > Game > Online relay, host with `host_online()` in one copy
      of a game and join from another computer (on a different network if you can, like a phone
      hotspot) with the room code. Moving, `send()` and leaving work as on a local network.
- [ ] `import`: a script with `import utils` uses a function from scripts/utils.es; saving utils.es
      while playing uses the new version.
- [ ] Graphics quality: Project Settings > Game > Graphics quality, and an exported game's title
      screen Graphics button (Low / Medium / High / Ultra). Low is smooth on a slow computer and its
      text stays sharp; the choice is still there the next time the game opens.

## Tools
- [ ] Pixel Editor (draw, undo, save, use on the selected object), Sprite Sheet, Tile Painter,
      Sound Maker, particle presets, Terrain brushes, Animator, Asset Library (click, drag, apply texture).
- [ ] Code editor: autocomplete, errors while typing, go to definition, find/replace; the block editor.
- [ ] Tools > Editor tools: make one, run it, Ctrl+Z undoes it.
- [ ] Native code: build a module; opening someone else's project asks before running its native code.

## Build & Share
- [ ] **Game details**: name, version, description, made by, icon picture, "Made with Rynax", title screen.
- [ ] **Desktop apps**: export Windows/macOS/Linux (release downloads can make all three). Unzip each
      on its system: it runs, shows the icon (in Explorer / the Dock / the window), the splash, the
      title screen (Play, Quit, Enter, arrow keys, mouse, gamepad), and the game.
- [ ] Signing, if you have certificates: a Mac with your Team ID (+ notary profile), Windows with a .pfx.
- [ ] **Web browser**: build, open it, play with the keyboard (jumping!), mouse, a gamepad; on a phone
      with touch controls; install it to the home screen; play offline.
- [ ] **Share**: the game card, the Wi-Fi link on your phone, the itch.io zip.

## Installing
- [ ] Windows: `Rynax-<version>-Setup.exe` installs without asking for an administrator; Rynax is in
      the Start menu with its icon; it runs; Settings > Apps uninstalls it (your games stay).
- [ ] macOS: open the .dmg, drag Rynax into Applications, open it: the Dock and the menu bar say
      Rynax (not Terminal), with its icon. If the release was signed and notarized, no warning.
- [ ] macOS: the menus are in the menu bar at the top of the screen: Rynax (About, Settings...,
      Check for Updates..., Hide, Quit), File, Edit, Create, Window, Tools, Help. Items grey out
      and tick as they should; Cmd+S, Cmd+Z, Cmd+C in a text box vs in the scene; Cmd+Q asks about
      unsaved changes.
- [ ] macOS on a Retina screen: the editor is the normal size, and the text is sharp. Drag the
      window to an external monitor (and Windows: between 100% and 150% screens): it rescales.
- [ ] macOS Window menu: Minimize (Cmd+M), Zoom, Enter Full Screen (Ctrl+Cmd+F).
- [ ] Linux: `flatpak install Rynax-<version>-linux.flatpak`, then run it from the app menu.

## Opening someone else's project
- [ ] Open a project zip made on another computer (File > Export Project as .zip there): it opens,
      and nothing runs by itself. With compiled native code in it, Rynax asks before loading it.
- [ ] With a changed native/CMakeLists.txt in it, the Native Code window's Build asks first ("Build
      this project's C/C++ code?"), and Don't build runs nothing.

## Updates
- [ ] **On each system, update from the previous release** (with the update key set up): the
      installed copy (Setup.exe, the .dmg's app in Applications, the zip) finds the new version,
      installs it, restarts into it, and Preferences > Updates > Go back returns to the old one.
- [ ] With a release download of the previous version: the **Update to ...** button shows in the menu
      bar and on the start screen; the Update window shows the notes; **Download and install**
      shows progress (and Cancel stops it); **Restart now** opens the new version with the same game.
- [ ] **Later**, then close Rynax: the next start is the new version. Your projects, and a file of your
      own in Rynax's folder, are untouched.
- [ ] Windows: works with Rynax in Downloads, and says what to do when it's in Program Files.
- [ ] **Skip this version**; Preferences > Behavior > Updates (off, betas, "Don't skip it").
- [ ] Offline: Help > Check for Updates says it couldn't reach GitHub; nothing else breaks.

## Multiplayer
- [ ] Two computers on the same Wi-Fi: host, find the game, join, see each other move, leave.
- [ ] Online: run `rynax-relay`, set it in Project Settings > Game > Online relay, host with
      `host_online()`, join from another network with the room code.

## When things go wrong
- [ ] Make the project folder read-only (or fill a USB stick), change a script and the scene, then
      Save, Save and close, and close a script tab: each says it couldn't save, and nothing closes
      or loses your changes.
- [ ] Preferences > Look > UI size at 1.5x and 2x on a small screen: tool windows open on screen,
      nothing runs off the edge of the Assets panel or the Inspector.
- [ ] A file or project folder with accents or emoji in its name (Café, 🎮): open, rename, export.

## Hardware
- [ ] A gamepad (and one that's plugged in but not touched: nothing should move by itself).
- [ ] Two monitors, different scaling on each; a laptop's touch screen; a high refresh-rate monitor.
