# Native code

This folder holds C and C++ code for your game. It is the advanced tier: when an idea is too
slow in EasyScript, or you want to practise the language most engines are built in.

- `src/` - your code. Every `.c` and `.cpp` file here becomes its own library (shared code goes in `src/common/`).
- `include/aven.h` - everything your code can ask Aven to do. The names match EasyScript.
- `bin/` - the built library. Aven loads it from here (and ships it with desktop exports).
- `build/` - the compiler's working files. Safe to delete.

Build with the **Build** button in Tools > Native Code. You need CMake and a C compiler:

- Windows: Visual Studio (the free Community edition, "Desktop development with C++") or the
  Build Tools for Visual Studio, plus CMake.
- macOS: `xcode-select --install`, plus CMake (`brew install cmake`).
- Linux: `gcc` or `clang` and `cmake` from your package manager.

Then add a NativeScript component to an object and pick a behavior. Numbers you declare with
`aven_number()` show up in the Inspector.

Native code runs at full speed but isn't sandboxed: a bug such as reading past the end of an
array can crash the game (and the editor while playing). Save often. Native modules don't run in
web builds; keep web games in EasyScript or blocks.
