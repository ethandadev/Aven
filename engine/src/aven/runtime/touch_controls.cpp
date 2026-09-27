#include "aven/runtime/touch_controls.h"

#include "aven/assets/assets.h"
#include "aven/platform/input.h"
#include "aven/render/renderer2d.h"

#include <algorithm>
#include <cmath>

namespace aven {

void TouchControls::configure(const TouchSettings& settings) {
    settings_ = settings;
    if (settings_.buttons.size() > 4)
        settings_.buttons.resize(4);
    buttonTouch_.assign(settings_.buttons.size(), -1);
    buttonDown_.assign(settings_.buttons.size(), false);
    stickTouch_ = -1;
    stick_ = {};
}

bool TouchControls::visible() const {
    return settings_.mode == TouchMode::Always || (settings_.mode == TouchMode::Auto && touched_);
}

TouchControls::Circle TouchControls::stickArea() const {
    float u = std::min(window_.x, window_.y);
    float r = u * 0.14f;
    return {{r * 1.35f + u * 0.03f, window_.y - r * 1.35f - u * 0.03f}, r};
}

TouchControls::Circle TouchControls::button(size_t i) const {
    float u = std::min(window_.x, window_.y);
    float r = u * 0.075f;
    // An arc around the bottom-right corner: the first button is the biggest target.
    const Vec2 spots[4] = {{2.0f, 2.3f}, {4.4f, 1.5f}, {2.2f, 4.8f}, {4.6f, 3.9f}};
    Vec2 s = spots[std::min<size_t>(i, 3)];
    return {{window_.x - s.x * r - u * 0.02f, window_.y - s.y * r - u * 0.02f}, i == 0 ? r * 1.15f : r};
}

void TouchControls::releaseAll(Input& input) {
    const int keys[4] = {keys::Left, keys::Right, keys::Up, keys::Down};
    for (int k = 0; k < 4; ++k)
        if (arrows_[k]) {
            input.onKey(keys[k], false);
            arrows_[k] = false;
        }
    for (size_t i = 0; i < buttonDown_.size(); ++i)
        if (buttonDown_[i]) {
            input.onKey(Input::keyFromName(settings_.buttons[i].key), false);
            buttonDown_[i] = false;
        }
    std::fill(buttonTouch_.begin(), buttonTouch_.end(), -1);
    stickTouch_ = mouseTouch_ = -1;
    stick_ = {};
}

void TouchControls::update(const std::vector<TouchPoint>& touches, Vec2 windowSize, Input& input, bool mirrored) {
    if (windowSize.x > 0 && windowSize.y > 0)
        window_ = windowSize;
    if (!touches.empty())
        touched_ = true;
    if (settings_.mode == TouchMode::Off || !visible()) {
        hadTouches_ = !touches.empty();
        return;
    }
    auto find = [&](int id) -> const TouchPoint* {
        for (auto& t : touches)
            if (t.id == id)
                return &t;
        return nullptr;
    };
    auto inside = [](Circle c, Vec2 p, float slack) { return length(p - c.center) <= c.radius * slack; };
    // Fingers that lifted let go of what they held.
    if (stickTouch_ >= 0 && !find(stickTouch_))
        stickTouch_ = -1;
    for (auto& b : buttonTouch_)
        if (b >= 0 && !find(b))
            b = -1;
    if (mouseTouch_ >= 0 && !find(mouseTouch_))
        mouseTouch_ = -1;
    // New fingers: on the stick, on a button, or else the mouse.
    auto taken = [&](int id) {
        return id == stickTouch_ || id == mouseTouch_ || std::find(buttonTouch_.begin(), buttonTouch_.end(), id) != buttonTouch_.end();
    };
    for (auto& t : touches) {
        if (taken(t.id))
            continue;
        if (settings_.stick && stickTouch_ < 0 && inside(stickArea(), t.position, 1.6f)) {
            stickTouch_ = t.id;
            continue;
        }
        bool onButton = false;
        for (size_t i = 0; i < settings_.buttons.size(); ++i)
            if (buttonTouch_[i] < 0 && inside(button(i), t.position, 1.25f)) {
                buttonTouch_[i] = t.id;
                onButton = true;
                break;
            }
        if (!onButton && mouseTouch_ < 0)
            mouseTouch_ = t.id;
    }

    // The stick.
    stick_ = {};
    if (const TouchPoint* t = stickTouch_ >= 0 ? find(stickTouch_) : nullptr) {
        Circle c = stickArea();
        Vec2 d = (t->position - c.center) / c.radius;
        d.y = -d.y; // up is up
        float len = length(d);
        stick_ = len > 1 ? d / len : d;
    }
    const int arrowKeys[4] = {keys::Left, keys::Right, keys::Up, keys::Down};
    const bool want[4] = {stick_.x < -0.35f, stick_.x > 0.35f, stick_.y > 0.35f, stick_.y < -0.35f};
    for (int k = 0; k < 4; ++k) {
        prevArrows_[k] = arrows_[k];
        if (mirrored)
            input.holdKey(arrowKeys[k], want[k], prevArrows_[k]);
        else if (want[k] != arrows_[k])
            input.onKey(arrowKeys[k], want[k]);
        arrows_[k] = want[k];
    }
    // The buttons.
    prevButtons_.resize(buttonDown_.size(), false);
    for (size_t i = 0; i < settings_.buttons.size(); ++i) {
        bool down = buttonTouch_[i] >= 0;
        int key = Input::keyFromName(settings_.buttons[i].key);
        prevButtons_[i] = buttonDown_[i];
        if (mirrored)
            input.holdKey(key, down, prevButtons_[i]);
        else if (down != buttonDown_[i] && key >= 0)
            input.onKey(key, down);
        buttonDown_[i] = down;
    }
    // The mouse follows the free finger. Fingers on controls never click the game.
    if (!touches.empty() || hadTouches_) {
        if (const TouchPoint* t = mouseTouch_ >= 0 ? find(mouseTouch_) : nullptr) {
            input.onMouseMove(t->position);
            if (!input.mouseDown(MouseButton::Left))
                input.onMouseButton(0, true);
        } else if (input.mouseDown(MouseButton::Left)) {
            input.onMouseButton(0, false);
        }
    }
    hadTouches_ = !touches.empty();
}

void TouchControls::draw(Renderer2D& r, Assets& assets, Vec2 fb) const {
    if (!visible())
        return;
    float sx = fb.x / std::max(window_.x, 1.0f), sy = fb.y / std::max(window_.y, 1.0f);
    rhi::TextureHandle circle = assets.shape(Shape2D::Circle).handle;
    auto disc = [&](Vec2 center, float radius, Color color) {
        Vec2 c{center.x * sx, fb.y - center.y * sy};
        Vec2 half{radius * sx, radius * sy};
        r.rect(c - half, c + half, color, circle);
    };
    r.begin(Mat4::orthographic(0, fb.x, 0, fb.y, -1, 1), false);
    if (settings_.stick) {
        Circle s = stickArea();
        disc(s.center, s.radius * 1.02f, {1, 1, 1, 0.18f});
        disc(s.center, s.radius * 0.92f, {0.05f, 0.07f, 0.1f, 0.22f});
        Vec2 knob = s.center + Vec2{stick_.x, -stick_.y} * (s.radius * 0.55f);
        disc(knob, s.radius * 0.42f, {1, 1, 1, stickTouch_ >= 0 ? 0.65f : 0.4f});
    }
    const Font& font = assets.defaultFont();
    for (size_t i = 0; i < settings_.buttons.size(); ++i) {
        Circle b = button(i);
        bool down = i < buttonDown_.size() && buttonDown_[i];
        disc(b.center, b.radius, {1, 1, 1, down ? 0.55f : 0.28f});
        disc(b.center, b.radius * 0.86f, {0.05f, 0.07f, 0.1f, down ? 0.1f : 0.25f});
        std::string label = settings_.buttons[i].label.empty() ? settings_.buttons[i].key : settings_.buttons[i].label;
        float size = b.radius * sy * (label.size() <= 2 ? 0.75f : 1.6f / static_cast<float>(label.size()) + 0.12f);
        r.textAt(font, label, {b.center.x * sx, fb.y - b.center.y * sy}, size, {1, 1, 1, 0.9f},
                 TextAlign::Center, true);
    }
    r.end();
}

} // namespace aven
