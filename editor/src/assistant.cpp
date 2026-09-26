// Ask Aven: describe a change in plain words ("make this enemy faster and patrol left/right")
// and see exactly which settings will change before applying them.
//
// This is a rule-based interpreter that runs offline. Every change it makes is an ordinary
// setting or behavior the user can see and edit afterwards, so nothing is hidden or magic.

#include "editor.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/runtime/script_system.h"
#include "aven/script/vm.h"
#include "particle_presets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>

namespace aven::editor {

namespace {

// ---------------------------------------------------------------- text helpers

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Other ways of saying things (editor/data/assistant_words.json): "zippier" -> "faster".
std::string withSynonyms(std::string text) {
    auto isWordChar = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'; };
    for (auto& [canonical, list] : editorData("assistant_words.json")["synonyms"].members())
        for (auto& alt : list.elements()) {
            std::string word = lower(alt.asString(""));
            if (word.empty())
                continue;
            for (size_t p = text.find(word); p != std::string::npos; p = text.find(word, p)) {
                bool startOk = p == 0 || !isWordChar(text[p - 1]);
                bool endOk = p + word.size() >= text.size() || !isWordChar(text[p + word.size()]);
                if (startOk && endOk) {
                    text.replace(p, word.size(), canonical);
                    p += canonical.size();
                } else {
                    p += word.size();
                }
            }
        }
    return text;
}

// Splits a request into separate instructions: "bigger and red, then spin" -> 3 clauses.
std::vector<std::string> clauses(const std::string& text) {
    std::string t = " " + text + " ";
    // Keep quoted text intact.
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (c == '"') {
            quoted = !quoted;
            current += c;
            continue;
        }
        if (!quoted) {
            auto at = [&](const char* w) { return t.compare(i, std::strlen(w), w) == 0; };
            if (c == ',' || c == ';' || c == '.' || c == '!') {
                out.push_back(current);
                current.clear();
                continue;
            }
            for (const char* sep : {" and then ", " and also ", " then ", " also ", " and "}) {
                if (at(sep)) {
                    // "left and right" / "up and down" / "back and forth" stay together.
                    std::string before = lower(current), after = lower(t.substr(i + std::strlen(sep), 8));
                    bool pair = (before.size() >= 4 && (before.substr(before.size() - 4) == "left" ||
                                                        before.substr(before.size() - 2) == "up" ||
                                                        before.substr(before.size() - 4) == "back")) &&
                                (after.rfind("right", 0) == 0 || after.rfind("down", 0) == 0 || after.rfind("forth", 0) == 0);
                    if (!pair) {
                        out.push_back(current);
                        current.clear();
                        i += std::strlen(sep) - 1;
                        goto next;
                    }
                }
            }
        }
        current += c;
    next:;
    }
    out.push_back(current);
    std::vector<std::string> clean;
    for (auto& c : out)
        if (!trim(c).empty())
            clean.push_back(trim(c));
    return clean;
}

bool hasWord(const std::string& text, const std::string& word) {
    size_t pos = 0;
    while ((pos = text.find(word, pos)) != std::string::npos) {
        bool startOk = pos == 0 || !std::isalpha(static_cast<unsigned char>(text[pos - 1]));
        size_t end = pos + word.size();
        // Allow simple plurals/verb forms: "spin" matches "spins", "spinning".
        bool endOk = end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end])) || text.compare(end, 1, "s") == 0 ||
                     text.compare(end, 3, "ing") == 0 || text.compare(end, 2, "ed") == 0 || text.compare(end, 2, "er") == 0;
        if (startOk && endOk)
            return true;
        pos = end;
    }
    return false;
}

bool any(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* w : words)
        if (hasWord(text, w))
            return true;
    return false;
}

std::optional<double> numberIn(const std::string& text) {
    static const std::pair<const char*, double> words[] = {
        {"half", 0.5}, {"one", 1}, {"two", 2}, {"twice", 2}, {"double", 2}, {"three", 3}, {"triple", 3}, {"thrice", 3},
        {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"hundred", 100}};
    for (size_t i = 0; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) ||
            (text[i] == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            size_t end = i;
            while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.'))
                ++end;
            bool negative = i > 0 && text[i - 1] == '-';
            double v = std::atof(text.substr(i, end - i).c_str());
            return negative ? -v : v;
        }
    }
    for (auto& [w, v] : words)
        if (hasWord(text, w))
            return v;
    return std::nullopt;
}

std::string quoted(const std::string& text) {
    size_t a = text.find('"');
    if (a == std::string::npos)
        return "";
    size_t b = text.find('"', a + 1);
    return b == std::string::npos ? text.substr(a + 1) : text.substr(a + 1, b - a - 1);
}

// The word right after one of the given words ("follow the player" -> "player").
std::string wordAfter(const std::string& text, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        size_t p = text.find(k);
        if (p == std::string::npos)
            continue;
        std::string rest = text.substr(p + std::strlen(k));
        std::vector<std::string> ws;
        std::string w;
        for (char c : rest + " ") {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '.') {
                w += c;
            } else if (!w.empty()) {
                ws.push_back(w);
                w.clear();
            }
        }
        for (auto& x : ws)
            if (x != "the" && x != "a" && x != "an" && x != "to" && x != "at" && x != "from" && x != "of" && x != "my" && x != "me")
                return x;
    }
    return "";
}

std::optional<Color> colorIn(const std::string& text) {
    static const std::pair<const char*, uint32_t> names[] = {
        {"red", 0xEF4444},    {"orange", 0xF97316}, {"yellow", 0xFACC15}, {"gold", 0xF5B301},    {"green", 0x22C55E},
        {"lime", 0x84CC16},   {"teal", 0x14B8A6},   {"cyan", 0x06B6D4},   {"blue", 0x3B82F6},    {"navy", 0x1E3A8A},
        {"purple", 0xA855F7}, {"violet", 0x8B5CF6}, {"pink", 0xEC4899},   {"magenta", 0xD946EF}, {"brown", 0x92400E},
        {"black", 0x111111},  {"white", 0xFFFFFF},  {"gray", 0x9CA3AF},   {"grey", 0x9CA3AF},    {"silver", 0xC0C7D0},
        {"beige", 0xE8D8B0},  {"sky", 0x7DD3FC},    {"grass", 0x4CAF50},  {"lava", 0xFF4D1F},    {"water", 0x38BDF8}};
    size_t hash = text.find('#');
    if (hash != std::string::npos && hash + 7 <= text.size()) {
        std::string hex = text.substr(hash + 1, 6);
        if (std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            return Color::fromHex(static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16)));
    }
    for (auto& [n, v] : names)
        if (hasWord(text, n)) {
            Color c = Color::fromHex(v);
            if (hasWord(text, "dark") || hasWord(text, "darker"))
                c = {c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 1};
            if (hasWord(text, "light") || hasWord(text, "lighter") || hasWord(text, "pastel"))
                c = {c.r + (1 - c.r) * 0.45f, c.g + (1 - c.g) * 0.45f, c.b + (1 - c.b) * 0.45f, 1};
            return c;
        }
    return std::nullopt;
}

std::string fmt(double v) {
    char buf[32];
    if (std::abs(v - std::round(v)) < 1e-6)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else
        std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string arrow() { return " -> "; }

} // namespace

// ---------------------------------------------------------------- field helpers

Json Editor::fieldValue(Entity e, const std::string& component, const std::string& field) {
    const ComponentInfo* ci = ComponentRegistry::find(component);
    if (!ci)
        return {};
    void* data = ci->get(scene().registry(), e);
    if (!data)
        return {};
    for (auto& f : ci->fields)
        if (f.name == field)
            return saveField(f, data);
    return {};
}

void Editor::setFieldValue(Entity e, const std::string& component, const std::string& field, const Json& value) {
    const ComponentInfo* ci = ComponentRegistry::find(component);
    if (!ci)
        return;
    auto& reg = scene().registry();
    void* data = ci->get(reg, e);
    if (!data) {
        ci->add(reg, e);
        data = ci->get(reg, e);
    }
    for (auto& f : ci->fields)
        if (f.name == field)
            loadField(f, data, value);
    flashFields_[component + "/" + field] = 2.5f;
}

bool Editor::hasComponent(Entity e, const std::string& component) {
    const ComponentInfo* ci = ComponentRegistry::find(component);
    return ci && ci->get(scene().registry(), e);
}

void Editor::addComponentByName(Entity e, const std::string& component) {
    const ComponentInfo* ci = ComponentRegistry::find(component);
    if (!ci || ci->get(scene().registry(), e))
        return;
    ci->add(scene().registry(), e);
    flashFields_[component + "/*"] = 2.5f;
    ensureRequirements(e, component);
}

// Behaviors that need physics get it automatically, sized to the object.
void Editor::ensureRequirements(Entity e, const std::string& component) {
    auto& reg = scene().registry();
    bool threeD = reg.has<MeshRenderer>(e) || reg.has<CharacterController>(e);
    Vec2 size{1, 1};
    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
        size = sr->size;
    auto ensureTrigger = [&] {
        if (threeD) {
            if (!reg.has<BoxCollider>(e) && !reg.has<SphereCollider>(e))
                reg.emplace<BoxCollider>(e).isTrigger = true;
        } else if (!reg.has<BoxCollider2D>(e) && !reg.has<CircleCollider2D>(e)) {
            reg.emplace<BoxCollider2D>(e).size = size;
            reg.get<BoxCollider2D>(e).isTrigger = true;
        }
    };
    // Screen things need a place on the screen.
    if (const ComponentInfo* ci = ComponentRegistry::find(component); ci && ci->category == "UI" && !reg.has<UIElement>(e))
        reg.emplace<UIElement>(e).size = component == "ValueBar" ? Vec2{300, 32} : Vec2{220, 64};
    if (component == "PlatformerController" || component == "TopDownController") {
        if (!reg.has<RigidBody2D>(e)) {
            auto& rb = reg.emplace<RigidBody2D>(e);
            rb.fixedRotation = true;
            rb.gravityScale = component == "TopDownController" ? 0.0f : 2.5f;
        }
        if (!reg.has<BoxCollider2D>(e) && !reg.has<CircleCollider2D>(e)) {
            auto& b = reg.emplace<BoxCollider2D>(e);
            b.size = {size.x * 0.8f, size.y * 0.95f};
            b.friction = 0;
        }
        if (scene().info(e).tag.empty())
            scene().info(e).tag = "player";
    } else if (component == "Collectible" || component == "Hazard" || component == "SceneLink") {
        ensureTrigger();
    } else if (component == "Health") {
        if (scene().info(e).tag.empty())
            scene().info(e).tag = "player";
    } else if (component == "Draggable" || component == "Clickable") {
        // Clicking works on sprites; 3D objects need a collider to be clicked.
        if (threeD && !reg.has<BoxCollider>(e) && !reg.has<SphereCollider>(e))
            reg.emplace<BoxCollider>(e).isTrigger = true;
    }
}

std::vector<std::string> Editor::scriptVariables(Entity e) {
    std::vector<std::string> out;
    auto* sc = scene().registry().tryGet<Script>(e);
    if (!sc || sc->path.empty())
        return out;
    auto text = fs::readText(projectDir_ / sc->path);
    if (!text)
        return out;
    std::string source = *text;
    if (fs::extension(sc->path) == ".blocks")
        source = blocks::compileFile(source);
    script::VM vm;
    vm.onError = [](const script::ScriptError&) {};
    if (auto mod = vm.compile(source, sc->path))
        for (auto& ex : mod->exports)
            if (ex.defaultValue.isNumber())
                out.push_back(ex.name);
    return out;
}

// ---------------------------------------------------------------- the interpreter

void Editor::askAven(const std::string& request) {
    assistant_ = {};
    assistant_.request = request;
    std::vector<Entity> targets = selectedEntities();
    Scene& s = scene();
    auto& reg = s.registry();
    auto add = [&](Entity e, std::string text, std::function<void(Editor&, Entity)> apply) {
        UUID id = s.info(e).uuid;
        std::string who = targets.size() > 1 ? s.info(e).name + ": " : "";
        assistant_.proposals.push_back({who + text, [id, apply](Editor& ed) {
                                            if (Entity x = ed.scene().findByUUID(id))
                                                apply(ed, x);
                                        },
                                        true});
    };
    auto numField = [&](Entity e, const std::string& comp, const std::string& field, double fallback) {
        Json v = fieldValue(e, comp, field);
        return v.isNumber() ? v.asNumber() : fallback;
    };
    // Scales a number setting: script variables first, then behaviors/components.
    auto scaleSetting = [&](Entity e, const std::vector<std::string>& varNames, const std::vector<std::pair<std::string, std::string>>& fields,
                            double factor, std::optional<double> setTo, const char* what) -> bool {
        bool found = false;
        auto* sc = reg.tryGet<Script>(e);
        for (auto& var : scriptVariables(e)) {
            std::string v = lower(var);
            bool match = std::any_of(varNames.begin(), varNames.end(), [&](const std::string& n) { return v == n; });
            if (!match)
                continue;
            double old = 0;
            if (sc && sc->overrides.contains(var) && sc->overrides[var].isNumber())
                old = sc->overrides[var].asNumber();
            else {
                // Read the script's default.
                auto text = fs::readText(projectDir_ / sc->path);
                std::string source = text ? *text : "";
                if (fs::extension(sc->path) == ".blocks")
                    source = blocks::compileFile(source);
                script::VM vm;
                vm.onError = [](const script::ScriptError&) {};
                if (auto mod = vm.compile(source, sc->path))
                    for (auto& ex : mod->exports)
                        if (ex.name == var && ex.defaultValue.isNumber())
                            old = ex.defaultValue.number();
            }
            double now = setTo ? *setTo : old * factor;
            add(e, std::string("Script variable ") + toLabel(var) + ": " + fmt(old) + arrow() + fmt(now),
                [var, now](Editor& ed, Entity x) {
                    ed.scene().registry().getOrEmplace<Script>(x).overrides[var] = now;
                    ed.flashFields_["script/" + var] = 2.5f;
                });
            found = true;
        }
        for (auto& [comp, field] : fields) {
            if (!hasComponent(e, comp))
                continue;
            double old = numField(e, comp, field, 0);
            double now = setTo ? *setTo : old * factor;
            add(e, comp + " > " + toLabel(field) + ": " + fmt(old) + arrow() + fmt(now),
                [comp, field, now](Editor& ed, Entity x) { ed.setFieldValue(x, comp, field, now); });
            found = true;
        }
        if (!found)
            assistant_.notes.push_back(std::string("Nothing on ") + s.info(e).name + " has a " + what + " to change yet.");
        return found;
    };

    for (const std::string& original : clauses(request)) {
        std::string c = withSynonyms(lower(original));
        bool understood = false;
        auto ok = [&](const std::string& summary) {
            understood = true;
            assistant_.understood.push_back(summary);
        };
        for (Entity e : targets) {
            bool threeD = reg.has<MeshRenderer>(e) || reg.has<CharacterController>(e) || (view3D_ && !reg.has<SpriteRenderer>(e));
            auto number = numberIn(c);
            // How much: "a lot" / "much" / "a bit" / explicit numbers.
            double more = any(c, {"much", "lot", "way", "super", "really"}) ? 2.0 : any(c, {"bit", "little", "slightly"}) ? 1.2 : 1.5;
            bool times = c.find("times") != std::string::npos || any(c, {"twice", "double", "triple", "half"});
            std::optional<double> setTo;
            if (number && !times)
                setTo = number;

            // --- remove / stop things
            if (any(c, {"stop", "remove", "don't", "dont", "no longer", "without", "not"}) &&
                any(c, {"patrol", "moving", "move", "chase", "follow", "spin", "rotat", "bob", "float", "gravity", "fall", "shoot"})) {
                std::vector<std::string> remove;
                if (any(c, {"patrol", "moving", "move"}))
                    remove = {"Patrol", "Chase", "TopDownController", "PlatformerController"};
                if (any(c, {"chase", "follow"}))
                    remove = {"Chase", "FollowMouse"};
                if (any(c, {"spin", "rotat"}))
                    remove = {"Spin"};
                if (any(c, {"bob", "float"}))
                    remove = {"Bob"};
                if (any(c, {"gravity", "fall"}))
                    remove = {"RigidBody2D", "RigidBody"};
                if (any(c, {"shoot"}))
                    remove = {"Shooter"};
                for (auto& comp : remove)
                    if (hasComponent(e, comp))
                        add(e, "Remove " + comp, [comp](Editor& ed, Entity x) {
                            if (auto* ci = ComponentRegistry::find(comp))
                                ci->remove(ed.scene().registry(), x);
                        });
                ok("stop");
                continue;
            }

            // --- speed
            if (any(c, {"faster", "quicker", "slower", "speed"}) && !any(c, {"jump"})) {
                double factor = any(c, {"slower", "slow"}) ? 1.0 / more : more;
                if (times && number)
                    factor = any(c, {"slower"}) ? 1.0 / *number : *number;
                bool spinning = any(c, {"spin", "rotat", "turn"});
                bool bobbing = any(c, {"bob", "float"});
                if (spinning) {
                    if (auto* sp = reg.tryGet<Spin>(e)) {
                        Vec3 old = sp->speed;
                        Vec3 now = setTo ? Vec3{0, 0, static_cast<float>(*setTo)} : old * static_cast<float>(factor);
                        add(e, "Spin > Speed: " + fmt(old.z != 0 ? old.z : old.y) + arrow() + fmt(now.z != 0 ? now.z : now.y),
                            [now](Editor& ed, Entity x) { ed.setFieldValue(x, "Spin", "speed", Json::parse("[" + fmt(now.x) + "," + fmt(now.y) + "," + fmt(now.z) + "]")); });
                    }
                } else if (bobbing) {
                    scaleSetting(e, {}, {{"Bob", "speed"}}, factor, setTo, "bobbing speed");
                } else {
                    scaleSetting(e, {"speed", "move_speed", "walk_speed", "run_speed"},
                                 {{"Patrol", "speed"}, {"Chase", "speed"}, {"PlatformerController", "speed"},
                                  {"TopDownController", "speed"}, {"CharacterController", "speed"}},
                                 factor, setTo, "speed");
                }
                ok(factor > 1 ? "faster" : "slower");
                continue;
            }

            // --- jumping
            if (any(c, {"jump"})) {
                if (any(c, {"double jump", "double-jump"}) || (any(c, {"double"}) && any(c, {"jump"}))) {
                    if (!hasComponent(e, "PlatformerController") && !threeD)
                        add(e, "Add PlatformerController (run and jump)", [](Editor& ed, Entity x) { ed.addComponentByName(x, "PlatformerController"); });
                    add(e, "PlatformerController > Extra Jumps" + arrow() + "1 (double jump)",
                        [](Editor& ed, Entity x) { ed.setFieldValue(x, "PlatformerController", "extra_jumps", 1); });
                    ok("double jump");
                    continue;
                }
                bool controlled = hasComponent(e, "PlatformerController") || hasComponent(e, "CharacterController") ||
                                  !scriptVariables(e).empty();
                if (!controlled && any(c, {"can", "make", "let", "able"})) {
                    if (threeD)
                        add(e, "Add CharacterController (walk with WASD, jump with Space)",
                            [](Editor& ed, Entity x) { ed.addComponentByName(x, "CharacterController"); });
                    else
                        add(e, "Add PlatformerController (run with the arrow keys, jump with Space)",
                            [](Editor& ed, Entity x) { ed.addComponentByName(x, "PlatformerController"); });
                    ok("jump");
                    continue;
                }
                double factor = any(c, {"lower", "less", "smaller", "weaker"}) ? 1.0 / more : more;
                if (times && number)
                    factor = *number;
                scaleSetting(e, {"jump_power", "jump", "jump_height", "jump_speed"},
                             {{"PlatformerController", "jump_power"}, {"CharacterController", "jump_height"}}, factor, setTo, "jump");
                ok(factor > 1 ? "jump higher" : "jump lower");
                continue;
            }

            // --- size
            if (any(c, {"bigger", "larger", "huge", "giant", "smaller", "tiny", "size", "grow", "shrink", "big", "small"}) &&
                !any(c, {"text"})) {
                double factor = any(c, {"smaller", "tiny", "shrink", "small"}) ? 1.0 / more : more;
                if (hasWord(c, "huge") || hasWord(c, "giant"))
                    factor = 3;
                if (hasWord(c, "tiny"))
                    factor = 0.33;
                if (times && number)
                    factor = *number;
                Vec3 old = s.transform(e).scale;
                Vec3 now = setTo ? Vec3{static_cast<float>(*setTo), static_cast<float>(*setTo), threeD ? static_cast<float>(*setTo) : old.z}
                                 : Vec3{old.x * static_cast<float>(factor), old.y * static_cast<float>(factor),
                                        threeD ? old.z * static_cast<float>(factor) : old.z};
                add(e, "Transform > Scale: " + fmt(old.x) + arrow() + fmt(now.x), [now](Editor& ed, Entity x) {
                    ed.scene().transform(x).scale = now;
                    ed.flashFields_["Transform/scale"] = 2.5f;
                });
                ok(factor > 1 ? "bigger" : "smaller");
                continue;
            }

            // --- patrol / move back and forth
            if (any(c, {"patrol", "back and forth", "left and right", "up and down", "forward and back", "pace"}) ||
                (any(c, {"move", "walk", "go", "slide"}) && any(c, {"left", "right", "side", "up and down", "around"}))) {
                if (any(c, {"float", "bob", "hover"})) {
                    add(e, "Add Bob (floats up and down)", [](Editor& ed, Entity x) { ed.addComponentByName(x, "Bob"); });
                    ok("bob");
                    continue;
                }
                std::string axis = any(c, {"up and down", "up", "down", "vertical"}) ? "Y" : any(c, {"forward", "toward"}) ? "Z" : "X";
                double dist = number ? *number : 3;
                add(e, std::string(hasComponent(e, "Patrol") ? "Patrol > " : "Add Patrol: ") + "moves " +
                           (axis == "X" ? "left and right" : axis == "Y" ? "up and down" : "forward and back") + ", " + fmt(dist) +
                           " units each way",
                    [axis, dist](Editor& ed, Entity x) {
                        ed.addComponentByName(x, "Patrol");
                        ed.setFieldValue(x, "Patrol", "axis", axis);
                        ed.setFieldValue(x, "Patrol", "distance", dist);
                    });
                ok("patrol");
                continue;
            }

            // --- follow / chase / run away
            if (any(c, {"follow", "chase", "hunt", "run away", "flee", "avoid", "go after", "towards", "toward"})) {
                if (any(c, {"mouse", "cursor", "pointer"})) {
                    add(e, "Add FollowMouse", [](Editor& ed, Entity x) { ed.addComponentByName(x, "FollowMouse"); });
                    ok("follow the mouse");
                    continue;
                }
                if (any(c, {"camera"}) && any(c, {"follow"})) {
                    // "the camera follows this"
                    Entity cam = SceneRenderer::findCamera(s);
                    if (cam) {
                        UUID target = s.info(e).uuid;
                        add(e, "Camera > CameraFollow: follows " + s.info(e).name, [target](Editor& ed, Entity) {
                            Entity camera = SceneRenderer::findCamera(ed.scene());
                            if (!camera)
                                return;
                            auto& cf = ed.scene().registry().getOrEmplace<CameraFollow>(camera);
                            cf.target = target;
                            ed.flashFields_["CameraFollow/target"] = 2.5f;
                        });
                        ok("camera follows");
                    }
                    continue;
                }
                bool away = any(c, {"run away", "flee", "avoid", "away"});
                std::string tag = wordAfter(c, {"follow ", "chase ", "hunt ", "after ", "from ", "avoid ", "toward ", "towards "});
                if (tag.empty() || tag == "it")
                    tag = "player";
                if (tag.size() > 1 && tag.back() == 's' && tag != "boss")
                    tag.pop_back();
                add(e, std::string("Add Chase: ") + (away ? "runs away from " : "chases ") + "objects tagged '" + tag + "'",
                    [tag, away](Editor& ed, Entity x) {
                        ed.addComponentByName(x, "Chase");
                        ed.setFieldValue(x, "Chase", "target_tag", tag);
                        ed.setFieldValue(x, "Chase", "run_away", away);
                    });
                ok(away ? "run away" : "chase");
                continue;
            }

            // --- spin / float
            if (any(c, {"spin", "rotate", "rotating", "turn around", "twirl"})) {
                double speed = number ? *number : any(c, {"fast", "quick"}) ? 360 : any(c, {"slow", "slowly", "gently"}) ? 45 : 90;
                bool three = threeD;
                add(e, "Add Spin: " + fmt(speed) + " degrees per second", [speed, three](Editor& ed, Entity x) {
                    ed.addComponentByName(x, "Spin");
                    ed.setFieldValue(x, "Spin", "speed", three ? Json::parse("[0," + fmt(speed) + ",0]") : Json::parse("[0,0," + fmt(speed) + "]"));
                });
                ok("spin");
                continue;
            }
            if (any(c, {"bob", "hover", "float", "floating"})) {
                add(e, "Add Bob (floats up and down)", [](Editor& ed, Entity x) { ed.addComponentByName(x, "Bob"); });
                ok("bob");
                continue;
            }

            // --- collectibles, hazards, health
            if (any(c, {"collect", "collectible", "pick up", "pickup", "coin", "gem", "treasure", "worth", "points"}) &&
                !any(c, {"show", "display"})) {
                std::string counter = any(c, {"coin"}) ? "coins" : any(c, {"gem"}) ? "gems" : any(c, {"star"}) ? "stars" : "score";
                double amount = number ? *number : 1;
                add(e, "Add Collectible: adds " + fmt(amount) + " to game." + counter + " when the player touches it",
                    [counter, amount](Editor& ed, Entity x) {
                        ed.addComponentByName(x, "Collectible");
                        ed.setFieldValue(x, "Collectible", "counter", counter);
                        ed.setFieldValue(x, "Collectible", "amount", amount);
                    });
                ok("collectible");
                continue;
            }
            if (any(c, {"dangerous", "deadly", "kill", "hurt", "damage", "hazard", "lava", "spike", "harm", "poison"})) {
                double dmg = number ? *number : 1;
                add(e, "Add Hazard: takes " + fmt(dmg) + " health from the player on touch", [dmg](Editor& ed, Entity x) {
                    ed.addComponentByName(x, "Hazard");
                    ed.setFieldValue(x, "Hazard", "damage", dmg);
                });
                ok("hazard");
                continue;
            }
            if (any(c, {"health", "lives", "life", "hearts", "hp", "hit points"})) {
                double hp = number ? *number : 3;
                add(e, "Health > Max Health" + arrow() + fmt(hp), [hp](Editor& ed, Entity x) {
                    ed.addComponentByName(x, "Health");
                    ed.setFieldValue(x, "Health", "max_health", hp);
                });
                ok("health");
                continue;
            }

            // --- physics
            if (any(c, {"bouncy", "bounce", "bounces", "bounciness"})) {
                double b = number && *number <= 1 ? *number : 0.8;
                add(e, "Collider > Bounciness" + arrow() + fmt(b) + (threeD ? "" : " (and falls with gravity)"),
                    [b, threeD](Editor& ed, Entity x) {
                        auto& r = ed.scene().registry();
                        if (threeD) {
                            if (!r.has<RigidBody>(x))
                                r.emplace<RigidBody>(x);
                            if (!r.has<SphereCollider>(x) && !r.has<BoxCollider>(x))
                                r.emplace<BoxCollider>(x);
                            ed.setFieldValue(x, r.has<SphereCollider>(x) ? "SphereCollider" : "BoxCollider", "bounciness", b);
                        } else {
                            if (!r.has<RigidBody2D>(x))
                                r.emplace<RigidBody2D>(x);
                            if (!r.has<CircleCollider2D>(x) && !r.has<BoxCollider2D>(x)) {
                                auto* sr = r.tryGet<SpriteRenderer>(x);
                                r.emplace<BoxCollider2D>(x).size = sr ? sr->size : Vec2{1, 1};
                            }
                            ed.setFieldValue(x, r.has<CircleCollider2D>(x) ? "CircleCollider2D" : "BoxCollider2D", "bounciness", b);
                        }
                    });
                ok("bouncy");
                continue;
            }
            if (any(c, {"fall", "gravity", "physics", "heavy", "drop", "tumble"}) && !any(c, {"no gravity", "zero gravity"})) {
                add(e, threeD ? "Add RigidBody and BoxCollider (falls and collides)" : "Add RigidBody2D and a collider (falls and collides)",
                    [threeD](Editor& ed, Entity x) {
                        auto& r = ed.scene().registry();
                        if (threeD) {
                            if (!r.has<RigidBody>(x))
                                r.emplace<RigidBody>(x);
                            if (!r.has<BoxCollider>(x) && !r.has<SphereCollider>(x))
                                r.emplace<BoxCollider>(x);
                        } else {
                            if (!r.has<RigidBody2D>(x))
                                r.emplace<RigidBody2D>(x);
                            if (!r.has<BoxCollider2D>(x) && !r.has<CircleCollider2D>(x)) {
                                auto* sr = r.tryGet<SpriteRenderer>(x);
                                r.emplace<BoxCollider2D>(x).size = sr ? sr->size : Vec2{1, 1};
                            }
                        }
                        ed.flashFields_[threeD ? "RigidBody/*" : "RigidBody2D/*"] = 2.5f;
                    });
                ok("gravity");
                continue;
            }
            if (any(c, {"solid", "wall", "floor", "ground", "platform", "stand on", "block", "obstacle", "collide"})) {
                add(e, "Add a collider (other things bump into it)", [threeD](Editor& ed, Entity x) {
                    auto& r = ed.scene().registry();
                    if (threeD) {
                        if (!r.has<BoxCollider>(x))
                            r.emplace<BoxCollider>(x);
                    } else if (!r.has<BoxCollider2D>(x)) {
                        auto* sr = r.tryGet<SpriteRenderer>(x);
                        r.emplace<BoxCollider2D>(x).size = sr ? sr->size : Vec2{1, 1};
                    }
                });
                ok("solid");
                continue;
            }

            // --- controls
            if (any(c, {"player", "controllable", "control", "arrow keys", "wasd", "keyboard", "playable", "first person"})) {
                if (threeD) {
                    bool fp = any(c, {"first person", "fps"});
                    add(e, std::string("Add CharacterController") + (fp ? " (first person)" : " (walk with WASD, jump with Space)"),
                        [fp](Editor& ed, Entity x) {
                            ed.addComponentByName(x, "CharacterController");
                            ed.setFieldValue(x, "CharacterController", "first_person", fp);
                        });
                } else if (any(c, {"top down", "top-down", "all directions", "walk around", "rpg", "maze"})) {
                    add(e, "Add TopDownController (walk in every direction)",
                        [](Editor& ed, Entity x) { ed.addComponentByName(x, "TopDownController"); });
                } else {
                    add(e, "Add PlatformerController (run with the arrow keys, jump with Space)",
                        [](Editor& ed, Entity x) { ed.addComponentByName(x, "PlatformerController"); });
                }
                ok("controls");
                continue;
            }

            // --- shooting
            if (any(c, {"shoot", "fire", "laser", "bullet", "shots"}) && !any(c, {"fire particles", "on fire", "burn"})) {
                std::string aim = any(c, {"mouse", "cursor"}) ? "Mouse" : hasComponent(e, "PlatformerController") || any(c, {"forward", "facing"})
                                                                              ? "Facing"
                                                                              : any(c, {"right"}) ? "Right" : "Up";
                add(e, "Add Shooter: fires prefabs/bullet.prefab (" + aim + ") when you press Z, J or click",
                    [aim](Editor& ed, Entity x) {
                        ed.ensureBulletPrefab();
                        ed.addComponentByName(x, "Shooter");
                        ed.setFieldValue(x, "Shooter", "prefab", "prefabs/bullet.prefab");
                        ed.setFieldValue(x, "Shooter", "aim", aim);
                    });
                ok("shoot");
                continue;
            }

            // --- appearance
            if (any(c, {"transparent", "see-through", "see through", "ghost", "invisible", "fade", "opaque"})) {
                float a = hasWord(c, "invisible") ? 0.0f : hasWord(c, "opaque") ? 1.0f : number && *number <= 1 ? static_cast<float>(*number) : 0.5f;
                add(e, "Color > Alpha" + arrow() + fmt(a), [a](Editor& ed, Entity x) {
                    auto& r = ed.scene().registry();
                    if (auto* sr = r.tryGet<SpriteRenderer>(x))
                        sr->color.a = a;
                    if (auto* mr = r.tryGet<MeshRenderer>(x))
                        mr->color.a = a;
                    if (auto* t = r.tryGet<TextRenderer>(x))
                        t->color.a = a;
                    if (auto* u = r.tryGet<UIText>(x))
                        u->color.a = a;
                });
                ok("transparency");
                continue;
            }
            if (any(c, {"glow", "glowing", "shine", "shiny", "light up", "neon"})) {
                Color col = colorIn(c).value_or(Color{1, 0.9f, 0.5f, 1});
                if (reg.has<MeshRenderer>(e)) {
                    add(e, "MeshRenderer > Emission: glows (strength 3)", [col](Editor& ed, Entity x) {
                        ed.setFieldValue(x, "MeshRenderer", "emission", Json::parse("[" + fmt(col.r) + "," + fmt(col.g) + "," + fmt(col.b) + ",1]"));
                        ed.setFieldValue(x, "MeshRenderer", "emission_strength", 3);
                    });
                } else {
                    add(e, "Add sparkly particles", [](Editor& ed, Entity x) {
                        auto& pe = ed.scene().registry().getOrEmplace<ParticleEmitter>(x);
                        if (auto* p = findParticlePreset("Sparkles"))
                            pe = p->settings;
                        ed.flashFields_["ParticleEmitter/*"] = 2.5f;
                    });
                }
                ok("glow");
                continue;
            }
            if (const ParticlePreset* preset = [&]() -> const ParticlePreset* {
                    for (auto& p : particlePresets())
                        if (hasWord(c, lower(p.name)) || (lower(p.name).back() == 's' && hasWord(c, lower(p.name).substr(0, strlen(p.name) - 1))))
                            return &p;
                    return nullptr;
                }()) {
                std::string name = preset->name;
                add(e, "ParticleEmitter: " + name + " preset", [name](Editor& ed, Entity x) {
                    auto& pe = ed.scene().registry().getOrEmplace<ParticleEmitter>(x);
                    if (auto* p = findParticlePreset(name))
                        pe = p->settings;
                    ed.flashFields_["ParticleEmitter/*"] = 2.5f;
                });
                ok(name);
                continue;
            }
            if (auto col = colorIn(c); col && !any(c, {"background", "sky"})) {
                Color cc = *col;
                std::string hex = fmt(cc.r) + ", " + fmt(cc.g) + ", " + fmt(cc.b);
                add(e, "Color" + arrow() + trim(original), [cc](Editor& ed, Entity x) {
                    auto& r = ed.scene().registry();
                    auto set = [&](Color& target, const char* comp) {
                        target = {cc.r, cc.g, cc.b, target.a};
                        ed.flashFields_[std::string(comp) + "/color"] = 2.5f;
                    };
                    if (auto* sr = r.tryGet<SpriteRenderer>(x))
                        set(sr->color, "SpriteRenderer");
                    if (auto* mr = r.tryGet<MeshRenderer>(x))
                        set(mr->color, "MeshRenderer");
                    if (auto* t = r.tryGet<TextRenderer>(x))
                        set(t->color, "TextRenderer");
                    if (auto* u = r.tryGet<UIText>(x))
                        set(u->color, "UIText");
                    if (auto* ui = r.tryGet<UIImage>(x))
                        set(ui->color, "UIImage");
                    if (auto* l = r.tryGet<Light>(x))
                        set(l->color, "Light");
                });
                ok("color");
                continue;
            }

            // --- everything else
            if (any(c, {"disappear", "vanish", "despawn", "destroy itself", "go away"}) && number) {
                double secs = *number;
                add(e, "Add Lifetime: disappears after " + fmt(secs) + " seconds", [secs](Editor& ed, Entity x) {
                    ed.addComponentByName(x, "Lifetime");
                    ed.setFieldValue(x, "Lifetime", "seconds", secs);
                });
                ok("lifetime");
                continue;
            }
            if (any(c, {"wrap", "other side", "comes back"})) {
                add(e, "Add WrapAround", [](Editor& ed, Entity x) { ed.addComponentByName(x, "WrapAround"); });
                ok("wrap around");
                continue;
            }
            if (any(c, {"drag", "draggable", "pick it up with the mouse"})) {
                add(e, "Add Draggable", [](Editor& ed, Entity x) { ed.addComponentByName(x, "Draggable"); });
                ok("draggable");
                continue;
            }
            if (any(c, {"click", "clickable", "tap"})) {
                double amount = number ? *number : 1;
                add(e, "Add Clickable: each click adds " + fmt(amount) + " to game.score", [amount](Editor& ed, Entity x) {
                    ed.addComponentByName(x, "Clickable");
                    ed.setFieldValue(x, "Clickable", "amount", amount);
                });
                ok("clickable");
                continue;
            }
            if (any(c, {"show", "display"}) && any(c, {"score", "coins", "gems", "health", "points", "lives"})) {
                std::string counter = any(c, {"coin"}) ? "coins" : any(c, {"gem"}) ? "gems" : any(c, {"health", "lives"}) ? "health" : "score";
                std::string label = counter == "score" ? "Score" : counter == "coins" ? "Coins" : counter == "gems" ? "Gems" : "Health";
                add(e, "Add ScoreDisplay: shows \"" + label + ": {}\"", [counter, label](Editor& ed, Entity x) {
                    auto& r = ed.scene().registry();
                    if (!r.has<UIText>(x) && !r.has<TextRenderer>(x))
                        r.emplace<TextRenderer>(x);
                    ed.addComponentByName(x, "ScoreDisplay");
                    ed.setFieldValue(x, "ScoreDisplay", "counter", counter);
                    ed.setFieldValue(x, "ScoreDisplay", "format", label + ": {}");
                });
                ok("show score");
                continue;
            }
            if (any(c, {"level", "scene", "door", "portal", "exit", "goal", "finish"}) && any(c, {"go", "load", "next", "lead", "takes", "to"})) {
                std::string best;
                for (auto& f : projectFiles({".scene"}))
                    if (f != scenePath_ && (best.empty() || c.find(lower(stdfs::path(f).stem().string())) != std::string::npos))
                        best = f;
                bool click = any(c, {"click", "button", "press"});
                add(e, "Add SceneLink: " + std::string(click ? "clicking" : "touching") + " it goes to " + (best.empty() ? "(pick a scene)" : best),
                    [best, click](Editor& ed, Entity x) {
                        ed.addComponentByName(x, "SceneLink");
                        ed.setFieldValue(x, "SceneLink", "scene", best);
                        ed.setFieldValue(x, "SceneLink", "when", click ? "Click" : "Touch");
                    });
                ok("scene link");
                continue;
            }
            std::string q = quoted(original);
            if (!q.empty() && any(c, {"say", "text", "write", "label", "show"})) {
                add(e, "Text" + arrow() + "\"" + q + "\"", [q](Editor& ed, Entity x) {
                    auto& r = ed.scene().registry();
                    if (auto* u = r.tryGet<UIText>(x))
                        u->text = q;
                    else if (auto* b = r.tryGet<UIButton>(x))
                        b->text = q;
                    else
                        r.getOrEmplace<TextRenderer>(x).text = q;
                });
                ok("text");
                continue;
            }
            if (any(c, {"rename", "call it", "name it", "named"})) {
                std::string name = !q.empty() ? q : wordAfter(original, {"rename to ", "rename it to ", "call it ", "name it ", "named "});
                if (!name.empty()) {
                    add(e, "Name" + arrow() + name, [name](Editor& ed, Entity x) { ed.scene().info(x).name = name; });
                    ok("rename");
                    continue;
                }
            }
            if (any(c, {"tag"})) {
                std::string tag = !q.empty() ? q : wordAfter(c, {"tag it ", "tag as ", "tag "});
                if (!tag.empty()) {
                    add(e, "Tag" + arrow() + tag, [tag](Editor& ed, Entity x) { ed.scene().info(x).tag = tag; });
                    ok("tag");
                    continue;
                }
            }
        }
        // Whole-scene requests.
        if (!understood) {
            static const std::pair<const char*, int> moods[] = {{"night", 2}, {"sunset", 1}, {"evening", 1}, {"day", 0},
                                                                {"sunny", 0}, {"fog", 4}, {"space", 5}, {"underwater", 6},
                                                                {"neon", 7}, {"candy", 8}, {"cloud", 3}, {"overcast", 3}};
            for (auto& [word, preset] : moods)
                if (hasWord(c, word) && any(c, {"make it", "make the", "set", "change", "lighting", "scene", "sky", "time"})) {
                    int p = preset;
                    assistant_.proposals.push_back({std::string("Lighting preset: ") + word, [p](Editor& ed) { ed.applyLighting(p); }, true});
                    understood = true;
                    assistant_.understood.push_back("lighting");
                    break;
                }
        }
        if (!understood) {
            if (auto col = colorIn(c); col && any(c, {"background", "sky"})) {
                Color cc = *col;
                assistant_.proposals.push_back({"Camera > Background" + arrow() + trim(original), [cc](Editor& ed) {
                                                    if (Entity cam = SceneRenderer::findCamera(ed.scene()))
                                                        ed.scene().registry().get<Camera>(cam).background = cc;
                                                },
                                                true});
                understood = true;
            }
        }
        if (!understood)
            assistant_.unknown.push_back(original);
    }
    if (targets.empty() && assistant_.proposals.empty())
        assistant_.notes.push_back("Select an object first, then tell me what to change about it.");
}

void Editor::ensureBulletPrefab() {
    std::error_code ec;
    if (stdfs::exists(projectDir_ / "prefabs/bullet.prefab", ec))
        return;
    stdfs::create_directories(projectDir_ / "prefabs", ec);
    const char* text = R"({
  "entities": [
    {
      "id": "00000000000000b1",
      "name": "Bullet",
      "tag": "bullet",
      "components": {
        "Transform": {"position": [0, 0, 0]},
        "SpriteRenderer": {"shape": "Circle", "color": [1, 0.85, 0.2, 1], "size": [0.25, 0.25], "order": 4},
        "RigidBody2D": {"gravity_scale": 0, "fixed_rotation": true, "continuous": true},
        "CircleCollider2D": {"radius": 0.12, "is_trigger": true},
        "Lifetime": {"seconds": 3, "fade_out": false}
      }
    }
  ]
}
)";
    fs::writeText(projectDir_ / "prefabs/bullet.prefab", text);
    scanAssets();
}

// ---------------------------------------------------------------- UI

void Editor::drawAssistant() {
    if (!unlocked(Feature::Assistant))
        return;
    ImGui::PushID("ask");
    if (assistantFocus_) {
        ImGui::SetKeyboardFocusHere();
        assistantFocus_ = false;
    }
    static const char* examples[] = {"e.g. make it faster and patrol left and right", "e.g. bigger and red", "e.g. chase the player",
                                     "e.g. make it a collectible coin worth 5", "e.g. spin slowly and glow", "e.g. let me control it",
                                     "e.g. dangerous, and bounce"};
    int ex = static_cast<int>(ImGui::GetTime() / 4) % IM_ARRAYSIZE(examples);
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Ask").x - 24);
    bool enter = ImGui::InputTextWithHint("##ask", examples[ex], &assistantText_, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ask Aven: describe a change in plain words. You'll see what it changes before anything happens.");
    ImGui::SameLine();
    if ((ImGui::Button("Ask") || enter) && !trim(assistantText_).empty()) {
        askAven(assistantText_);
        assistantFocus_ = true;
    }
    if (!assistant_.request.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##askresult", {0, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
        if (assistant_.proposals.empty()) {
            ImGui::TextWrapped("I'm not sure how to do that yet.");
        } else {
            ImGui::TextDisabled("Here's what will change:");
            for (size_t i = 0; i < assistant_.proposals.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Checkbox("##on", &assistant_.proposals[i].enabled);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", assistant_.proposals[i].text.c_str());
                ImGui::PopID();
            }
        }
        for (auto& n : assistant_.notes)
            ImGui::TextColored({1, 0.8f, 0.4f, 1}, "%s", n.c_str());
        if (!assistant_.unknown.empty()) {
            std::string list;
            for (auto& u : assistant_.unknown)
                list += (list.empty() ? "\"" : ", \"") + u + "\"";
            ImGui::TextWrapped("I didn't understand %s. Try words like faster, bigger, red, spin, patrol, chase the player, "
                               "collectible, dangerous, bouncy, shoot, glow, fire, or let me control it.",
                               list.c_str());
        }
        if (!assistant_.proposals.empty()) {
            bool anyOn = std::any_of(assistant_.proposals.begin(), assistant_.proposals.end(), [](auto& p) { return p.enabled; });
            ImGui::BeginDisabled(!anyOn || playing_);
            if (ImGui::Button("Do it")) {
                recordUndo("Ask Aven: " + assistant_.request);
                for (auto& p : assistant_.proposals)
                    if (p.enabled)
                        p.apply(*this);
                milestone("settings_changed", 3);
                notify("Done! The changed settings are highlighted. Ctrl+Z undoes it.");
                assistant_ = {};
                assistantText_.clear();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel"))
            assistant_ = {};
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

} // namespace aven::editor
