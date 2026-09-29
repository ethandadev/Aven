#include "aven/runtime/intro.h"

#include "aven/assets/assets.h"
#include "aven/platform/input.h"
#include "aven/render/renderer2d.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace aven {

namespace {

const Color kAccent = Color::fromHex(0x3B82F6);
const Color kInk = Color::fromHex(0x0E1014);

float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

} // namespace

void Intro::begin(const ProjectSettings& settings, Assets& assets, bool canQuit) {
    const PublishSettings& p = settings.publish;
    canQuit_ = canQuit;
    quit_ = false;
    time_ = 0;
    focus_ = 0;
    hover_ = -1;
    name_ = settings.name;
    subtitle_ = settings.description;
    std::string copyright = p.copyright;
    if (copyright.empty() && !p.author.empty()) {
        std::time_t now = std::time(nullptr);
        const std::tm* local = std::localtime(&now); // (null if the clock is nonsense)
        copyright = "(c) " + (local ? std::to_string(1900 + local->tm_year) + " " : std::string()) + p.author;
    }
    footer_ = "v" + settings.version + (copyright.empty() ? "" : "   " + copyright);
    background_.clear();
    for (const std::string& candidate : {p.titleImage, std::string("thumbnail.png")})
        if (!candidate.empty() && assets.exists(candidate)) {
            background_ = candidate;
            break;
        }
    graphicsButton_ = p.graphicsButton;
    graphicsChanged_ = false;
    phase_ = p.splash ? Phase::Splash : p.titleScreen ? Phase::Title : Phase::Done;
    titleNext_ = p.titleScreen;
}

Intro::Layout Intro::layout(Vec2 size) const {
    Layout l;
    float u = std::min(size.y, size.x * 0.6f); // stays sensible in tall (phone) and wide windows
    l.titleSize = u * 0.11f;
    l.textSize = u * 0.036f;
    l.titleAt = {size.x * 0.5f, size.y * 0.34f};
    float bw = u * 0.42f, bh = u * 0.085f, top = size.y * (buttonCount() > 2 ? 0.56f : 0.6f);
    for (int i = 0; i < buttonCount(); ++i) {
        float y = top + static_cast<float>(i) * bh * 1.3f;
        l.buttonMin[i] = {size.x * 0.5f - bw * 0.5f, y};
        l.buttonMax[i] = {size.x * 0.5f + bw * 0.5f, y + bh};
    }
    return l;
}

void Intro::update(float dt, const Input& input, Vec2 window) {
    window_ = window;
    dt = std::min(dt, 0.1f); // a slow first frame (loading) shouldn't eat the splash
    if (phase_ == Phase::Splash) {
        time_ += dt;
        // Any key, click or tap skips it (after a moment, so a key still held from launching doesn't).
        bool skip = time_ > 0.2f && (input.anyKeyPressed() || input.pressed("pad_a") || input.pressed("pad_start"));
        if (skip && time_ < kSplashSeconds - 0.3f)
            time_ = kSplashSeconds - 0.3f; // straight to fading out
        if (time_ >= kSplashSeconds) {
            phase_ = titleNext_ ? Phase::Title : Phase::Done;
            time_ = 0;
        }
        return;
    }
    if (phase_ != Phase::Title)
        return;
    time_ += dt;
    Layout l = layout(window);
    Vec2 m = input.mousePosition();
    hover_ = -1;
    for (int i = 0; i < buttonCount(); ++i)
        if (m.x >= l.buttonMin[i].x && m.x <= l.buttonMax[i].x && m.y >= l.buttonMin[i].y && m.y <= l.buttonMax[i].y)
            hover_ = i;
    if (hover_ >= 0 && !(input.mouseDelta() == Vec2{}))
        focus_ = hover_;
    auto choose = [&](int i) {
        switch (button(i)) {
        case Button::Play: phase_ = Phase::Done; break;
        case Button::Quit: quit_ = true; break;
        case Button::Graphics: // Low -> Medium -> High -> Ultra -> Low
            graphics_ = static_cast<GraphicsQuality>((static_cast<int>(graphics_) + 1) % 4);
            graphicsChanged_ = true;
            break;
        }
    };
    if (input.pressed("up") || input.pressed("w") || input.pressed("pad_up"))
        focus_ = std::max(0, focus_ - 1);
    if (input.pressed("down") || input.pressed("s") || input.pressed("tab") || input.pressed("pad_down"))
        focus_ = std::min(buttonCount() - 1, focus_ + 1);
    if (time_ > 0.15f) {
        if (input.mousePressed(MouseButton::Left) && hover_ >= 0)
            choose(hover_);
        else if (input.pressed("enter") || input.pressed("space") || input.pressed("pad_a") || input.pressed("pad_start"))
            choose(focus_);
    }
}

void Intro::draw(Renderer2D& r, Assets& assets, Vec2 fb) const {
    if (phase_ == Phase::Done)
        return;
    const Font& font = assets.defaultFont();
    rhi::TextureHandle white = assets.white().handle;
    auto flip = [&](Vec2 p) { return Vec2{p.x, fb.y - p.y}; }; // layout is top-down; drawing is y-up
    r.begin(Mat4::orthographic(0, fb.x, 0, fb.y, -1, 1), false);
    float u = std::min(fb.y, fb.x * 0.6f);

    if (phase_ == Phase::Splash) {
        r.rect({0, 0}, fb, kInk, white);
        float a = smooth(time_ / 0.25f) * smooth((kSplashSeconds - time_) / 0.3f);
        // The Aven mark (a blue diamond with a dark middle) beside the name, "Made with" above.
        float big = u * 0.1f, mark = u * 0.13f, gap = u * 0.035f;
        float nameWidth = font.measure("Aven", big).x;
        float x0 = fb.x * 0.5f - (mark + gap + nameWidth) * 0.5f, cy = fb.y * 0.5f;
        rhi::TextureHandle diamond = assets.shape(Shape2D::Diamond).handle;
        Vec2 c{x0 + mark * 0.5f, cy};
        r.rect(c - Vec2{mark, mark} * 0.5f, c + Vec2{mark, mark} * 0.5f, {kAccent.r, kAccent.g, kAccent.b, a}, diamond);
        r.rect(c - Vec2{mark, mark} * 0.225f, c + Vec2{mark, mark} * 0.225f, {kInk.r, kInk.g, kInk.b, a}, diamond);
        r.textAt(font, "Aven", {x0 + mark + gap, cy}, big, {1, 1, 1, a}, TextAlign::Left, true);
        r.textAt(font, "Made with", {fb.x * 0.5f, cy + big * 0.95f}, u * 0.034f, {0.62f, 0.66f, 0.74f, a}, TextAlign::Center,
                 true);
        r.end();
        return;
    }

    // Title screen: the background picture (cropped to fill), darkened so the words read.
    const TextureAsset* bg = background_.empty() ? nullptr : &assets.texture(background_);
    if (bg && !bg->missing && bg->width > 0 && bg->height > 0) {
        float screenAspect = fb.x / std::max(fb.y, 1.0f), imageAspect = static_cast<float>(bg->width) / bg->height;
        Vec2 uv0{0, 0}, uv1{1, 1};
        if (imageAspect > screenAspect) {
            float keep = screenAspect / imageAspect;
            uv0.x = (1 - keep) * 0.5f;
            uv1.x = 1 - uv0.x;
        } else {
            float keep = imageAspect / screenAspect;
            uv0.y = (1 - keep) * 0.5f;
            uv1.y = 1 - uv0.y;
        }
        Vec3 corners[4] = {{0, 0, 0}, {fb.x, 0, 0}, {fb.x, fb.y, 0}, {0, fb.y, 0}};
        Vec2 uvs[4] = {{uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y}};
        r.quad(corners, uvs, {1, 1, 1, 1}, bg->handle);
        r.rect({0, 0}, fb, {0.02f, 0.03f, 0.05f, 0.62f}, white);
    } else {
        r.rect({0, 0}, fb, Color::fromHex(0x141821), white);
        r.rect({0, 0}, {fb.x, fb.y * 0.5f}, {kAccent.r, kAccent.g, kAccent.b, 0.08f}, white);
    }
    Layout l = layout(fb);
    float fade = smooth(time_ / 0.3f);
    float titleSize = l.titleSize;
    float width = font.measure(name_, titleSize).x;
    if (width > fb.x * 0.9f)
        titleSize *= fb.x * 0.9f / width; // long names get smaller rather than cut off
    r.textAt(font, name_, flip(l.titleAt), titleSize, {1, 1, 1, fade}, TextAlign::Center, true);
    if (!subtitle_.empty())
        r.textAt(font, subtitle_, flip({l.titleAt.x, l.titleAt.y + titleSize * 0.85f}), l.textSize,
                 {0.82f, 0.86f, 0.92f, fade}, TextAlign::Center, true);
    std::string graphicsLabel = std::string("Graphics: ") + qualityName(graphics_);
    for (int i = 0; i < buttonCount(); ++i) {
        const char* label = button(i) == Button::Play ? "Play" : button(i) == Button::Quit ? "Quit" : graphicsLabel.c_str();
        Vec2 mn = flip({l.buttonMin[i].x, l.buttonMax[i].y}), mx = flip({l.buttonMax[i].x, l.buttonMin[i].y});
        bool lit = i == hover_ || i == focus_;
        if (i == focus_) { // a ring on the one Enter presses
            float ring = u * 0.006f;
            r.rect(mn - Vec2{ring, ring}, mx + Vec2{ring, ring}, {1, 1, 1, 0.85f * fade}, white);
        }
        Color fill = i == 0 ? Color{kAccent.r * (lit ? 1.15f : 1.0f), kAccent.g * (lit ? 1.15f : 1.0f), kAccent.b, fade}
                            : Color{0.16f, 0.18f, 0.23f, (lit ? 0.98f : 0.9f) * fade};
        r.rect(mn, mx, fill, white);
        r.textAt(font, label, (mn + mx) * 0.5f, l.textSize * 1.25f, {1, 1, 1, fade}, TextAlign::Center, true);
    }
    float small = u * 0.026f, margin = u * 0.04f;
    r.textAt(font, footer_, {margin, margin + small * 0.5f}, small, {0.7f, 0.74f, 0.8f, 0.8f * fade}, TextAlign::Left, true);
    r.textAt(font, "Made with Aven", {fb.x - margin, margin + small * 0.5f}, small, {0.7f, 0.74f, 0.8f, 0.6f * fade},
             TextAlign::Right, true);
    r.end();
}

} // namespace aven
