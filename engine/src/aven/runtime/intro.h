#pragma once

// What an exported game shows before it starts (Project > publish settings):
//   - a moment of "Made with Aven" (about a second; any key, click or tap skips it), then
//   - optionally a title screen: the game's name and description, Play, and Quit.
// The game waits (no updates) until the intro is over. The editor's Play button skips all of it.

#include "aven/math/math.h"
#include "aven/runtime/project.h"

#include <string>

namespace aven {

class Assets;
class Input;
class Renderer2D;

class Intro {
public:
    static constexpr float kSplashSeconds = 1.4f;

    // canQuit: false in browsers, where a game can't close its own tab.
    void begin(const ProjectSettings& settings, Assets& assets, bool canQuit);
    bool active() const { return phase_ != Phase::Done; }
    bool onTitle() const { return phase_ == Phase::Title; }
    bool quitRequested() const { return quit_; }
    void skip() { phase_ = Phase::Done; }

    // window: the window's size in the same pixels as the mouse.
    void update(float dt, const Input& input, Vec2 window);
    void draw(Renderer2D& r, Assets& assets, Vec2 framebuffer) const;

private:
    enum class Phase { Splash, Title, Done };
    struct Layout {
        Vec2 titleAt;      // center of the name
        float titleSize = 0, textSize = 0;
        Vec2 buttonMin[2], buttonMax[2]; // Play, Quit (top-left origin)
    };
    Layout layout(Vec2 size) const;
    int buttonCount() const { return canQuit_ ? 2 : 1; }

    Phase phase_ = Phase::Done;
    float time_ = 0;
    bool canQuit_ = true, quit_ = false, titleNext_ = false;
    int focus_ = 0;   // the button Enter/Space presses
    int hover_ = -1;  // the button under the mouse
    std::string name_, subtitle_, footer_, background_;
    Vec2 window_{1280, 720};
};

} // namespace aven
