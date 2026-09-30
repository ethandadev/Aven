#include "avatars.h"

#include <algorithm>
#include <cmath>

namespace rynax::editor {

const std::vector<Critter>& critters() {
    static const std::vector<Critter> list = {
        {"cat", "Cat"},     {"fox", "Fox"},     {"bear", "Bear"},   {"frog", "Frog"},   {"robot", "Robot"},
        {"ghost", "Ghost"}, {"alien", "Alien"}, {"bunny", "Bunny"}, {"penguin", "Penguin"}, {"slime", "Slime"},
    };
    return list;
}

namespace {

constexpr float kPi = 3.14159265f;

struct Pen {
    ImDrawList* dl;
    ImVec2 c;
    float r;
    ImVec2 at(float x, float y) const { return {c.x + x * r, c.y + y * r}; }
    void circle(float x, float y, float radius, ImU32 col) const { dl->AddCircleFilled(at(x, y), radius * r, col, 32); }
    void ellipse(float x, float y, float rx, float ry, ImU32 col, float rot = 0) const {
        dl->AddEllipseFilled(at(x, y), {rx * r, ry * r}, col, rot, 32);
    }
    void tri(float x1, float y1, float x2, float y2, float x3, float y3, ImU32 col) const {
        dl->AddTriangleFilled(at(x1, y1), at(x2, y2), at(x3, y3), col);
    }
    void rect(float x1, float y1, float x2, float y2, ImU32 col, float round = 0) const {
        dl->AddRectFilled(at(x1, y1), at(x2, y2), col, round * r);
    }
    void line(float x1, float y1, float x2, float y2, ImU32 col, float width) const {
        dl->AddLine(at(x1, y1), at(x2, y2), col, std::max(1.0f, width * r));
    }
    // A smile: the lower part of a circle.
    void smile(float x, float y, float radius, ImU32 col, float width, float open = 0.75f) const {
        dl->PathArcTo(at(x, y), radius * r, kPi * (0.5f - open * 0.5f), kPi * (0.5f + open * 0.5f), 16);
        dl->PathStroke(col, 0, std::max(1.0f, width * r));
    }
    // Two eyes, closed for a moment every few seconds.
    void eyes(float y, float apart, float size, ImU32 col, bool blink) const {
        for (float s : {-1.0f, 1.0f}) {
            if (blink) {
                line(s * apart - size, y, s * apart + size, y, col, size * 0.5f);
            } else {
                circle(s * apart, y, size, col);
                circle(s * apart + size * 0.35f, y - size * 0.35f, size * 0.35f, IM_COL32(255, 255, 255, 230));
            }
        }
    }
    void cheeks(float y, float apart, float size) const {
        circle(-apart, y, size, IM_COL32(255, 120, 150, 90));
        circle(apart, y, size, IM_COL32(255, 120, 150, 90));
    }
};

constexpr ImU32 kInk = IM_COL32(40, 34, 48, 255);
constexpr ImU32 kWhite = IM_COL32(250, 250, 252, 255);
constexpr ImU32 kPink = IM_COL32(255, 160, 180, 255);

} // namespace

void drawCritter(ImDrawList* dl, ImVec2 center, float radius, std::string_view id, ImU32 background, float time) {
    Pen p{dl, center, radius};
    dl->AddCircleFilled(center, radius, background, 48);
    // Everything below is in units of the radius, with the head a little below the middle.
    bool blink = time > 0 && std::fmod(time + static_cast<float>(id.size()) * 0.7f, 4.2f) < 0.13f;

    if (id == "fox") {
        ImU32 orange = IM_COL32(245, 140, 60, 255);
        p.tri(-0.62f, -0.05f, -0.48f, -0.72f, -0.08f, -0.35f, orange);
        p.tri(0.62f, -0.05f, 0.48f, -0.72f, 0.08f, -0.35f, orange);
        p.tri(-0.5f, -0.2f, -0.45f, -0.55f, -0.22f, -0.35f, kInk);
        p.tri(0.5f, -0.2f, 0.45f, -0.55f, 0.22f, -0.35f, kInk);
        p.ellipse(0, 0.1f, 0.62f, 0.5f, orange);
        p.tri(-0.62f, 0.12f, 0.62f, 0.12f, 0, 0.66f, kWhite);
        p.eyes(0.02f, 0.25f, 0.075f, kInk, blink);
        p.circle(0, 0.46f, 0.07f, kInk);
    } else if (id == "bear") {
        ImU32 brown = IM_COL32(166, 112, 72, 255), light = IM_COL32(222, 184, 140, 255);
        for (float s : {-1.0f, 1.0f}) {
            p.circle(s * 0.45f, -0.4f, 0.2f, brown);
            p.circle(s * 0.45f, -0.4f, 0.1f, light);
        }
        p.circle(0, 0.1f, 0.58f, brown);
        p.ellipse(0, 0.32f, 0.26f, 0.2f, light);
        p.eyes(0.0f, 0.24f, 0.07f, kInk, blink);
        p.ellipse(0, 0.25f, 0.09f, 0.06f, kInk);
        p.smile(0, 0.28f, 0.1f, kInk, 0.035f);
        p.cheeks(0.2f, 0.4f, 0.08f);
    } else if (id == "frog") {
        ImU32 green = IM_COL32(110, 200, 90, 255), dark = IM_COL32(70, 150, 60, 255);
        p.ellipse(0, 0.2f, 0.66f, 0.46f, green);
        for (float s : {-1.0f, 1.0f}) {
            p.circle(s * 0.32f, -0.2f, 0.22f, green);
            p.circle(s * 0.32f, -0.22f, 0.15f, kWhite);
            if (blink)
                p.line(s * 0.32f - 0.1f, -0.22f, s * 0.32f + 0.1f, -0.22f, kInk, 0.04f);
            else
                p.circle(s * 0.32f + 0.03f, -0.2f, 0.075f, kInk);
        }
        p.smile(0, 0.05f, 0.36f, dark, 0.05f, 0.55f);
        p.cheeks(0.3f, 0.45f, 0.09f);
    } else if (id == "robot") {
        ImU32 steel = IM_COL32(170, 180, 196, 255), glow = IM_COL32(90, 230, 255, 255);
        p.line(0, -0.45f, 0, -0.68f, steel, 0.05f);
        p.circle(0, -0.72f, 0.08f, IM_COL32(255, 90, 110, 255));
        p.rect(-0.66f, -0.02f, -0.54f, 0.3f, steel, 0.04f);
        p.rect(0.54f, -0.02f, 0.66f, 0.3f, steel, 0.04f);
        p.rect(-0.52f, -0.45f, 0.52f, 0.58f, steel, 0.16f);
        p.rect(-0.4f, -0.28f, 0.4f, 0.14f, IM_COL32(40, 50, 70, 255), 0.1f);
        if (blink) {
            p.line(-0.28f, -0.07f, -0.1f, -0.07f, glow, 0.05f);
            p.line(0.1f, -0.07f, 0.28f, -0.07f, glow, 0.05f);
        } else {
            p.rect(-0.28f, -0.16f, -0.12f, 0.02f, glow, 0.04f);
            p.rect(0.12f, -0.16f, 0.28f, 0.02f, glow, 0.04f);
        }
        for (int i = 0; i < 4; ++i)
            p.rect(-0.22f + i * 0.12f, 0.3f, -0.14f + i * 0.12f, 0.42f, IM_COL32(40, 50, 70, 255), 0.02f);
    } else if (id == "ghost") {
        p.circle(0, -0.05f, 0.5f, kWhite);
        p.rect(-0.5f, -0.05f, 0.5f, 0.5f, kWhite);
        for (int i = 0; i < 4; ++i)
            p.circle(-0.375f + i * 0.25f, 0.5f, 0.125f, kWhite);
        p.ellipse(-0.18f, -0.05f, 0.08f, blink ? 0.015f : 0.12f, kInk);
        p.ellipse(0.18f, -0.05f, 0.08f, blink ? 0.015f : 0.12f, kInk);
        p.ellipse(0, 0.2f, 0.08f, 0.07f, kInk);
        p.cheeks(0.12f, 0.32f, 0.07f);
    } else if (id == "alien") {
        ImU32 lime = IM_COL32(160, 230, 110, 255);
        for (float s : {-1.0f, 1.0f}) {
            p.line(s * 0.15f, -0.45f, s * 0.32f, -0.72f, lime, 0.04f);
            p.circle(s * 0.32f, -0.74f, 0.07f, lime);
        }
        p.ellipse(0, 0.05f, 0.5f, 0.58f, lime);
        p.ellipse(-0.2f, 0.0f, 0.15f, blink ? 0.02f : 0.22f, kInk, -0.5f);
        p.ellipse(0.2f, 0.0f, 0.15f, blink ? 0.02f : 0.22f, kInk, 0.5f);
        if (!blink) {
            p.circle(-0.16f, -0.08f, 0.04f, IM_COL32(255, 255, 255, 200));
            p.circle(0.24f, -0.08f, 0.04f, IM_COL32(255, 255, 255, 200));
        }
        p.smile(0, 0.3f, 0.1f, kInk, 0.035f);
    } else if (id == "bunny") {
        for (float s : {-1.0f, 1.0f}) {
            p.ellipse(s * 0.22f, -0.5f, 0.13f, 0.36f, kWhite, s * 0.15f);
            p.ellipse(s * 0.22f, -0.48f, 0.06f, 0.26f, kPink, s * 0.15f);
        }
        p.circle(0, 0.18f, 0.5f, kWhite);
        p.eyes(0.1f, 0.2f, 0.07f, kInk, blink);
        p.tri(-0.06f, 0.26f, 0.06f, 0.26f, 0, 0.33f, kPink);
        p.line(0, 0.33f, 0, 0.4f, kInk, 0.025f);
        p.cheeks(0.32f, 0.32f, 0.08f);
    } else if (id == "penguin") {
        ImU32 black = IM_COL32(45, 50, 64, 255);
        p.circle(0, 0.1f, 0.62f, black);
        p.circle(-0.2f, 0.12f, 0.3f, kWhite);
        p.circle(0.2f, 0.12f, 0.3f, kWhite);
        p.circle(0, 0.34f, 0.3f, kWhite);
        p.eyes(0.02f, 0.2f, 0.07f, kInk, blink);
        p.tri(-0.1f, 0.18f, 0.1f, 0.18f, 0, 0.32f, IM_COL32(255, 170, 50, 255));
        p.cheeks(0.24f, 0.36f, 0.07f);
    } else if (id == "slime") {
        ImU32 goo = IM_COL32(70, 205, 175, 255);
        p.circle(0, 0.2f, 0.52f, goo);
        p.rect(-0.52f, 0.2f, 0.52f, 0.62f, goo, 0.08f);
        p.circle(0, -0.2f, 0.28f, goo);
        p.ellipse(-0.26f, -0.12f, 0.08f, 0.14f, IM_COL32(255, 255, 255, 120), -0.4f);
        p.eyes(0.18f, 0.2f, 0.08f, kInk, blink);
        p.smile(0, 0.3f, 0.12f, kInk, 0.04f);
    } else { // cat
        ImU32 fur = IM_COL32(255, 196, 110, 255);
        p.tri(-0.58f, -0.1f, -0.46f, -0.72f, -0.1f, -0.4f, fur);
        p.tri(0.58f, -0.1f, 0.46f, -0.72f, 0.1f, -0.4f, fur);
        p.tri(-0.48f, -0.22f, -0.44f, -0.56f, -0.22f, -0.4f, kPink);
        p.tri(0.48f, -0.22f, 0.44f, -0.56f, 0.22f, -0.4f, kPink);
        p.ellipse(0, 0.1f, 0.62f, 0.52f, fur);
        p.eyes(0.02f, 0.25f, 0.075f, kInk, blink);
        p.tri(-0.06f, 0.18f, 0.06f, 0.18f, 0, 0.25f, kPink);
        p.smile(-0.06f, 0.25f, 0.06f, kInk, 0.03f, 0.9f);
        p.smile(0.06f, 0.25f, 0.06f, kInk, 0.03f, 0.9f);
        for (float s : {-1.0f, 1.0f}) {
            p.line(s * 0.3f, 0.2f, s * 0.62f, 0.14f, IM_COL32(40, 34, 48, 160), 0.02f);
            p.line(s * 0.3f, 0.26f, s * 0.62f, 0.3f, IM_COL32(40, 34, 48, 160), 0.02f);
        }
    }
}

} // namespace rynax::editor
