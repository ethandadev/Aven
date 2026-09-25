#include "aven/blocks/blocks.h"

namespace aven::blocks {

namespace {

Input num(const char* name, const char* def = "0") { return {name, InputType::Number, def, {}}; }
Input text(const char* name, const char* def = "") { return {name, InputType::Text, def, {}}; }
Input cond(const char* name) { return {name, InputType::Condition, "", {}}; }
Input value(const char* name, const char* def = "0") { return {name, InputType::Value, def, {}}; }
Input key(const char* name = "key", const char* def = "space") { return {name, InputType::Key, def, {}}; }
Input choice(const char* name, std::vector<std::string> options) {
    std::string def = options.empty() ? "" : options[0];
    return {name, InputType::Choice, def, std::move(options)};
}
Input color(const char* name, const char* def = "red") { return {name, InputType::Color, def, {}}; }
Input variable(const char* name = "var") { return {name, InputType::Variable, "my_variable", {}}; }
Input ident(const char* name, const char* def) { return {name, InputType::Name, def, {}}; }
// A dropdown whose choice is written into the code as a name, not as text.
Input identChoice(const char* name, std::vector<std::string> options) {
    std::string def = options.empty() ? "" : options[0];
    return {name, InputType::Name, def, std::move(options)};
}
Input asset(const char* name, InputType t, const char* def = "") { return {name, t, def, {}}; }

std::vector<BlockDef> build() {
    std::vector<BlockDef> b;
    auto add = [&](BlockDef d) { b.push_back(std::move(d)); };
    using S = Shape;

    // ---------------------------------------------------------------- Events
    add({"when_start", "Events", S::Hat, "when game starts", "", "on_start()", "not is_clone", {},
         "Runs once when the game (or scene) starts."});
    add({"when_update", "Events", S::Hat, "every frame", "", "on_update(dt)", "", {},
         "Runs about 60 times per second. Use it for movement and checking things."});
    add({"when_key", "Events", S::Hat, "when {key} key pressed", "", "on_key_pressed(key)", "key == {key}", {key()},
         "Runs when a key is pressed down."});
    add({"when_clicked", "Events", S::Hat, "when this object is clicked", "", "on_click()", "", {},
         "Runs when the player clicks or taps this object."});
    add({"when_touch", "Events", S::Hat, "when I touch {tag}", "", "on_collide(other)",
         "other.tag == {tag} or other.name == {tag}", {text("tag", "anything")},
         "Runs when this object bumps into another. Needs a collider (and a RigidBody on one of them)."});
    add({"when_receive", "Events", S::Hat, "when I receive {message}", "", "on_message(message, data)",
         "message == {message}", {text("message", "go")}, "Runs when any script broadcasts this message."});
    add({"when_clone", "Events", S::Hat, "when I start as a clone", "", "on_start()", "is_clone", {},
         "Runs in each copy made with 'create a clone of myself'."});
    add({"when_timer", "Events", S::Hat, "every {seconds} seconds", "", "@timer", "", {num("seconds", "1")},
         "Runs over and over with a pause in between."});
    add({"when_destroyed", "Events", S::Hat, "when I am destroyed", "", "on_destroy()", "", {},
         "Runs right before this object disappears."});

    // ---------------------------------------------------------------- Motion
    add({"move_steps", "Motion", S::Statement, "move {steps} steps", "self.move_forward({steps} / 10)", "", "",
         {num("steps", "10")}, "Moves in the direction the object is facing (10 steps = 1 unit)."});
    add({"move_by", "Motion", S::Statement, "move by x: {dx} y: {dy}", "self.move({dx}, {dy})", "", "",
         {num("dx", "1"), num("dy", "0")}, "Moves by an amount left/right (x) and up/down (y)."});
    add({"arrow_move", "Motion", S::Statement, "move with arrow keys at speed {speed}",
         "self.x += axis(\"horizontal\") * {speed} * delta_time()\nself.y += axis(\"vertical\") * {speed} * delta_time()",
         "", "", {num("speed", "5")}, "Arrow keys or WASD move this object. Put it inside 'every frame'."});
    add({"go_to", "Motion", S::Statement, "go to x: {x} y: {y}", "self.x = {x}\nself.y = {y}", "", "",
         {num("x"), num("y")}, "Jumps to a position."});
    add({"go_to_object", "Motion", S::Statement, "go to {target}", "self.world_position = find({target}).world_position",
         "", "", {text("target", "Player")}, "Jumps to another object's position."});
    add({"go_to_mouse", "Motion", S::Statement, "go to mouse", "self.world_position = mouse_position()", "", "", {},
         "Jumps to the mouse pointer."});
    add({"glide_to", "Motion", S::Statement, "glide to x: {x} y: {y} in {seconds} secs",
         "self.tween(\"x\", {x}, {seconds}, \"ease_in_out\")\nself.tween(\"y\", {y}, {seconds}, \"ease_in_out\")\nwait({seconds})",
         "", "", {num("x"), num("y"), num("seconds", "1")}, "Slides smoothly to a position."});
    add({"set_x", "Motion", S::Statement, "set x to {x}", "self.x = {x}", "", "", {num("x")}, ""});
    add({"set_y", "Motion", S::Statement, "set y to {y}", "self.y = {y}", "", "", {num("y")}, ""});
    add({"change_x", "Motion", S::Statement, "change x by {dx}", "self.x += {dx}", "", "", {num("dx", "1")}, ""});
    add({"change_y", "Motion", S::Statement, "change y by {dy}", "self.y += {dy}", "", "", {num("dy", "1")}, ""});
    add({"turn", "Motion", S::Statement, "turn {degrees} degrees", "self.turn({degrees})", "", "", {num("degrees", "15")},
         "Spins the object (positive = counter-clockwise)."});
    add({"point_direction", "Motion", S::Statement, "point in direction {angle}", "self.angle = {angle}", "", "",
         {num("angle", "90")}, "0 faces right, 90 faces up."});
    add({"point_towards", "Motion", S::Statement, "point towards {target}", "self.look_at(find({target}))", "", "",
         {text("target", "Player")}, ""});
    add({"point_mouse", "Motion", S::Statement, "point towards mouse", "self.look_at(mouse_position())", "", "", {}, ""});
    add({"move_toward_object", "Motion", S::Statement, "move toward {target} at speed {speed}",
         "self.move_toward(find({target}), {speed} * delta_time())", "", "", {text("target", "Player"), num("speed", "3")},
         "Chases another object. Put it inside 'every frame'."});

    // ---------------------------------------------------------------- Looks
    add({"say", "Looks", S::Statement, "say {text} for {seconds} seconds", "self.say({text}, {seconds})", "", "",
         {text("text", "Hello!"), num("seconds", "2")}, "Shows a speech bubble above the object."});
    add({"show", "Looks", S::Statement, "show", "self.show()", "", "", {}, ""});
    add({"hide", "Looks", S::Statement, "hide", "self.hide()", "", "", {}, ""});
    add({"set_color", "Looks", S::Statement, "set color to {color}", "self.color = {color}", "", "", {color("color")}, ""});
    add({"set_size", "Looks", S::Statement, "set size to {size}", "self.scale = {size}", "", "", {num("size", "1")},
         "1 is normal size, 2 is double."});
    add({"change_size", "Looks", S::Statement, "change size by {amount}",
         "self.scale_x += {amount}\nself.scale_y += {amount}", "", "", {num("amount", "0.1")}, ""});
    add({"set_image", "Looks", S::Statement, "switch image to {image}", "self.image = {image}", "", "",
         {asset("image", InputType::Image)}, ""});
    add({"set_shape", "Looks", S::Statement, "change shape to {shape}", "self.shape = {shape}", "", "",
         {choice("shape", {"square", "circle", "triangle", "rounded_square", "diamond", "star", "heart"})}, ""});
    add({"next_frame", "Looks", S::Statement, "next frame", "self.frame += 1", "", "", {}, "Shows the next picture of a sprite sheet."});
    add({"play_frames", "Looks", S::Statement, "play frames {first} to {last} at {fps} fps",
         "self.play_animation({first}, {last}, {fps})", "", "", {num("first", "0"), num("last", "3"), num("fps", "10")}, ""});
    add({"set_text", "Looks", S::Statement, "set text to {text}", "self.text = {text}", "", "", {value("text", "\"Hi\"")},
         "Changes the words on a text object or button."});
    add({"fade_to", "Looks", S::Statement, "fade to {alpha} in {seconds} secs", "self.tween(\"alpha\", {alpha}, {seconds})",
         "", "", {num("alpha", "0"), num("seconds", "1")}, "0 is invisible, 1 is solid."});
    add({"flip", "Looks", S::Statement, "face {side}", "self.flip_x = {side} == \"left\"", "", "",
         {choice("side", {"left", "right"})}, "Mirrors the image to face left or right."});
    add({"set_layer", "Looks", S::Statement, "set layer to {order}", "self.order = {order}", "", "", {num("order", "1")},
         "Higher layers are drawn on top."});
    add({"emit", "Looks", S::Statement, "burst {count} particles", "self.emit({count})", "", "", {num("count", "20")},
         "Needs a ParticleEmitter on this object."});
    add({"shake", "Looks", S::Statement, "shake the camera {amount}", "camera_shake({amount}, 0.3)", "", "",
         {num("amount", "0.3")}, ""});

    // ---------------------------------------------------------------- Sound
    add({"play_sound", "Sound", S::Statement, "play sound {sound}", "play_sound({sound})", "", "",
         {asset("sound", InputType::Sound)}, ""});
    add({"play_music", "Sound", S::Statement, "play music {music}", "play_music({music})", "", "",
         {asset("music", InputType::Sound)}, "Loops in the background, even across scenes."});
    add({"stop_music", "Sound", S::Statement, "stop music", "stop_music()", "", "", {}, ""});
    add({"set_volume", "Sound", S::Statement, "set volume to {volume} %", "set_volume({volume} / 100)", "", "",
         {num("volume", "100")}, ""});

    // ---------------------------------------------------------------- Control
    add({"wait", "Control", S::Statement, "wait {seconds} seconds", "wait({seconds})", "", "", {num("seconds", "1")}, ""});
    add({"repeat", "Control", S::CBlock, "repeat {times}", "for _i in range(int({times})):", "", "", {num("times", "10")},
         "", true});
    add({"forever", "Control", S::CBlock, "forever", "while True:", "", "", {}, "", true});
    add({"if", "Control", S::CBlock, "if {condition} then", "if {condition}:", "", "", {cond("condition")}, ""});
    add({"if_else", "Control", S::IfElse, "if {condition} then", "if {condition}:", "", "", {cond("condition")}, ""});
    add({"repeat_until", "Control", S::CBlock, "repeat until {condition}", "while not {condition}:", "", "",
         {cond("condition")}, "", true});
    add({"wait_until", "Control", S::Statement, "wait until {condition}", "while not {condition}:\n    wait()", "", "",
         {cond("condition")}, ""});
    add({"stop_script", "Control", S::Statement, "stop this script", "return", "", "", {}, ""});
    add({"clone", "Control", S::Statement, "create a clone of myself", "self.clone()", "", "", {}, ""});
    add({"destroy", "Control", S::Statement, "delete this object", "self.destroy()\nreturn", "", "", {}, ""});
    add({"spawn", "Control", S::Statement, "spawn {prefab} at x: {x} y: {y}", "spawn({prefab}, {x}, {y})", "", "",
         {asset("prefab", InputType::Prefab), num("x"), num("y")}, "Creates a copy of a saved prefab."});
    add({"broadcast", "Control", S::Statement, "broadcast {message}", "broadcast({message})", "", "",
         {text("message", "go")}, "Every 'when I receive' block with this message will run."});
    add({"load_scene", "Control", S::Statement, "go to scene {scene}", "load_scene({scene})", "", "",
         {asset("scene", InputType::Scene)}, ""});
    add({"restart", "Control", S::Statement, "restart the scene", "restart_scene()", "", "", {}, ""});

    // ---------------------------------------------------------------- Sensing
    add({"key_down", "Sensing", S::Boolean, "key {key} pressed?", "key_down({key})", "", "", {key()}, ""});
    add({"mouse_down", "Sensing", S::Boolean, "mouse down?", "mouse_down()", "", "", {}, ""});
    add({"touching", "Sensing", S::Boolean, "touching {tag}?", "self.is_touching({tag})", "", "", {text("tag", "enemy")},
         "Checks a tag or an object name."});
    add({"on_ground", "Sensing", S::Boolean, "on the ground?", "self.on_ground", "", "", {}, "Needs a RigidBody2D."});
    add({"mouse_x", "Sensing", S::Reporter, "mouse x", "mouse_x()", "", "", {}, ""});
    add({"mouse_y", "Sensing", S::Reporter, "mouse y", "mouse_y()", "", "", {}, ""});
    add({"my_property", "Sensing", S::Reporter, "my {property}", "self.{property}", "", "",
         {identChoice("property", {"x", "y", "angle", "scale_x", "scale_y", "alpha", "velocity_x", "velocity_y", "frame", "name", "tag"})},
         ""});
    add({"distance_to", "Sensing", S::Reporter, "distance to {target}", "self.distance_to(find({target}))", "", "",
         {text("target", "Player")}, ""});
    add({"time", "Sensing", S::Reporter, "time", "time()", "", "", {}, "Seconds since the scene started."});
    add({"delta_time", "Sensing", S::Reporter, "frame time", "delta_time()", "", "", {},
         "Seconds since the last frame; multiply speeds by this."});
    add({"axis", "Sensing", S::Reporter, "{axis} input", "axis({axis})", "", "", {choice("axis", {"horizontal", "vertical"})},
         "-1, 0 or 1 from arrow keys, WASD or a gamepad."});
    add({"count", "Sensing", S::Reporter, "number of {tag}", "count({tag})", "", "", {text("tag", "enemy")}, ""});

    // ---------------------------------------------------------------- Operators
    auto binop = [&](const char* id, const char* op) {
        add({id, "Operators", S::Reporter, std::string("{a} ") + op + " {b}", std::string("({a} ") + op + " {b})", "", "",
             {value("a", "0"), value("b", "0")}, ""});
    };
    binop("add", "+");
    binop("subtract", "-");
    binop("multiply", "*");
    binop("divide", "/");
    binop("modulo", "%");
    auto cmp = [&](const char* id, const char* op, const char* code) {
        add({id, "Operators", S::Boolean, std::string("{a} ") + op + " {b}", std::string("({a} ") + code + " {b})", "", "",
             {value("a", "0"), value("b", "50")}, ""});
    };
    cmp("less", "<", "<");
    cmp("greater", ">", ">");
    cmp("equals", "=", "==");
    cmp("not_equals", "≠", "!=");
    add({"and", "Operators", S::Boolean, "{a} and {b}", "({a} and {b})", "", "", {cond("a"), cond("b")}, ""});
    add({"or", "Operators", S::Boolean, "{a} or {b}", "({a} or {b})", "", "", {cond("a"), cond("b")}, ""});
    add({"not", "Operators", S::Boolean, "not {a}", "(not {a})", "", "", {cond("a")}, ""});
    add({"random", "Operators", S::Reporter, "pick random {min} to {max}", "random_int({min}, {max})", "", "",
         {num("min", "1"), num("max", "10")}, ""});
    add({"join", "Operators", S::Reporter, "join {a} {b}", "(str({a}) + str({b}))", "", "",
         {value("a", "\"Score: \""), value("b", "0")}, ""});
    add({"length", "Operators", S::Reporter, "length of {text}", "len(str({text}))", "", "", {value("text", "\"hello\"")}, ""});
    add({"math", "Operators", S::Reporter, "{func} of {x}", "{func}({x})", "", "",
         {identChoice("func", {"abs", "round", "floor", "ceil", "sqrt", "sin", "cos"}), num("x", "9")}, ""});

    // ---------------------------------------------------------------- Variables
    add({"set_var", "Variables", S::Statement, "set {var} to {value}", "{var} = {value}", "", "", {variable(), value("value", "0")}, ""});
    add({"change_var", "Variables", S::Statement, "change {var} by {value}", "{var} += {value}", "", "",
         {variable(), value("value", "1")}, ""});
    add({"var", "Variables", S::Reporter, "{var}", "{var}", "", "", {variable()}, ""});
    add({"set_game", "Variables", S::Statement, "set game {name} to {value}", "game.{name} = {value}", "", "",
         {ident("name", "score"), value("value", "0")}, "Game variables are shared by every object and scene."});
    add({"change_game", "Variables", S::Statement, "change game {name} by {value}", "game.{name} += {value}", "", "",
         {ident("name", "score"), value("value", "1")}, ""});
    add({"game_var", "Variables", S::Reporter, "game {name}", "game.{name}", "", "", {ident("name", "score")}, ""});
    add({"print", "Variables", S::Statement, "print {value}", "print({value})", "", "", {value("value", "\"Hello\"")},
         "Writes a message in the Console panel."});
    add({"save", "Variables", S::Statement, "save {value} as {key}", "save_data({key}, {value})", "", "",
         {value("value", "0"), text("key", "high_score")}, "Remembers a value even after the game closes."});
    add({"load", "Variables", S::Reporter, "saved {key} or {default}", "load_data({key}, {default})", "", "",
         {text("key", "high_score"), value("default", "0")}, ""});

    // ---------------------------------------------------------------- Physics
    add({"set_velocity", "Physics", S::Statement, "set velocity to x: {vx} y: {vy}", "self.velocity = vec({vx}, {vy})", "",
         "", {num("vx", "0"), num("vy", "0")}, "Needs a RigidBody2D."});
    add({"set_velocity_x", "Physics", S::Statement, "set x velocity to {vx}", "self.velocity_x = {vx}", "", "",
         {num("vx", "5")}, ""});
    add({"set_velocity_y", "Physics", S::Statement, "set y velocity to {vy}", "self.velocity_y = {vy}", "", "",
         {num("vy", "5")}, ""});
    add({"jump", "Physics", S::Statement, "jump with power {power}", "self.velocity_y = {power}", "", "", {num("power", "8")},
         ""});
    add({"push", "Physics", S::Statement, "push x: {fx} y: {fy}", "self.apply_impulse({fx}, {fy})", "", "",
         {num("fx", "0"), num("fy", "5")}, ""});
    add({"set_gravity", "Physics", S::Statement, "set gravity to {g}", "set_gravity(0, {g})", "", "", {num("g", "-9.8")}, ""});
    return b;
}

} // namespace

const std::vector<BlockDef>& catalog() {
    static const std::vector<BlockDef> defs = build();
    return defs;
}

const std::vector<Category>& categories() {
    static const std::vector<Category> cats = {
        {"Events", Color::fromHex(0xF2B01E)},  {"Motion", Color::fromHex(0x4C8BF5)},
        {"Looks", Color::fromHex(0x9966FF)},   {"Sound", Color::fromHex(0xCF63CF)},
        {"Control", Color::fromHex(0xF59E0B)}, {"Sensing", Color::fromHex(0x38BDF8)},
        {"Operators", Color::fromHex(0x59C059)}, {"Variables", Color::fromHex(0xFF8C1A)},
        {"Physics", Color::fromHex(0xEF4444)},
    };
    return cats;
}

const BlockDef* find(const std::string& id) {
    for (auto& d : catalog())
        if (d.id == id)
            return &d;
    return nullptr;
}

Color categoryColor(const std::string& category) {
    for (auto& c : categories())
        if (c.name == category)
            return c.color;
    return Color::fromHex(0x888888);
}

} // namespace aven::blocks
