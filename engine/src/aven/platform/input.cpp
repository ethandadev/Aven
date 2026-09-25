#include "aven/platform/input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace aven {

namespace {

struct NamedKey {
    const char* name;
    int key;
};

const NamedKey kNamedKeys[] = {
    {"space", keys::Space},         {"enter", keys::Enter},         {"return", keys::Enter},
    {"escape", keys::Escape},       {"esc", keys::Escape},          {"tab", keys::Tab},
    {"backspace", keys::Backspace}, {"delete", keys::Delete},       {"insert", keys::Insert},
    {"left", keys::Left},           {"right", keys::Right},         {"up", keys::Up},
    {"down", keys::Down},           {"page_up", keys::PageUp},      {"page_down", keys::PageDown},
    {"home", keys::Home},           {"end", keys::End},             {"left_shift", keys::LeftShift},
    {"right_shift", keys::RightShift}, {"left_ctrl", keys::LeftControl}, {"right_ctrl", keys::RightControl},
    {"left_alt", keys::LeftAlt},    {"right_alt", keys::RightAlt},  {"minus", keys::Minus},
    {"equals", keys::Equal},        {"comma", keys::Comma},         {"period", keys::Period},
    {"slash", keys::Slash},         {"semicolon", keys::Semicolon}, {"apostrophe", keys::Apostrophe},
    {"left_bracket", keys::LeftBracket}, {"right_bracket", keys::RightBracket}, {"backslash", keys::Backslash},
    {"backquote", keys::GraveAccent},
};

const char* kPadButtonNames[] = {"pad_a",  "pad_b",     "pad_x",   "pad_y",   "pad_lb",   "pad_rb",   "pad_back",  "pad_start",
                                 "pad_guide", "pad_ls", "pad_rs", "pad_up", "pad_right", "pad_down", "pad_left"};

std::string normalize(std::string_view name) {
    std::string out;
    for (char c : name) {
        if (c == ' ' || c == '-')
            out += '_';
        else
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (out == "arrow_left" || out == "left_arrow")
        return "left";
    if (out == "arrow_right" || out == "right_arrow")
        return "right";
    if (out == "arrow_up" || out == "up_arrow")
        return "up";
    if (out == "arrow_down" || out == "down_arrow")
        return "down";
    return out;
}

} // namespace

Input::Input() : actions_(defaultActions()) {}

void Input::beginFrame() {
    prevKeys_ = keys_;
    prevMouse_ = mouse_;
    std::copy(std::begin(pad_), std::end(pad_), std::begin(prevPad_));
    prevMousePos_ = mousePos_;
    scroll_ = {};
    typed_.clear();
}

void Input::onKey(int key, bool down) {
    if (key >= 0 && key < keys::Count)
        keys_[static_cast<size_t>(key)] = down;
}

void Input::onMouseButton(int button, bool down) {
    if (button >= 0 && button < static_cast<int>(mouse_.size()))
        mouse_[static_cast<size_t>(button)] = down;
}

void Input::onMouseMove(Vec2 position) { mousePos_ = position; }
void Input::onScroll(Vec2 delta) { scroll_ += delta; }
void Input::onChar(uint32_t codepoint) { typed_ += static_cast<char32_t>(codepoint); }

void Input::setGamepad(bool connected, const bool buttons[], const float axes[]) {
    padConnected_ = connected;
    for (int i = 0; i < static_cast<int>(PadButton::Count); ++i)
        pad_[i] = connected && buttons[i];
    for (int i = 0; i < static_cast<int>(PadAxis::Count); ++i)
        padAxes_[i] = connected ? axes[i] : 0.0f;
}

void Input::releaseAll() {
    keys_.fill(false);
    mouse_.fill(false);
    std::fill(std::begin(pad_), std::end(pad_), false);
}

void Input::mirror(const Input& src, Vec2 mouseOffset) {
    keys_ = src.keys_;
    prevKeys_ = src.prevKeys_;
    mouse_ = src.mouse_;
    prevMouse_ = src.prevMouse_;
    std::copy(std::begin(src.pad_), std::end(src.pad_), std::begin(pad_));
    std::copy(std::begin(src.prevPad_), std::end(src.prevPad_), std::begin(prevPad_));
    std::copy(std::begin(src.padAxes_), std::end(src.padAxes_), std::begin(padAxes_));
    padConnected_ = src.padConnected_;
    mousePos_ = src.mousePos_ - mouseOffset;
    prevMousePos_ = src.prevMousePos_ - mouseOffset;
    scroll_ = src.scroll_;
    typed_ = src.typed_;
}

void Input::reset() {
    keys_.fill(false);
    prevKeys_.fill(false);
    mouse_.fill(false);
    prevMouse_.fill(false);
    std::fill(std::begin(pad_), std::end(pad_), false);
    std::fill(std::begin(prevPad_), std::end(prevPad_), false);
    scroll_ = {};
    typed_.clear();
    prevMousePos_ = mousePos_;
}

bool Input::keyDown(int key) const { return key >= 0 && key < keys::Count && keys_[static_cast<size_t>(key)]; }
bool Input::keyPressed(int key) const {
    return key >= 0 && key < keys::Count && keys_[static_cast<size_t>(key)] && !prevKeys_[static_cast<size_t>(key)];
}
bool Input::keyReleased(int key) const {
    return key >= 0 && key < keys::Count && !keys_[static_cast<size_t>(key)] && prevKeys_[static_cast<size_t>(key)];
}

bool Input::anyKeyPressed() const {
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] && !prevKeys_[i])
            return true;
    for (size_t i = 0; i < mouse_.size(); ++i)
        if (mouse_[i] && !prevMouse_[i])
            return true;
    return false;
}

int Input::keyFromName(std::string_view raw) {
    std::string name = normalize(raw);
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z')
            return keys::A + (c - 'a');
        if (c >= '0' && c <= '9')
            return keys::Num0 + (c - '0');
    }
    if (name.size() >= 2 && name[0] == 'f' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12)
            return keys::F1 + n - 1;
    }
    for (auto& k : kNamedKeys)
        if (name == k.name)
            return k.key;
    return -1;
}

std::string Input::nameOfKey(int key) {
    if (key >= keys::A && key <= keys::Z)
        return std::string(1, static_cast<char>('a' + key - keys::A));
    if (key >= keys::Num0 && key <= keys::Num9)
        return std::string(1, static_cast<char>('0' + key - keys::Num0));
    if (key >= keys::F1 && key <= keys::F12)
        return "f" + std::to_string(key - keys::F1 + 1);
    for (auto& k : kNamedKeys)
        if (k.key == key)
            return k.name;
    return "";
}

bool Input::isValidName(std::string_view raw) {
    std::string name = normalize(raw);
    if (keyFromName(name) >= 0)
        return true;
    for (const char* n : {"mouse_left", "mouse_right", "mouse_middle", "shift", "ctrl", "control", "alt", "any"})
        if (name == n)
            return true;
    for (const char* p : kPadButtonNames)
        if (name == p)
            return true;
    return false;
}

std::vector<std::string> Input::allNames() {
    std::vector<std::string> out;
    for (char c = 'a'; c <= 'z'; ++c)
        out.emplace_back(1, c);
    for (char c = '0'; c <= '9'; ++c)
        out.emplace_back(1, c);
    for (auto& k : kNamedKeys)
        out.push_back(k.name);
    for (const char* n : {"mouse_left", "mouse_right", "mouse_middle", "shift", "ctrl", "alt", "any"})
        out.push_back(n);
    for (const char* p : kPadButtonNames)
        out.push_back(p);
    return out;
}

bool Input::query(std::string_view raw, Phase phase) const {
    std::string name = normalize(raw);
    auto check = [&](bool now, bool before) {
        switch (phase) {
        case Phase::Down: return now;
        case Phase::Pressed: return now && !before;
        case Phase::Released: return !now && before;
        }
        return false;
    };
    int key = keyFromName(name);
    if (key >= 0)
        return check(keys_[static_cast<size_t>(key)], prevKeys_[static_cast<size_t>(key)]);
    auto either = [&](int a, int b) {
        return check(keys_[static_cast<size_t>(a)] || keys_[static_cast<size_t>(b)],
                     prevKeys_[static_cast<size_t>(a)] || prevKeys_[static_cast<size_t>(b)]);
    };
    if (name == "shift")
        return either(keys::LeftShift, keys::RightShift);
    if (name == "ctrl" || name == "control")
        return either(keys::LeftControl, keys::RightControl);
    if (name == "alt")
        return either(keys::LeftAlt, keys::RightAlt);
    if (name == "mouse_left")
        return check(mouse_[0], prevMouse_[0]);
    if (name == "mouse_right")
        return check(mouse_[1], prevMouse_[1]);
    if (name == "mouse_middle")
        return check(mouse_[2], prevMouse_[2]);
    if (name == "any") {
        bool now = false, before = false;
        for (size_t i = 0; i < keys_.size(); ++i) {
            now |= keys_[i];
            before |= prevKeys_[i];
        }
        return check(now, before);
    }
    for (int i = 0; i < static_cast<int>(PadButton::Count); ++i)
        if (name == kPadButtonNames[i])
            return check(pad_[i], prevPad_[i]);
    return false;
}

bool Input::down(std::string_view name) const {
    return hasAction(name) ? queryAction(name, Phase::Down) : query(name, Phase::Down);
}
bool Input::pressed(std::string_view name) const {
    return hasAction(name) ? queryAction(name, Phase::Pressed) : query(name, Phase::Pressed);
}
bool Input::released(std::string_view name) const {
    return hasAction(name) ? queryAction(name, Phase::Released) : query(name, Phase::Released);
}

void Input::setActions(std::vector<InputAction> actions) { actions_ = std::move(actions); }

InputAction* Input::findAction(std::string_view name) {
    std::string n = normalize(name);
    for (auto& a : actions_)
        if (a.name == n)
            return &a;
    return nullptr;
}

bool Input::hasAction(std::string_view name) const {
    std::string n = normalize(name);
    // A single key name is never treated as an action (so "left" means the arrow key).
    if (keyFromName(n) >= 0)
        return false;
    for (auto& a : actions_)
        if (a.name == n)
            return true;
    return false;
}

bool Input::queryAction(std::string_view name, Phase phase) const {
    std::string n = normalize(name);
    for (auto& a : actions_) {
        if (a.name != n)
            continue;
        if (phase == Phase::Down) {
            for (auto& b : a.bindings)
                if (query(b, Phase::Down))
                    return true;
            return false;
        }
        // Pressed/released consider the action as a whole, so holding one key and
        // pressing another bound key doesn't count as a new press.
        bool now = false, any = false;
        for (auto& b : a.bindings) {
            now |= query(b, Phase::Down);
            any |= query(b, phase);
        }
        return phase == Phase::Pressed ? (any && now) : (any && !now);
    }
    return false;
}

bool Input::actionDown(std::string_view a) const { return queryAction(a, Phase::Down); }
bool Input::actionPressed(std::string_view a) const { return queryAction(a, Phase::Pressed); }
bool Input::actionReleased(std::string_view a) const { return queryAction(a, Phase::Released); }

float Input::axis(std::string_view raw) const {
    std::string name = normalize(raw);
    const float dead = 0.2f;
    auto stick = [&](PadAxis a) {
        float v = padAxes_[static_cast<int>(a)];
        return std::abs(v) < dead ? 0.0f : v;
    };
    float v = 0;
    if (name == "horizontal" || name == "x") {
        v = (queryAction("right", Phase::Down) ? 1.0f : 0.0f) - (queryAction("left", Phase::Down) ? 1.0f : 0.0f);
        if (v == 0)
            v = stick(PadAxis::LeftX);
    } else if (name == "vertical" || name == "y") {
        v = (queryAction("up", Phase::Down) ? 1.0f : 0.0f) - (queryAction("down", Phase::Down) ? 1.0f : 0.0f);
        if (v == 0)
            v = -stick(PadAxis::LeftY);
    } else if (name == "look_x") {
        v = stick(PadAxis::RightX);
    } else if (name == "look_y") {
        v = -stick(PadAxis::RightY);
    }
    return clamp(v, -1.0f, 1.0f);
}

std::vector<InputAction> Input::defaultActions() {
    return {
        {"left", {"left", "a", "pad_left"}},
        {"right", {"right", "d", "pad_right"}},
        {"up", {"up", "w", "pad_up"}},
        {"down", {"down", "s", "pad_down"}},
        {"jump", {"space", "pad_a"}},
        {"fire", {"z", "j", "mouse_left", "pad_x"}},
        {"action", {"e", "enter", "pad_b"}},
        {"pause", {"escape", "p", "pad_start"}},
    };
}

Json Input::saveActions() const {
    Json j = Json::object();
    for (auto& a : actions_) {
        Json list = Json::array();
        for (auto& b : a.bindings)
            list.push(b);
        j[a.name] = std::move(list);
    }
    return j;
}

void Input::loadActions(const Json& j) {
    if (!j.isObject())
        return;
    std::vector<InputAction> result = defaultActions();
    for (auto& m : j.members()) {
        InputAction a{normalize(m.key), {}};
        for (auto& b : m.value.elements())
            a.bindings.push_back(normalize(b.asString()));
        auto it = std::find_if(result.begin(), result.end(), [&](const InputAction& x) { return x.name == a.name; });
        if (it != result.end())
            *it = std::move(a);
        else
            result.push_back(std::move(a));
    }
    actions_ = std::move(result);
}

} // namespace aven
