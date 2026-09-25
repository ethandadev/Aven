#pragma once

#include "aven/scene/scene.h"

namespace aven {

constexpr float kUIReferenceHeight = 720.0f;

struct UIRect {
    Vec2 min, max; // screen pixels, origin bottom-left
    float scale = 1; // reference pixels to screen pixels
    bool contains(Vec2 p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
    Vec2 center() const { return (min + max) * 0.5f; }
    Vec2 size() const { return max - min; }
};

// Screen rectangle of a UI element. Elements nested under another UI element are
// placed relative to their parent's rectangle instead of the whole screen.
UIRect computeUIRect(const Scene& scene, Entity e, float screenWidth, float screenHeight);

} // namespace aven
