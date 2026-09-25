# EasyScript

EasyScript is Aven's scripting language. It reads like Python: indentation groups lines, `def`
makes a function, and there are no semicolons or type names. Each script belongs to one object
(`self`). The engine calls its events, such as `on_update(dt)`, for you.

This guide teaches the language. For every function and property, see the
[EasyScript API](easyscript-api.md), or the editor's **Reference** button.

> Every example on this page is compiled by the test suite, so they're known to be valid.

## A first script

```easyscript
# Spins the object around. Put it on any object with Add Component > Script.

speed = 90  # degrees per second (shows up in the Inspector)

def on_update(dt):
    self.angle += speed * dt
```

- Lines starting with `#` are comments.
- `speed = 90` at the top is a **variable** of this object. Top-level variables that don't start
  with `_` appear in the Inspector, so you can give each object its own speed without touching
  the code. Start a name with `_` to keep it private.
- `def on_update(dt):` is an **event**. Aven runs it every frame. `dt` is the time since the last
  frame in seconds, so `speed * dt` is "90 degrees per second" whatever the frame rate.

## Values

```easyscript
lives = 3                # a number (whole or decimal, it doesn't matter)
name = "Hero"            # text
alive = True             # True or False
target = None            # nothing yet
items = ["key", "map"]   # a list
stats = {"hp": 10}       # a dictionary: names to values
spot = vec(2, 3)         # a vector (x, y) or (x, y, z)
tint = rgb(255, 128, 0)  # a color

def on_start():
    print(f"{name} has {lives} lives and {len(items)} items")
    print(f"hp: {stats['hp']}, position: {spot.x}, {spot.y}")
```

`f"..."` strings fill in the values between `{ }`. Use `{score:.2f}` for two decimal places.

## Deciding and repeating

```easyscript
health = 10

def on_update(dt):
    if health <= 0:
        self.destroy()
    elif health < 3:
        self.color = "#ff4444"
    else:
        self.color = "#ffffff"

def on_start():
    for i in range(3):                # 0, 1, 2
        spawn("prefabs/coin.prefab", i * 2, 4)
    for enemy in find_all("enemy"):   # every object tagged "enemy"
        enemy.visible = True
    count = 0
    while count < 5:
        count += 1
```

Comparisons are `==  !=  <  >  <=  >=`. Combine them with `and`, `or` and `not`. Use `break` to
leave a loop early and `continue` to skip to the next time around.

## Your own functions

```easyscript
def jump_height(power):
    return power * 1.5

def on_key_pressed(key):
    if key == "space":
        self.velocity_y = jump_height(8)
```

## The object: `self`

`self` is the object the script is on, and other objects work the same way:

```easyscript
def on_update(dt):
    self.x += 2 * dt                 # position: x, y, z (or self.position)
    self.scale_x = 1.5               # size
    self.alpha = 0.5                 # see-through
    player = find("Player")          # another object, by name
    if player and self.distance_to(player) < 3:
        self.look_at(player)
```

Objects can do things: `self.destroy()`, `self.hide()`, `self.show()`,
`self.play_animation(0, 3)`, `self.apply_impulse(0, 5)`, `self.move_toward(player, 2 * dt)`
and more. The full list is in the [API](easyscript-api.md#self-actions).

## Events

| Event | When it runs |
|---|---|
| `on_start()` | Once, when the object appears |
| `on_update(dt)` | Every frame |
| `on_fixed_update(dt)` | 60 times a second, in step with physics |
| `on_collide(other)` / `on_collide_end(other)` | Bumping into something, and stopping |
| `on_trigger(other)` / `on_trigger_exit(other)` | Something entered or left this trigger |
| `on_click()` | The player clicked this object or button |
| `on_key_pressed(key)` | Any key was pressed (`key` is its name, like `"space"`) |
| `on_message(message, data)` | Someone called `broadcast("message", data)` |
| `on_destroy()` | Just before the object is removed |

```easyscript
def on_trigger(other):
    if other.tag == "player":
        game.coins += 1
        play_sound("sounds/coin.wav")
        self.destroy()
```

## Sharing values: `game`

Anything you put on `game` is shared by every script, and it survives changing scenes:

```easyscript
def on_start():
    game.score = 0

def on_click():
    game.score += 10
    if game.score >= 100:
        load_scene("scenes/win.scene")
```

## Talking to other objects

`broadcast` tells every script. `send` calls one object's function by name:

```easyscript
def on_trigger(other):
    if other.tag == "player":
        other.send("hurt", 1)      # runs def hurt(amount) in the player's script
        broadcast("player_hit")    # every on_message(message, data) hears it

def on_message(message, data):
    if message == "game_over":
        self.hide()
```

## Waiting and timers

`wait(seconds)` pauses just this function. The rest of the game keeps running:

```easyscript
def on_start():
    self.text = "Ready..."
    wait(1)
    self.text = "Go!"
    wait(0.5)
    self.hide()

def blink():
    self.visible = not self.visible

def on_click():
    every(0.2, blink)                 # runs blink five times a second
    after(3, self.destroy)            # and removes the object in 3 seconds
```

## Input

```easyscript
speed = 5

def on_update(dt):
    self.x += axis("horizontal") * speed * dt   # arrow keys or A/D: -1, 0 or 1
    if key_pressed("space"):
        self.velocity_y = 10
    if mouse_pressed():
        spawn("prefabs/spark.prefab", mouse_x(), mouse_y())
```

## Mistakes

When a script has an error, Aven pauses the game and the **Error Doctor** explains it: what went
wrong, on which line, and usually a one-click fix. Common ones:

- **Indentation:** the lines inside `if`, `for`, `while` and `def` must be indented the same
  amount. The editor does it for you when you press Enter after `:`.
- **Names:** `Speed` and `speed` are different names.
- **Text vs numbers:** `"Score: " + 5` doesn't work; use `f"Score: {5}"` or `str(5)`.

## Where next

- The **Code Ladder** (in any script tab) shows your script in C, C#, GDScript, Luau and C++.
- [Native code](native-code.md) moves a behavior to C when you want full speed, or to practice C.
