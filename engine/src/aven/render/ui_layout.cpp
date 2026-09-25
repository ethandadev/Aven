#include "aven/render/ui_layout.h"

namespace aven {

static Vec2 anchorFraction(Anchor a) {
    switch (a) {
    case Anchor::TopLeft: return {0, 1};
    case Anchor::Top: return {0.5f, 1};
    case Anchor::TopRight: return {1, 1};
    case Anchor::Left: return {0, 0.5f};
    case Anchor::Center: return {0.5f, 0.5f};
    case Anchor::Right: return {1, 0.5f};
    case Anchor::BottomLeft: return {0, 0};
    case Anchor::Bottom: return {0.5f, 0};
    case Anchor::BottomRight: return {1, 0};
    }
    return {0.5f, 0.5f};
}

UIRect computeUIRect(const Scene& scene, Entity e, float w, float h) {
    float scale = h / kUIReferenceHeight;
    UIRect area{{0, 0}, {w, h}, scale};
    Entity p = scene.parent(e);
    if (p && scene.registry().has<UIElement>(p))
        area = computeUIRect(scene, p, w, h);
    const UIElement* ui = scene.registry().tryGet<UIElement>(e);
    if (!ui)
        return area;
    Vec2 f = anchorFraction(ui->anchor);
    Vec2 anchorPoint = area.min + (area.max - area.min) * f;
    Vec2 size = ui->size * scale;
    const Transform* t = scene.registry().tryGet<Transform>(e);
    if (t)
        size = size * Vec2(t->scale.x, t->scale.y);
    Vec2 mn = anchorPoint + ui->offset * scale - size * f;
    return {mn, mn + size, scale};
}

} // namespace aven
