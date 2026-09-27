#pragma once

// On-screen controls for phones and tablets (Project Settings > Touch controls): a stick in the
// bottom-left that presses the arrow keys, and up to four buttons in the bottom-right that press
// keys. Everything else a finger does acts like the mouse, so tapping game buttons still works.

#include "aven/math/math.h"
#include "aven/runtime/project.h"

#include <vector>

namespace aven {

class Assets;
class Input;
class Renderer2D;

struct TouchPoint {
    int id = 0;
    Vec2 position; // window coordinates, origin top-left (like the mouse)
};

class TouchControls {
public:
    void configure(const TouchSettings& settings);
    // Every finger on the screen this frame. Presses and releases keys on `input`, and moves the
    // mouse for fingers that aren't on a control.
    // `mirrored`: the input is rebuilt every frame (the editor), so held keys are applied again.
    void update(const std::vector<TouchPoint>& touches, Vec2 windowSize, Input& input, bool mirrored = false);
    // Draws them over the game (`framebuffer` pixels; the window may be scaled for the screen).
    void draw(Renderer2D& r, Assets& assets, Vec2 framebuffer) const;
    bool visible() const;
    void releaseAll(Input& input);

    // Layout, in window coordinates (for tests and the editor's preview).
    struct Circle {
        Vec2 center;
        float radius;
    };
    Circle stickArea() const;
    Circle button(size_t i) const;
    Vec2 stickValue() const { return stick_; } // -1..1, y up

private:
    TouchSettings settings_;
    Vec2 window_{1280, 720};
    bool touched_ = false; // Auto shows them after the first touch
    int stickTouch_ = -1;
    Vec2 stick_;
    std::vector<int> buttonTouch_;          // which finger holds each button (-1: none)
    bool arrows_[4] = {false, false, false, false}; // left, right, up, down
    bool prevArrows_[4] = {false, false, false, false};
    std::vector<bool> prevButtons_;
    std::vector<bool> buttonDown_;
    int mouseTouch_ = -1;
    bool hadTouches_ = false;
};

} // namespace aven
