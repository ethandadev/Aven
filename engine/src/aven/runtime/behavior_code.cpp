#include "aven/runtime/behavior_code.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace aven {

namespace {

std::string num(const Json& v, double fallback) {
    double d = v.isNumber() ? v.asNumber() : fallback;
    char buf[32];
    if (std::abs(d - std::round(d)) < 1e-9)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(d)));
    else
        std::snprintf(buf, sizeof buf, "%g", d);
    return buf;
}

std::string str(const Json& v, const std::string& fallback) {
    std::string s = v.isString() ? v.asString() : fallback;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

// Game variable names become code (game.coins), so keep them to simple identifiers.
std::string ident(const Json& v, const std::string& fallback) {
    std::string s = v.isString() && !v.asString().empty() ? v.asString() : fallback;
    std::string out;
    for (char c : s)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
        out = "_" + out;
    return out;
}

std::string lowerAxis(const Json& v) {
    std::string a = v.asString("X");
    return a == "Y" ? "y" : a == "Z" ? "z" : "x";
}

} // namespace

std::string behaviorAsEasyScript(const std::string& c, const Json& v) {
    if (c == "Patrol") {
        std::string axis = lowerAxis(v["axis"]);
        std::string code = "# Patrol: walks back and forth.\n\n"
                           "distance = " + num(v["distance"], 3) + "  # how far it goes to each side\n"
                           "speed = " + num(v["speed"], 2) + "\n\n"
                           "_start = 0\n_direction = 1\n\n"
                           "def on_start():\n    _start = self." + axis + "\n\n"
                           "def on_update(dt):\n"
                           "    if self." + axis + " > _start + distance:\n        _direction = -1\n"
                           "    elif self." + axis + " < _start - distance:\n        _direction = 1\n"
                           "    self." + axis + " += _direction * speed * dt\n";
        if (v["flip_sprite"].asBool(true) && axis == "x")
            code += "    self.flip_x = _direction < 0\n";
        return code;
    }
    if (c == "Chase") {
        bool away = v["run_away"].asBool(false);
        return "# Chase: moves toward the nearest " + v["target_tag"].asString("player") + (away ? " (or away from it).\n\n" : ".\n\n") +
               "target_tag = " + str(v["target_tag"], "player") + "\n"
               "speed = " + num(v["speed"], 3) + "\n"
               "sight = " + num(v["sight"], 6) + "  # only notices targets closer than this\n\n"
               "def on_update(dt):\n"
               "    target = None\n"
               "    closest = sight\n"
               "    for other in find_all(target_tag):\n"
               "        d = self.distance_to(other)\n"
               "        if d < closest:\n"
               "            closest = d\n"
               "            target = other\n"
               "    if target and closest > " + num(v["stop_distance"], 0.3) + ":\n"
               "        way = self.direction_to(target)\n"
               "        self.x += way.x * speed * dt" + (away ? " * -1" : "") + "\n"
               "        self.y += way.y * speed * dt" + (away ? " * -1" : "") + "\n"
               "        self.flip_x = (target.x < self.x) != " + (away ? "True" : "False") + "\n";
    }
    if (c == "Spin") {
        const Json& s = v["speed"];
        double x = s.size() > 0 ? s[0].asNumber() : 0, y = s.size() > 1 ? s[1].asNumber() : 0, z = s.size() > 2 ? s[2].asNumber() : 90;
        std::string code = "# Spin: keeps turning (degrees per second).\n\n";
        if (x == 0 && y == 0)
            return code + "speed = " + num(Json(z), 90) + "\n\ndef on_update(dt):\n    self.angle += speed * dt\n";
        code += "def on_update(dt):\n";
        if (x != 0)
            code += "    self.rotation_x += " + num(Json(x), 0) + " * dt\n";
        if (y != 0)
            code += "    self.rotation_y += " + num(Json(y), 0) + " * dt\n";
        if (z != 0)
            code += "    self.rotation_z += " + num(Json(z), 0) + " * dt\n";
        return code;
    }
    if (c == "Bob") {
        return "# Bob: floats up and down.\n\n"
               "height = " + num(v["height"], 0.25) + "\n"
               "speed = " + num(v["speed"], 1) + "  # bounces per second\n\n"
               "_start_y = 0\n\n"
               "def on_start():\n    _start_y = self.y\n\n"
               "def on_update(dt):\n"
               "    # sin() uses degrees: one bounce is 360.\n"
               "    self.y = _start_y + sin(time() * speed * 360) * height\n";
    }
    if (c == "Collectible") {
        std::string counter = ident(v["counter"], "score");
        std::string code = "# Collectible: touching it adds to game." + counter + " and removes it.\n\n"
                           "def on_trigger(other):\n"
                           "    if other.tag == " + str(v["collector_tag"], "player") + ":\n"
                           "        game." + counter + " = get_game(\"" + counter + "\", 0) + " + num(v["amount"], 1) + "\n";
        if (!v["sound"].asString("").empty())
            code += "        play_sound(" + str(v["sound"], "") + ")\n";
        return code + "        self.destroy()\n";
    }
    if (c == "Hazard") {
        return "# Hazard: hurts whatever it touches.\n\n"
               "damage = " + num(v["damage"], 1) + "\n\n"
               "def on_trigger(other):\n"
               "    if other.tag == " + str(v["victim_tag"], "player") + ":\n"
               "        # damage() takes hit points from the Health behavior (or restarts the level).\n"
               "        other.damage(damage)\n";
    }
    if (c == "Health") {
        std::string zero = v["when_zero"].asString("RestartScene");
        std::string counter = ident(v["counter"], "health");
        std::string action = zero == "Destroy" ? "self.destroy()" : zero == "Respawn" ? "self.position = _start\n        _health = max_health"
                                                         : zero == "Nothing" ? "return" : "restart_scene()";
        return "# Health: hit points. Other scripts call hurt(amount) to take some away.\n\n"
               "max_health = " + num(v["max_health"], 3) + "\n"
               "invincible_time = " + num(v["invincible_time"], 1) + "  # seconds of safety after a hit\n\n"
               "_health = 0\n_safe = 0\n_start = None\n\n"
               "def on_start():\n    _health = max_health\n    _start = self.position\n    game." + counter + " = _health\n\n"
               "def hurt(amount):\n"
               "    if _safe > 0:\n        return\n"
               "    _health -= amount\n"
               "    _safe = invincible_time\n"
               "    game." + counter + " = _health\n"
               "    if _health <= 0:\n        " + action + "\n\n"
               "def on_update(dt):\n"
               "    if _safe > 0:\n"
               "        _safe -= dt\n"
               "        # Blink while safe.\n"
               "        if int(_safe * 12) % 2 == 0:\n            self.alpha = 0.35\n"
               "        else:\n            self.alpha = 1\n";
    }
    if (c == "Lifetime") {
        std::string code = "# Lifetime: disappears after a while.\n\n"
                           "seconds = " + num(v["seconds"], 3) + "\n\n"
                           "_age = 0\n\n"
                           "def on_update(dt):\n"
                           "    _age += dt\n";
        if (v["fade_out"].asBool(true))
            code += "    left = seconds - _age\n    if left < 0.5:\n        self.alpha = max(0, left / 0.5)\n";
        return code + "    if _age >= seconds:\n        self.destroy()\n";
    }
    if (c == "WrapAround") {
        return "# WrapAround: off one edge, back on the other.\n\n"
               "margin = " + num(v["margin"], 0.5) + "\n\n"
               "def on_update(dt):\n"
               "    cam = camera()\n"
               "    half_h = cam.get_component(\"Camera\").size + margin\n"
               "    half_w = half_h * screen_width() / screen_height()\n"
               "    if self.x > cam.x + half_w:\n        self.x = cam.x - half_w\n"
               "    elif self.x < cam.x - half_w:\n        self.x = cam.x + half_w\n"
               "    if self.y > cam.y + half_h:\n        self.y = cam.y - half_h\n"
               "    elif self.y < cam.y - half_h:\n        self.y = cam.y + half_h\n";
    }
    if (c == "Shooter") {
        std::string aim = v["aim"].asString("Up");
        std::string velocity = aim == "Right" ? "vec(bullet_speed, 0)"
                             : aim == "Facing" ? "vec(bullet_speed * _facing(), 0)"
                             : aim == "Mouse" ? "self.direction_to(mouse_x(), mouse_y()) * bullet_speed"
                                              : "vec(0, bullet_speed)";
        std::string code = "# Shooter: fires copies of a prefab.\n\n"
                           "bullet_speed = " + num(v["bullet_speed"], 12) + "\n"
                           "cooldown = " + num(v["cooldown"], 0.25) + "  # seconds between shots\n\n"
                           "_wait = 0\n\n";
        if (aim == "Facing")
            code += "def _facing():\n    if self.flip_x:\n        return -1\n    return 1\n\n";
        const Json& off = v["offset"];
        std::string ox = off.size() > 0 ? num(off[0], 0) : "0", oy = off.size() > 1 ? num(off[1], 0.6) : "0.6";
        code += "def on_update(dt):\n"
                "    _wait -= dt\n"
                "    if _wait <= 0 and key_down(" + str(v["action"], "fire") + "):\n"
                "        _wait = cooldown\n"
                "        bullet = spawn(" + str(v["prefab"], "prefabs/bullet.prefab") + ", self.x + " + ox + ", self.y + " + oy + ")\n"
                "        bullet.velocity = " + velocity + "\n";
        if (!v["sound"].asString("").empty())
            code += "        play_sound(" + str(v["sound"], "") + ")\n";
        return code;
    }
    if (c == "PlatformerController") {
        std::string code = "# Platformer controls: run with the arrow keys, jump with Space.\n"
                           "# Needs a RigidBody2D (so gravity pulls it down) and a collider.\n\n"
                           "speed = " + num(v["speed"], 6) + "\n"
                           "jump_power = " + num(v["jump_power"], 12) + "\n"
                           "extra_jumps = " + num(v["extra_jumps"], 0) + "  # 1 = double jump\n\n"
                           "_jumps_left = 0\n\n"
                           "def on_update(dt):\n"
                           "    move = axis(\"horizontal\")\n"
                           "    self.velocity_x = move * speed\n"
                           "    if self.on_ground:\n        _jumps_left = extra_jumps\n"
                           "    if key_pressed(\"jump\") or key_pressed(\"up\"):\n"
                           "        if self.on_ground:\n            self.velocity_y = jump_power\n";
        if (!v["jump_sound"].asString("").empty())
            code += "            play_sound(" + str(v["jump_sound"], "") + ")\n";
        code += "        elif _jumps_left > 0:\n            _jumps_left -= 1\n            self.velocity_y = jump_power\n";
        if (v["flip_sprite"].asBool(true))
            code += "    if move < 0:\n        self.flip_x = True\n    elif move > 0:\n        self.flip_x = False\n";
        return code;
    }
    if (c == "TopDownController") {
        return "# Top-down controls: walk in every direction.\n\n"
               "speed = " + num(v["speed"], 5) + "\n\n"
               "def on_update(dt):\n"
               "    self.velocity_x = axis(\"horizontal\") * speed\n"
               "    self.velocity_y = axis(\"vertical\") * speed\n";
    }
    if (c == "FollowMouse") {
        return "# FollowMouse: glides after the mouse pointer.\n\n"
               "smoothness = " + num(v["smoothness"], 12) + "\n\n"
               "def on_update(dt):\n" +
               std::string(v["only_while_held"].asBool(false) ? "    if not mouse_down():\n        return\n" : "") +
               "    k = min(1, smoothness * dt)\n"
               "    self.x += (mouse_x() - self.x) * k\n"
               "    self.y += (mouse_y() - self.y) * k\n";
    }
    if (c == "Draggable") {
        return "# Draggable: pick it up with the mouse.\n\n"
               "_dragging = False\n_dx = 0\n_dy = 0\n\n"
               "def on_click():\n"
               "    _dragging = True\n"
               "    _dx = self.x - mouse_x()\n"
               "    _dy = self.y - mouse_y()\n\n"
               "def on_update(dt):\n"
               "    if _dragging:\n"
               "        self.x = mouse_x() + _dx\n"
               "        self.y = mouse_y() + _dy\n"
               "        if not mouse_down():\n            _dragging = False\n";
    }
    if (c == "Spawner") {
        const Json& r = v["random_range"];
        std::string rx = r.size() > 0 ? num(r[0], 4) : "4", ry = r.size() > 1 ? num(r[1], 0) : "0";
        return "# Spawner: makes copies over and over.\n\n"
               "interval = " + num(v["interval"], 1.5) + "  # seconds between copies\n\n"
               "def on_start():\n    every(interval, make_one)\n\n"
               "def make_one():\n"
               "    x = self.x + random_range(-" + rx + ", " + rx + ")\n"
               "    y = self.y + random_range(-" + ry + ", " + ry + ")\n"
               "    spawn(" + str(v["prefab"], "prefabs/enemy.prefab") + ", x, y)\n";
    }
    if (c == "Clickable") {
        std::string counter = ident(v["counter"], "score");
        std::string code = "# Clickable: clicking adds to game." + counter + ".\n\n"
                           "amount = " + num(v["amount"], 1) + "\n\n"
                           "def on_click():\n"
                           "    game." + counter + " = get_game(\"" + counter + "\", 0) + amount\n";
        if (!v["sound"].asString("").empty())
            code += "    play_sound(" + str(v["sound"], "") + ")\n";
        return code;
    }
    if (c == "ScoreDisplay") {
        std::string format = v["format"].asString("Score: {}");
        std::string counter = ident(v["counter"], "score");
        size_t at = format.find("{}");
        std::string before = at == std::string::npos ? format : format.substr(0, at);
        std::string after = at == std::string::npos ? "" : format.substr(at + 2);
        return "# ScoreDisplay: shows game." + counter + " as text.\n\n"
               "def on_update(dt):\n"
               "    self.text = " + str(Json(before), "") + " + str(get_game(\"" + counter + "\", 0))" +
               (after.empty() ? "" : " + " + str(Json(after), "")) + "\n";
    }
    if (c == "SceneLink") {
        std::string when = v["when"].asString("Touch");
        std::string go = std::string(v["delay"].asNumber(0) > 0 ? "    wait(" + num(v["delay"], 0) + ")\n" : "") +
                         "    load_scene(" + str(v["scene"], "scenes/level2.scene") + ")\n";
        if (when == "Click")
            return "# SceneLink: clicking goes to another scene.\n\ndef on_click():\n" + go;
        if (when == "AfterTime")
            return "# SceneLink: goes to another scene after a while.\n\ndef on_start():\n" + go;
        std::string indented;
        size_t start = 0;
        while (start < go.size()) {
            size_t nl = go.find('\n', start);
            indented += "    " + go.substr(start, nl - start + 1);
            start = nl + 1;
        }
        return "# SceneLink: touching it goes to another scene.\n\n"
               "def on_trigger(other):\n    if other.tag == " + str(v["tag"], "player") + ":\n" + indented;
    }
    return "";
}

} // namespace aven
