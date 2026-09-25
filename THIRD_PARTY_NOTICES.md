# Third-party notices

Aven is MIT licensed. It builds on these projects, which CMake downloads at the pinned versions
in `cmake/AvenDependencies.cmake` (glad is kept in `third_party/`). Games exported with Aven
include the player, so the libraries marked "player" ship with every game. Their licenses allow
that; keep this file (or equivalent credits) with your game.

| Project | Used for | Ships in | License |
|---|---|---|---|
| [GLFW](https://www.glfw.org) 3.4 | windows, input, gamepads | player, editor | zlib/libpng |
| [glad](https://github.com/Dav1dde/glad) | OpenGL function loading | player, editor | MIT (generated code); the OpenGL registry it's generated from is Apache 2.0 |
| [stb](https://github.com/nothings/stb) (stb_image, stb_image_write, stb_truetype) | images and fonts | player, editor | MIT or public domain (your choice) |
| [cgltf](https://github.com/jkuhlmann/cgltf) 1.15 | glTF 3D models | player, editor | MIT |
| [miniaudio](https://miniaud.io) 0.11.25 | sound and music | player, editor | MIT No Attribution or public domain (your choice) |
| [Box2D](https://box2d.org) 3.1.1 | 2D physics | player, editor | MIT |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) 5.2.0 | 3D physics | player, editor | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.91.9b (docking) | the editor's interface | editor | MIT |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | move/rotate/scale gizmos | editor | MIT |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) 1.8.0 | QR codes on game cards | editor | MIT |
| [gif-h](https://github.com/charlietangora/gif-h) | GIF recording | editor | public domain (Unlicense-style dedication) |
| [Roboto](https://fonts.google.com/specimen/Roboto) | the interface and default game font | player, editor | Apache 2.0 |
| [Cousine](https://fonts.google.com/specimen/Cousine) | the code font (embedded in the engine) | player, editor | Apache 2.0 |

For web builds, [Emscripten](https://emscripten.org) (MIT / University of Illinois NCSA) compiles
the player and adds its runtime.

Aven's own art is made by scripts in this repository, and is part of Aven, under the MIT License:
- the app icon (`tools/icon`);
- the template sprites and sounds (`tools/templates`);
- the starter tileset (`tools/art`).
