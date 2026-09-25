# EasyScript API

Everything scripts can use, generated from the engine itself (the same list as the editor's Scripting Reference). Blocks turn into these calls, and the C API (sdk/include/aven.h) uses `aven_` plus the same names.

## Basics

- `abs(x)`
- `acos(x)`
- `asin(x)`
- `atan(x)`
- `atan2(y, x)`
- `bool(value)`
- `ceil(x)`
- `clamp(value, low, high)`
- `cos(x)`
- `degrees(x)`
- `dict()`
- `exp(x)`
- `float(value)`
- `floor(x)`
- `game.anything = value  (shared by all scripts and scenes)`
- `get_game("score", 0)`
- `int(value)`
- `len(list_or_text)`
- `lerp(a, b, t)`
- `list(things)`
- `log(x)`
- `max(a, b, ...) or max(my_list)`
- `min(a, b, ...) or min(my_list)`
- `move_toward(current, target, step)`
- `pi`
- `pow(x, power)`
- `print(value, ...)`
- `radians(x)`
- `range(stop) or range(start, stop) or range(start, stop, step)`
- `reversed(my_list)`
- `round(x) or round(x, digits)`
- `sign(x)`
- `sin(x)`
- `sorted(my_list, key=None, reverse=False)`
- `sqrt(x)`
- `str(value)`
- `sum(my_list)`
- `tan(x)`
- `type(value)`

## Time

- `after(seconds, function)`
- `delta_time()`
- `every(seconds, function)`
- `set_time_scale(0.5)`
- `start_task(function, values...)`
- `stop_timer(timer)`
- `time()`
- `wait(seconds)`

## Input

- `axis("horizontal") or axis("vertical")`
- `key_down("space")`
- `key_pressed("space")`
- `key_released("space")`
- `mouse_delta()`
- `mouse_down("left")`
- `mouse_position()`
- `mouse_pressed("left")`
- `mouse_released("left")`
- `mouse_screen_position()`
- `mouse_scroll()`
- `mouse_x()`
- `mouse_y()`

## Events

- `broadcast("message", data=None)`

## Camera

- `camera()`
- `camera_shake(amount=0.3, seconds=0.3)`

## Random

- `choice(my_list)`
- `random()`
- `random_int(low, high)`
- `random_range(low, high)`
- `random_seed(number)`
- `shuffle(my_list)`

## Vectors & colors

- `color("red") or color("#ff8800")`
- `hsv(hue 0-360, saturation 0-1, value 0-1)`
- `rgb(red, green, blue) with values from 0 to 255`
- `vec(x, y) or vec(x, y, z)`

## Objects

- `count("enemy")`
- `create_sprite("circle", x, y, size=1, color="white")`
- `create_text("Hello", x, y, size=0.5)`
- `destroy(obj)`
- `find("Player")`
- `find_all("enemy")`
- `spawn("prefabs/coin.prefab", x, y)`

## Saving

- `delete_data("high_score")`
- `has_data("high_score")`
- `load_data("high_score", 0)`
- `save_data("high_score", 100)`

## Math

- `direction(from, to)`
- `distance(a, b)`

## Screen

- `is_fullscreen()`
- `lock_mouse(True)`
- `screen_height()`
- `screen_width()`
- `set_fullscreen(True)`

## Scenes

- `is_paused()`
- `load_scene("scenes/level2.scene")`
- `pause_game()`
- `quit()`
- `restart_scene()`
- `resume_game()`

## Audio

- `play_music("music/theme.ogg", volume=1, loop=True)`
- `play_sound("sounds/jump.wav", volume=1, pitch=1)`
- `set_volume(0.5)`
- `stop_music()`

## Physics

- `raycast(from, to)`
- `set_gravity(x, y) or set_gravity(x, y, z)`

## Animation

- `tween(obj, "property", target, seconds, "ease_out")`

## self properties

- `self.name`
- `self.tag`
- `self.active`
- `self.visible`
- `self.id`
- `self.exists`
- `self.x`
- `self.y`
- `self.z`
- `self.position`
- `self.world_x`
- `self.world_y`
- `self.world_z`
- `self.world_position`
- `self.angle`
- `self.rotation`
- `self.rotation_x`
- `self.rotation_y`
- `self.rotation_z`
- `self.scale`
- `self.scale_x`
- `self.scale_y`
- `self.scale_z`
- `self.size`
- `self.width`
- `self.height`
- `self.color`
- `self.alpha`
- `self.text`
- `self.image`
- `self.shape`
- `self.frame`
- `self.flip_x`
- `self.flip_y`
- `self.order`
- `self.velocity`
- `self.velocity_x`
- `self.velocity_y`
- `self.velocity_z`
- `self.on_ground`
- `self.parent`
- `self.children`
- `self.forward`
- `self.right`
- `self.up`
- `self.is_clone`

## self actions

- `self.destroy(...)`
- `self.damage(...)`
- `self.heal(...)`
- `self.move(...)`
- `self.move_forward(...)`
- `self.turn(...)`
- `self.look_at(...)`
- `self.move_toward(...)`
- `self.distance_to(...)`
- `self.direction_to(...)`
- `self.is_touching(...)`
- `self.apply_force(...)`
- `self.apply_impulse(...)`
- `self.play_animation(...)`
- `self.set_tile(...)`
- `self.get_tile(...)`
- `self.set_tile_at(...)`
- `self.get_tile_at(...)`
- `self.stop_animation(...)`
- `self.tween(...)`
- `self.clone(...)`
- `self.hide(...)`
- `self.show(...)`
- `self.get_component(...)`
- `self.add_component(...)`
- `self.has_component(...)`
- `self.remove_component(...)`
- `self.find_child(...)`
- `self.play_sound(...)`
- `self.send(...)`
- `self.say(...)`
- `self.emit(...)`

## Events you can write

- `def on_start():  runs once when the object starts`
- `def on_update(dt):  runs every frame`
- `def on_fixed_update(dt):  runs at a steady 60 times per second (physics)`
- `def on_collide(other):  bumped into another object`
- `def on_collide_end(other):  stopped touching`
- `def on_trigger(other):  something entered this trigger`
- `def on_trigger_exit(other):  something left this trigger`
- `def on_click():  the player clicked this object or button`
- `def on_key_pressed(key):  any key was pressed`
- `def on_message(message, data):  a broadcast was sent`
- `def on_destroy():  about to be removed`
