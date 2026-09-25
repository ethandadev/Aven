#pragma once

#include "aven/core/json.h"
#include "aven/math/math.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aven {

// Key codes match GLFW so the platform layer can pass them straight through.
namespace keys {
constexpr int Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47;
constexpr int Num0 = 48, Num9 = 57, Semicolon = 59, Equal = 61, A = 65, Z = 90;
constexpr int LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96;
constexpr int Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261;
constexpr int Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267, Home = 268, End = 269;
constexpr int F1 = 290, F12 = 301;
constexpr int LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343;
constexpr int RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347;
constexpr int Count = 349;
} // namespace keys

enum class MouseButton { Left = 0, Right = 1, Middle = 2 };

// Gamepad buttons follow the Xbox layout (same as GLFW).
enum class PadButton { A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb, Up, Right, Down, Left, Count };
enum class PadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

// Named inputs like "jump" or "fire" that map to several keys/buttons, so
// players can remap controls and scripts don't hard-code keys.
struct InputAction {
    std::string name;
    std::vector<std::string> bindings; // key names: "space", "w", "mouse_left", "pad_a", ...
};

class Input {
public:
    Input();

    // --- fed by the platform layer
    void beginFrame(); // call once per frame before new events arrive
    void onKey(int key, bool down);
    void onMouseButton(int button, bool down);
    void onMouseMove(Vec2 position);
    void onScroll(Vec2 delta);
    void onChar(uint32_t codepoint);
    void setGamepad(bool connected, const bool buttons[static_cast<int>(PadButton::Count)],
                    const float axes[static_cast<int>(PadAxis::Count)]);
    void releaseAll();

    // --- raw state
    bool keyDown(int key) const;
    bool keyPressed(int key) const;
    bool keyReleased(int key) const;
    bool mouseDown(MouseButton b) const { return mouse_[static_cast<int>(b)]; }
    bool mousePressed(MouseButton b) const { return mouse_[static_cast<int>(b)] && !prevMouse_[static_cast<int>(b)]; }
    bool mouseReleased(MouseButton b) const { return !mouse_[static_cast<int>(b)] && prevMouse_[static_cast<int>(b)]; }
    Vec2 mousePosition() const { return mousePos_; } // window pixels, origin top-left
    Vec2 mouseDelta() const { return mousePos_ - prevMousePos_; }
    Vec2 scroll() const { return scroll_; }
    const std::u32string& typedText() const { return typed_; }
    bool anyKeyPressed() const;

    // --- names used by scripts and blocks: "left", "space", "a", "mouse_left", "pad_a", ...
    // Unknown names return false; use isValidName() to report typos.
    bool down(std::string_view name) const;
    bool pressed(std::string_view name) const;
    bool released(std::string_view name) const;
    static bool isValidName(std::string_view name);
    static std::vector<std::string> allNames();
    static int keyFromName(std::string_view name); // -1 if not a keyboard key
    static std::string nameOfKey(int key);

    // --- actions and axes
    void setActions(std::vector<InputAction> actions);
    const std::vector<InputAction>& actions() const { return actions_; }
    InputAction* findAction(std::string_view name);
    bool actionDown(std::string_view action) const;
    bool actionPressed(std::string_view action) const;
    bool actionReleased(std::string_view action) const;
    bool hasAction(std::string_view action) const;
    // -1..1 from "horizontal" (left/right, A/D, stick) or "vertical" (down/up, S/W, stick).
    float axis(std::string_view name) const;

    static std::vector<InputAction> defaultActions();
    Json saveActions() const;
    void loadActions(const Json& j);

    bool gamepadConnected() const { return padConnected_; }

private:
    std::array<bool, keys::Count> keys_{};
    std::array<bool, keys::Count> prevKeys_{};
    std::array<bool, 8> mouse_{};
    std::array<bool, 8> prevMouse_{};
    bool pad_[static_cast<int>(PadButton::Count)]{};
    bool prevPad_[static_cast<int>(PadButton::Count)]{};
    float padAxes_[static_cast<int>(PadAxis::Count)]{};
    bool padConnected_ = false;
    Vec2 mousePos_, prevMousePos_, scroll_;
    std::u32string typed_;
    std::vector<InputAction> actions_;

    enum class Phase { Down, Pressed, Released };
    bool query(std::string_view name, Phase phase) const;
    bool queryAction(std::string_view action, Phase phase) const;
};

} // namespace aven
