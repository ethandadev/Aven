#pragma once

// Debug drawing for scripts: lines, circles, boxes and labels drawn over the game for a while
// (debug_line(), debug_circle()... in EasyScript). Shown in the editor while playing; exported
// games only show them when run with --debug-draw.

#include "aven/math/math.h"
#include "aven/render/rhi.h"

#include <string>
#include <vector>

namespace aven {

class Renderer2D;
class Font;
struct CameraView;

class DebugDraw {
public:
    enum class Kind { Line, Circle, Box, Text };
    struct Shape {
        Kind kind = Kind::Line;
        Vec3 a, b; // line: ends; circle/text: a = center; box: a = center, b = size
        float radius = 0;
        Color color{1, 1, 0, 1};
        std::string text;
        float remaining = 0; // seconds left; a shape drawn for 0 seconds lasts one frame
    };

    void line(Vec3 a, Vec3 b, Color color, float seconds);
    void circle(Vec3 center, float radius, Color color, float seconds);
    void box(Vec3 center, Vec3 size, Color color, float seconds); // size.z == 0: a flat 2D rectangle
    void text(Vec3 at, std::string text, Color color, float seconds);

    // Ages the shapes; call once per frame before scripts run.
    void tick(float dt);
    void clear() { shapes_.clear(); }
    const std::vector<Shape>& shapes() const { return shapes_; }

    // Lines, circles and boxes into the world (the scene pass), labels on top of the final image.
    void drawWorld(Renderer2D& r, const CameraView& camera, float viewportHeight, rhi::TextureHandle white) const;
    void drawLabels(Renderer2D& r, const Font& font, const CameraView& camera, float width, float height) const;

private:
    static constexpr size_t kMaxShapes = 20000; // a runaway loop can't eat all memory
    std::vector<Shape> shapes_;
    void add(Shape s);
};

} // namespace aven
