# Roadmap

Where Aven stands, including what's missing. If you'd like to help with any of it, see
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Built

- **2D and 3D runtime:**
  - 2D: sprites, sprite sheets and animation, tilemaps, text, particles, and Box2D physics.
  - 3D: a PBR renderer with shadows, sky and fog; glTF models with skeletal animation; Jolt
    physics and a character controller.
  - Shared: post-processing (bloom, tonemapping, grading, vignette, SSAO, FXAA), screen UI, 2D
    and 3D audio, save data, prefabs and scenes.
- **Coding:**
  - Behaviors, blocks and EasyScript.
  - Native C/C++ modules through a versioned C API.
  - The Code Ladder: C, C#, GDScript, Luau and C++.
- **Editor:**
  - Scene work: Hierarchy, Inspector, scene view with gizmos, assets, console, undo history,
    find, command palette and profiler.
  - Code and block editors.
  - Build and export for desktop, web and itch.io.
  - Themes, layouts, shortcuts and editor levels.
- **Creator tools:** Pixel Editor, Sprite Sheet, Tile Painter, Sound Maker, particle presets, and
  screenshot and GIF capture.
- **Beginner features:** game recipes, the Error Doctor, Learn mode, play-and-edit, Ask Aven,
  Explain, game cards with QR sharing on the local network, Bug Replay, and Contributor Quests.
- **Templates:** eight starter games.
- **Tests:** unit and integration tests covering:
  - the language, and translation to every rung (the C output is compiled);
  - physics, behaviors, tilemaps and native modules;
  - deterministic replay, and the examples in these docs.

## Known gaps

**Platforms**
- **One graphics backend:** OpenGL 3.3 on the desktop and WebGL2 on the web. The renderer goes
  through an abstraction (`rhi.h`), but Vulkan, Metal and Direct3D backends aren't written. On
  macOS, OpenGL is deprecated (it still works).
- **Tested on Linux and in the browser.** Windows and macOS builds are set up (CMake, icons,
  Winsock, `.dll`/`.dylib` loading) and built in CI, but haven't been play-tested by hand.
- **No phone or console exports.** Phones can play web builds, but there's no touch input yet:
  taps act as mouse clicks, so clicker-style games work and keyboard games don't.
- **No multiplayer or networking.**

**Sharing**
- **No hosted service.** Sharing gives you a web build, an itch.io zip, a game card and a link for
  your local network. A public link means uploading to a host such as itch.io or GitHub Pages
  yourself.

**Engine**
- **Animation:** sprite frames and glTF clips play, but there are no animation state machines,
  blend trees or IK.
- **Not built yet:** terrain, navigation meshes and pathfinding, LODs, occlusion culling, and
  baked lighting or global illumination.
- **Audio:** volume, pitch and 3D placement, but no mixer, buses or effects.

**Editor**
- **Assets:** read straight from the project folder, with no import pipeline, compression
  settings or asset bundles.
- **Missing tools:** visual shader and material graphs, and a timeline or cutscene editor.
- **Ask Aven** understands a fixed vocabulary (`editor/data/assistant_words.json`) rather than
  using a language model, so it knows what it knows and says when it doesn't.
- **English only**, in the editor and the docs.

**Coding**
- **EasyScript:** no classes and no importing one script from another. Objects share values
  through `game` and messages.
- **C API gaps:**
  - `aven_call` passes numbers only.
  - There's no way yet to get an object's parent or children, or to read other components.
  - Native code isn't sandboxed and doesn't run on the web.
- **Code Ladder output for other engines** is meant for reading and learning. It's close to
  working code, but it expects that engine's scene setup (components, tags, input actions).

## Next steps

1. Touch controls and on-screen buttons for web builds on phones.
2. Play-test and fix Windows and macOS builds, then ship installers.
3. `aven_find_child`, `aven_parent` and text arguments for `aven_call` in the C API (API
   version 2).
4. Pathfinding on tilemaps and navigation meshes.
5. An animation state machine, driven by blocks and EasyScript.
6. A Vulkan or Metal backend behind `rhi.h`.
7. Translations of the editor.
