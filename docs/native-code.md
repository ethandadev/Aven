# Native code (C and C++)

Native code is Aven's advanced tier. It moves part of your game into C or C++: for speed, or to
practise the language most engines are written in. It runs next to EasyScript and blocks. An
object with a **NativeScript** component behaves just like one with a Script: same events, same
properties, same game values.

## The quickest way in: the Code Ladder

Open any EasyScript or blocks script and press **Code Ladder**, then pick the **C (Aven)** rung.
It shows your script as a complete C module. **Save to native/src and build** puts it in your
project and compiles it. Then swap the object's Script for a NativeScript with the same behavior
name.

The translation is designed to compile as it is. Features C has no direct match for, like lists
or `wait()`, become marked comments, and the notes under the code explain each one.

## Setting up

Open **Tools > Native Code (C/C++)** and press **Create a native module**. Aven adds a `native/`
folder to your project:

```
native/
  CMakeLists.txt     builds every file in src/ into its own library in bin/
  include/aven.h     the C API (kept up to date by Aven)
  src/behaviors.c    example behaviors: Spinner, Bobber, Collector
  src/common/        (optional) code shared by several files
  bin/               the built libraries, which Aven loads
  build/             the compiler's working files (safe to delete)
```

To build, you need CMake and a C compiler:

- **Windows:** Visual Studio Community with "Desktop development with C++".
- **macOS:** `xcode-select --install` and `brew install cmake`.
- **Linux:** gcc or clang, and cmake.

Press **Build** (Ctrl+B). Compiler errors appear in the Console; click one to jump to the line.
After a successful build, Aven reloads the libraries by itself. It also picks up libraries you
build outside the editor.

## A behavior

```c
#include "aven.h"

#include <math.h>

typedef struct {
    float height; /* set in the Inspector */
    float speed;
    float start_y; /* private: remembered by each object */
    float time;
} Bobber;

static void bobber_start(AvenEntity self, void* data) {
    Bobber* b = (Bobber*)data;
    b->start_y = (float)aven_get(self, "y");
}

static void bobber_update(AvenEntity self, void* data, float dt) {
    Bobber* b = (Bobber*)data;
    b->time += dt;
    aven_set(self, "y", b->start_y + sinf(b->time * b->speed) * b->height);
}

static void setup(AvenModule* module) {
    AvenBehavior* b = aven_behavior(module, "Bobber", sizeof(Bobber));
    aven_number(b, "height", offsetof(Bobber, height), 0.25, "How far up and down it floats");
    aven_number(b, "speed", offsetof(Bobber, speed), 3, "How fast it floats");
    b->on_start = bobber_start;
    b->on_update = bobber_update;
}

AVEN_MODULE(setup)
```

- **Each object gets its own copy of the struct** (`data`). It starts zeroed, then the
  properties are filled in.
- **`aven_number`, `aven_integer` and `aven_flag`** make a field show up in the Inspector, like a
  top-level variable in EasyScript. Pass `offsetof` so Aven knows where the field is.
- **Events** are `on_start`, `on_update`, `on_fixed_update`, `on_collide`, `on_collide_end`,
  `on_trigger`, `on_trigger_exit`, `on_click`, `on_key_pressed`, `on_message` and `on_destroy`.
  Leave out the ones you don't need.
- **Each file ends with `AVEN_MODULE(setup)`.** One file can hold several behaviors.

## Talking to the game

The API uses EasyScript's names, so everything in the [EasyScript API](easyscript-api.md) has an
obvious C spelling:

| EasyScript | C |
|---|---|
| `self.x += 1` | `aven_set(self, "x", aven_get(self, "x") + 1);` |
| `self.text = "Hi"` | `aven_set_text(self, "text", "Hi");` |
| `if other.tag == "coin":` | `if (strcmp(aven_tag(other), "coin") == 0)` |
| `game.score += 10` | `aven_game_set("score", aven_game_get("score", 0) + 10);` |
| `find("Player")` | `aven_find("Player")` |
| `spawn("prefabs/coin.prefab", x, y)` | `aven_spawn("prefabs/coin.prefab", x, y, 0);` |
| `self.play_animation(0, 3)` | `aven_call(self, "play_animation", (const double[]){0, 3}, 2);` |
| `key_down("left")` | `aven_key_down("left")` |
| `play_sound("sounds/hit.wav")` | `aven_play_sound("sounds/hit.wav");` |
| `other.send("hurt", 1)` | `aven_send(other, "hurt", 1);` |
| `broadcast("win")` | `aven_send(0, "win", 0);` |
| `random_range(1, 6)` | `aven_random(1, 6)` |

- **Messages work both ways.** EasyScript's `other.send("hurt", 1)` reaches a C behavior's
  `on_message` as `("hurt", 1)`, and `aven_send` from C calls EasyScript's `def hurt(amount)`.
- **Mistakes get plain-words reports** in the Console, reported once each: a property name that
  doesn't exist, an object that was destroyed, a behavior name that isn't built.

Text returned by the API (`aven_name`, `aven_tag`, `aven_get_text`) stays valid for a while, but
copy it if you need to keep it.

The complete list is in `sdk/include/aven.h`, with comments.

## C++

`aven.h` works from C++ too, so `.cpp` files in `src/` are built the same way. See
`sdk/examples/patrol.cpp` for a behavior written as a C++ struct with methods.

## Other languages

`aven.h` is plain C with no dependencies: a table of function pointers (`AvenApi`) and one
exported function per library (`aven_module_entry`). Any language that can build a shared
library with C functions can bind to it: Rust, Zig, Odin, or C# with NativeAOT.

## Things to know

- **Native code isn't sandboxed.** A bug like reading past the end of an array can crash the game,
  and the editor while you're playing. Save often.
- **Libraries reload only while the game is stopped.** If you build during play, the new version
  loads when you stop.
- **Desktop exports include `native/bin`** (not your source). **Web builds can't run native
  code**; objects using it sit still in the browser.
- **The API is versioned** (`AVEN_API_VERSION`). Newer engines only add to the end of the table,
  so modules built today keep working.
