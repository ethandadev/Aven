# Architecture

Aven is C++20 with three programs on one engine library:

```
engine/     aven_engine (static library): everything a game needs at runtime
player/     aven-player: runs a game (desktop, and WebAssembly for the web)
editor/     aven-editor: the editor, built with Dear ImGui on the same engine
sdk/        aven.h (the C API for native modules), the native module template, examples
templates/  starter games, each a normal Aven project
tests/      aven_tests: unit and integration tests
tools/      scripts that make templates, art and docs, and build the web player
quests/     Contributor Quests (small guided contributions, shown in the editor)
```

## Engine modules (`engine/src/aven/`)

| Folder | What's there |
|---|---|
| `core/` | JSON, logging, files, UUIDs, embedded data |
| `math/` | vectors, quaternions, matrices, colors |
| `ecs/` | a sparse-set entity/component registry |
| `scene/` | components (`components.h`), reflection (fields, labels, ranges, tooltips), and `Scene`: hierarchy, save/load, prefabs |
| `platform/` | window and input (GLFW; the HTML5 gamepad API on the web) |
| `render/` | `rhi.h` (the graphics API interface) with an OpenGL 3.3 / WebGL2 backend in `gl/`; `Renderer2D` (batched sprites, text, particles), `Renderer3D` (PBR, shadows, sky, fog), post-processing, UI layout, fonts, meshes and glTF models; `SceneRenderer` ties them together |
| `assets/` | loading and caching images, fonts, sounds and models from the project folder, with hot reload |
| `audio/` | the miniaudio backend and the sfxr-style sound synthesizer |
| `script/` | EasyScript: lexer, parser (AST), compiler to bytecode, VM, standard library, and the Code Ladder translators |
| `blocks/` | the block catalog, and blocks-to-EasyScript compilation |
| `runtime/` | `Game`: the scene plus every system that makes it move (scripts, native code, 2D and 3D physics, audio, behaviors, particles, animation, UI), bug-replay recording, and project settings |

### A frame of a game

`Game::update(dt)`:

1. Scene changes requested last frame happen.
2. `GameplaySystems::preUpdate`: input-driven things like UI buttons and clicks.
3. `ScriptSystem::update`:
   - New scripts run `on_start`, then waiting `wait()` tasks resume.
   - `on_key_pressed` and `on_update` run, then native (C) behaviors do the same.
   - Tweens advance.
4. Behaviors (controllers, spawners, health...) update.
5. Physics runs at a fixed 60 steps per second:
   - `on_fixed_update` runs first.
   - Then Box2D (2D) and Jolt (3D) step and report collisions to scripts, behaviors and native
     code.
6. Animation, particles, audio and camera follow update.

`Game::render` asks `SceneRenderer` to draw with the scene's camera:

1. 3D shadow maps.
2. The sky.
3. Opaque 3D objects, then transparent 3D objects.
4. 2D sprites, tilemaps, text and particles, sorted by order.
5. Post-processing (HDR to the screen).
6. Screen UI.

### Components and reflection

Components are plain structs in `scene/components.h`. `components.cpp` registers each one's
fields with a label, range and tooltip. Everything else is driven by that registration: saving
and loading, the Inspector, undo, `self.get_component(...)` in scripts, Ask Aven, Explain and
the Error Doctor. A component that needs custom data (a Tilemap's tiles, a Script's overrides)
adds `saveExtra`/`loadExtra` functions and lists their keys in `extraKeys`.

### Scripting

EasyScript compiles to bytecode for a small VM (`script/vm.cpp`). Functions that call `wait()`
become tasks the VM resumes on later frames. The engine exposes itself through native functions
in `runtime/script_system.cpp`:
- globals such as `spawn` and `key_down`;
- `self` methods and properties, which are looked up by name through the same
  `getProperty`/`setProperty` the tweens and the C API use.

Blocks compile to EasyScript text, so they run on the same VM.

Native modules (`runtime/native.cpp`) are shared libraries loaded from a project's
`native/bin`. The C API table (`sdk/include/aven.h`) forwards to the same built-ins EasyScript
uses, so the languages behave the same.

### Determinism and Bug Replay

A game started with a seed, the same scene and the same inputs plays out identically. The
editor records each frame's input and delta time (`runtime/replay.cpp`), which is what Bug Replay
plays back.

## The editor (`editor/src/`)

One `Editor` class, split into files by area:

| File | Area |
|---|---|
| `editor.cpp` | project and scene management, play mode, undo, command-line automation |
| `chrome.cpp` | menus, toolbar, docking layouts, shortcuts |
| `panels.cpp` | Hierarchy, Inspector, Assets, Console, script tabs, settings, Reference |
| `viewport.cpp` | the scene view: cameras, gizmos, picking, overlays |
| `hub.cpp` | the project hub and templates |
| `code_editor.cpp`, `block_editor.cpp` | the code and block editors |
| `learn_mode.cpp` | editor levels and the lessons panel |
| `assistant.cpp`, `explain.cpp`, `doctor.cpp` | Ask Aven, Explain, and the Error Doctor |
| `recipes.cpp`, `code_ladder.cpp`, `play_edit.cpp`, `bug_replay.cpp`, `share.cpp`, `quests.cpp` | the other beginner features |
| `pixel_editor.cpp`, `sprite_sheet.cpp`, `tile_painter.cpp`, `sound_maker.cpp`, `particle_tools.cpp`, `capture.cpp` | creator tools |
| `native_code.cpp` | the native (C/C++) module window and build |

Things contributors can change without C++ live in `editor/data/`: the Error Doctor's
explanations, and the words Ask Aven understands.

### Testing the editor

The editor can run itself for tests and screenshots:

```
aven-editor path/to/game --screenshot out.png --frames 30 --panel doctor,recipes --select Player
```

`--panel` accepts the commands handled in `Editor::openPanels` (`editor.cpp`). A command written
`@N:command` runs on frame N. Headless machines can use `xvfb-run`.

## Platform support

- **Graphics:** a single backend, OpenGL 3.3 on the desktop and WebGL2 in browsers, behind
  `rhi.h`. Other backends (Vulkan, Metal, Direct3D) would implement the same interface. See the
  [roadmap](roadmap.md).
- **Web:** built with Emscripten (`tools/web/build_web_player.sh`). 3D physics runs
  single-threaded, and native modules aren't available.
