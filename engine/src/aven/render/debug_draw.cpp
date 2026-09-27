#include "aven/render/debug_draw.h"

#include "aven/render/renderer2d.h"
#include "aven/render/scene_renderer.h"

#include <algorithm>
#include <cmath>

namespace aven {

void DebugDraw::add(Shape s) {
    if (shapes_.size() < kMaxShapes)
        shapes_.push_back(std::move(s));
}

void DebugDraw::line(Vec3 a, Vec3 b, Color color, float seconds) {
    add({Kind::Line, a, b, 0, color, {}, std::max(seconds, 0.0f)});
}

void DebugDraw::circle(Vec3 center, float radius, Color color, float seconds) {
    add({Kind::Circle, center, {}, std::abs(radius), color, {}, std::max(seconds, 0.0f)});
}

void DebugDraw::box(Vec3 center, Vec3 size, Color color, float seconds) {
    add({Kind::Box, center, size, 0, color, {}, std::max(seconds, 0.0f)});
}

void DebugDraw::text(Vec3 at, std::string text, Color color, float seconds) {
    add({Kind::Text, at, {}, 0, color, std::move(text), std::max(seconds, 0.0f)});
}

void DebugDraw::tick(float dt) {
    // Shapes drawn last frame for 0 seconds (or whose time ran out) go away.
    std::erase_if(shapes_, [&](Shape& s) {
        s.remaining -= dt;
        return s.remaining < 0;
    });
}

namespace {

// A line as a thin quad facing the camera, `pixels` wide on screen.
void segment(Renderer2D& r, const CameraView& cam, Vec3 a, Vec3 b, Color c, float viewportHeight, rhi::TextureHandle white) {
    Vec3 dir = b - a;
    if (lengthSquared(dir) < 1e-12f)
        return;
    Vec3 mid = (a + b) * 0.5f;
    float worldPerPixel = cam.orthographic ? cam.orthoSize * 2.0f / viewportHeight
                                           : length(mid - cam.position) * 2.0f * std::tan(radians(cam.fieldOfView) * 0.5f) / viewportHeight;
    Vec3 toCam = cam.orthographic ? -cam.forward : normalize(cam.position - mid);
    Vec3 side = cross(normalize(dir), toCam);
    if (lengthSquared(side) < 1e-8f)
        return;
    side = normalize(side) * (2.0f * worldPerPixel * 0.5f);
    Vec3 corners[4] = {a - side, b - side, b + side, a + side};
    Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    r.quad(corners, uv, c, white);
}

} // namespace

void DebugDraw::drawWorld(Renderer2D& r, const CameraView& cam, float viewportHeight, rhi::TextureHandle white) const {
    if (shapes_.empty())
        return;
    // Circles face the camera: in 2D that's the XY plane.
    Mat4 camWorld = inverse(cam.view);
    Vec3 right = normalize(camWorld.column(0).xyz()), up = normalize(camWorld.column(1).xyz());
    r.begin(cam.viewProjection, true, false);
    for (const Shape& s : shapes_) {
        switch (s.kind) {
        case Kind::Line: segment(r, cam, s.a, s.b, s.color, viewportHeight, white); break;
        case Kind::Circle: {
            const int n = 40;
            for (int i = 0; i < n; ++i) {
                float a0 = static_cast<float>(i) * 2 * kPi / n, a1 = static_cast<float>(i + 1) * 2 * kPi / n;
                Vec3 p0 = s.a + (right * std::cos(a0) + up * std::sin(a0)) * s.radius;
                Vec3 p1 = s.a + (right * std::cos(a1) + up * std::sin(a1)) * s.radius;
                segment(r, cam, p0, p1, s.color, viewportHeight, white);
            }
            break;
        }
        case Kind::Box: {
            Vec3 h = s.b * 0.5f;
            if (std::abs(s.b.z) < 1e-6f) {
                Vec3 q[4] = {s.a + Vec3(-h.x, -h.y, 0), s.a + Vec3(h.x, -h.y, 0), s.a + Vec3(h.x, h.y, 0), s.a + Vec3(-h.x, h.y, 0)};
                for (int i = 0; i < 4; ++i)
                    segment(r, cam, q[i], q[(i + 1) % 4], s.color, viewportHeight, white);
            } else {
                Vec3 c[8];
                for (int i = 0; i < 8; ++i)
                    c[i] = s.a + Vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
                const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto& e : edges)
                    segment(r, cam, c[e[0]], c[e[1]], s.color, viewportHeight, white);
            }
            break;
        }
        case Kind::Text: break;
        }
    }
    r.end();
}

void DebugDraw::drawLabels(Renderer2D& r, const Font& font, const CameraView& cam, float width, float height) const {
    bool any = std::any_of(shapes_.begin(), shapes_.end(), [](const Shape& s) { return s.kind == Kind::Text; });
    if (!any)
        return;
    r.begin(Mat4::orthographic(0, width, 0, height, -1, 1), false);
    for (const Shape& s : shapes_) {
        if (s.kind != Kind::Text)
            continue;
        if (!cam.orthographic && dot(s.a - cam.position, cam.forward) <= 0)
            continue; // behind the camera
        Vec2 p = cam.worldToScreen(s.a, {width, height});
        float size = std::max(14.0f, height / 45.0f);
        // A dark outline keeps labels readable on any background.
        for (Vec2 o : {Vec2{1, -1}, Vec2{-1, 1}, Vec2{1, 1}, Vec2{-1, -1}})
            r.textAt(font, s.text, {p.x + o.x, height - p.y + o.y}, size, {0, 0, 0, s.color.a * 0.8f}, TextAlign::Center, true);
        r.textAt(font, s.text, {p.x, height - p.y}, size, s.color, TextAlign::Center, true);
    }
    r.end();
}

} // namespace aven
