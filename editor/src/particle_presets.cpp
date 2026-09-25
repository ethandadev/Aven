#include "particle_presets.h"

#include <algorithm>
#include <cctype>

namespace aven::editor {

namespace {

ParticleEmitter make(float rate, int burst, float life, float speed, float spread, Vec3 dir, Vec3 gravity, EmitterShape shape,
                     float shapeSize, uint32_t start, float startA, uint32_t end, float endA, float startSize, float endSize,
                     bool additive) {
    ParticleEmitter p;
    p.rate = rate;
    p.burst = burst;
    p.lifetime = life;
    p.speed = speed;
    p.spread = spread;
    p.direction = dir;
    p.gravity = gravity;
    p.shapeType = shape;
    p.shapeSize = shapeSize;
    Color s = Color::fromHex(start), e = Color::fromHex(end);
    p.startColor = {s.r, s.g, s.b, startA};
    p.endColor = {e.r, e.g, e.b, endA};
    p.startSize = startSize;
    p.endSize = endSize;
    p.additive = additive;
    return p;
}

} // namespace

const std::vector<ParticlePreset>& particlePresets() {
    using S = EmitterShape;
    static const std::vector<ParticlePreset> list = {
        {"Fire", "Flickering flames that rise and fade.",
         make(60, 0, 0.8f, 2.2f, 18, {0, 1, 0}, {0, 1.5f, 0}, S::Circle, 0.25f, 0xFFD34D, 1, 0xFF3B1F, 0, 0.5f, 0.05f, true)},
        {"Smoke", "Soft grey puffs drifting up.",
         make(18, 0, 2.5f, 0.9f, 25, {0, 1, 0}, {0, 0.3f, 0}, S::Circle, 0.3f, 0x9CA3AF, 0.6f, 0x4B5563, 0, 0.4f, 1.4f, false)},
        {"Sparkles", "Twinkling stars that burst outward.",
         make(20, 0, 0.8f, 2.5f, 180, {0, 1, 0}, {0, -1, 0}, S::Point, 0.1f, 0xFFF7AE, 1, 0xFDE047, 0, 0.18f, 0.0f, true)},
        {"Magic", "Swirling purple glitter.",
         make(35, 0, 1.2f, 1.2f, 180, {0, 1, 0}, {0, 0.5f, 0}, S::Circle, 0.5f, 0xE9D5FF, 1, 0x9333EA, 0, 0.2f, 0.0f, true)},
        {"Explosion", "One big bang of fire and sparks.",
         make(0, 60, 0.7f, 7, 180, {0, 1, 0}, {0, -3, 0}, S::Point, 0.1f, 0xFDE047, 1, 0xEF4444, 0, 0.45f, 0.05f, true)},
        {"Confetti", "Colorful paper for celebrations.",
         make(0, 80, 2.5f, 7, 50, {0, 1, 0}, {0, -6, 0}, S::Point, 0.1f, 0xF472B6, 1, 0x60A5FA, 1, 0.18f, 0.18f, false)},
        {"Rain", "Falling rain across the screen.",
         make(120, 0, 1.2f, 12, 3, {0, -1, 0}, {0, -4, 0}, S::Box, 12, 0xBFDBFE, 0.7f, 0x93C5FD, 0.3f, 0.06f, 0.06f, false)},
        {"Snow", "Gentle snowflakes.",
         make(40, 0, 6, 0.8f, 30, {0, -1, 0}, {0.1f, -0.4f, 0}, S::Box, 12, 0xFFFFFF, 0.95f, 0xE0F2FE, 0.4f, 0.12f, 0.1f, false)},
        {"Bubbles", "Round bubbles floating up.",
         make(10, 0, 3, 1, 25, {0, 1, 0}, {0, 0.4f, 0}, S::Box, 1, 0xBAE6FD, 0.6f, 0xE0F2FE, 0, 0.2f, 0.35f, false)},
        {"Dust", "Little puffs for footsteps and landings.",
         make(0, 12, 0.5f, 1.5f, 70, {0, 1, 0}, {0, -1, 0}, S::Point, 0.1f, 0xD6C7A1, 0.8f, 0xA89779, 0, 0.2f, 0.35f, false)},
        {"Stars", "A calm starry background.",
         make(18, 70, 5, 0.2f, 180, {0, -1, 0}, {0, 0, 0}, S::Box, 11, 0xFFFFFF, 0.9f, 0x93C5FD, 0.2f, 0.07f, 0.05f, true)},
        {"Hearts", "Floating hearts. Aww.",
         make(6, 0, 2, 1, 40, {0, 1, 0}, {0, 0.6f, 0}, S::Circle, 0.4f, 0xFB7185, 1, 0xF43F5E, 0, 0.25f, 0.1f, false)},
    };
    return list;
}

const ParticlePreset* findParticlePreset(const std::string& name) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::string n = lower(name);
    for (auto& p : particlePresets()) {
        std::string pn = lower(p.name);
        // "sparkle" finds "Sparkles", "fires" finds "Fire".
        if (pn == n || pn == n + "s" || pn + "s" == n || (n.size() > 3 && pn.rfind(n, 0) == 0))
            return &p;
    }
    return nullptr;
}

} // namespace aven::editor
