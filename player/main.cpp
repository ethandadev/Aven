// Aven Player: runs a finished game. Exported games ship this executable next to
// their project folder, so double-clicking it starts the game.
//
// Usage: aven-player [project_folder] [--scene path] [--screenshot out.png --frames N]
//                    [--size WxH] [--hidden] [--debug-draw]

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/platform/window.h"
#include "aven/render/rhi.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace aven;
namespace stdfs = std::filesystem;

namespace {

struct Options {
    stdfs::path project;
    std::string scene;
    std::string screenshot;
    int frames = 60;
    int width = 0, height = 0;
    bool hidden = false;
    bool debugDraw = false; // show scripts' debug_line() shapes
    bool intro = false;     // --intro: show the splash and title screen even in --screenshot runs
    struct KeyPress {
        int frame;
        std::string key;
        int frames; // how long it's held
    };
    std::vector<KeyPress> keyPresses;
    struct Touch {
        int frame;
        Vec2 at;
        int frames;
    };
    std::vector<Touch> touches; // --touch: pretend fingers (tests)
};

#ifdef __EMSCRIPTEN__
// Every finger on the canvas, from the browser's touch events.
std::vector<TouchPoint> g_touches;

EM_BOOL onTouch(int type, const EmscriptenTouchEvent* e, void*) {
    for (int i = 0; i < e->numTouches; ++i) {
        const EmscriptenTouchPoint& t = e->touches[i];
        if (!t.isChanged)
            continue;
        auto it = std::find_if(g_touches.begin(), g_touches.end(), [&](const TouchPoint& p) { return p.id == t.identifier; });
        bool ended = type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL;
        if (ended) {
            if (it != g_touches.end())
                g_touches.erase(it);
        } else if (it != g_touches.end()) {
            it->position = {static_cast<float>(t.targetX), static_cast<float>(t.targetY)};
        } else {
            g_touches.push_back({static_cast<int>(t.identifier), {static_cast<float>(t.targetX), static_cast<float>(t.targetY)}});
        }
    }
    return EM_TRUE; // no scrolling or zooming the page while playing
}
#endif

stdfs::path findProject(const stdfs::path& hint) {
    if (!hint.empty())
        return hint;
#ifdef __EMSCRIPTEN__
    return "/game"; // the web page downloads the game's files here before starting
#endif
    stdfs::path exe = fs::executableDir();
    // Beside the program, or inside a macOS app (Game.app/Contents/MacOS/Game -> Contents/Resources/game).
    std::error_code cwdError;
    stdfs::path cwd = stdfs::current_path(cwdError); // (none if the folder was deleted)
    for (const stdfs::path& p : {exe / "game", exe.parent_path() / "Resources" / "game", exe, cwdError ? exe : cwd})
        if (ProjectSettings::isProject(p))
            return p;
    return {};
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--scene")
            o.scene = next();
        else if (a == "--screenshot")
            o.screenshot = next();
        else if (a == "--frames")
            o.frames = std::atoi(next().c_str());
        else if (a == "--hidden")
            o.hidden = true;
        else if (a == "--debug-draw")
            o.debugDraw = true;
        else if (a == "--intro")
            o.intro = true;
        else if (a == "--size") {
            std::string s = next();
            if (std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height) != 2 || o.width < 16 || o.height < 16 || o.width > 16384 ||
                o.height > 16384)
                o.width = o.height = 0; // (not a size: the game's own)
        } else if (a == "--press") {
            // --press 30:space taps a key on frame 30; --press 30:right:60 holds it for 60 frames
            // (for automated tests).
            std::string s = next();
            size_t colon = s.find(':');
            if (colon != std::string::npos) {
                std::string key = s.substr(colon + 1);
                int frames = 2;
                size_t second = key.find(':');
                if (second != std::string::npos) {
                    frames = std::max(1, std::atoi(key.substr(second + 1).c_str()));
                    key = key.substr(0, second);
                }
                o.keyPresses.push_back({std::atoi(s.substr(0, colon).c_str()), key, frames});
            }
        } else if (a == "--touch") {
            // --touch 30:100,600:60 puts a finger at (100, 600) on frame 30 for 60 frames (tests).
            int frame = 0, frames = 2;
            float x = 0, y = 0;
            if (std::sscanf(next().c_str(), "%d:%f,%f:%d", &frame, &x, &y, &frames) >= 3)
                o.touches.push_back({frame, {x, y}, std::max(1, frames)});
        } else if (a == "--help" || a == "-h") {
            std::printf("usage: aven-player [project_folder] [--scene path] [--screenshot out.png --frames N]\n"
                        "                   [--size WxH] [--hidden] [--press FRAME:KEY[:FRAMES]] [--touch FRAME:X,Y[:FRAMES]]\n"
                        "                   [--debug-draw] [--intro]\n");
            return false;
        } else if (!a.empty() && a[0] != '-') {
            o.project = a;
        }
    }
    return true;
}

// Everything the running game needs. On the web the browser calls frame() for every
// animation frame, so this lives on the heap instead of main()'s stack.
struct Player {
    Options opt;
    stdfs::path projectDir;
    ProjectSettings settings;
    Window window;
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<Assets> assets;
    SceneRenderer renderer;
    std::unique_ptr<Game> game;
    double last = 0;
    int frameIndex = 0;
    int exitCode = 0;
    bool capture = false;

    bool start() {
        settings.load(projectDir);
        WindowDesc wd;
        wd.title = settings.name;
        wd.width = opt.width ? opt.width : settings.width;
        wd.height = opt.height ? opt.height : settings.height;
        wd.resizable = settings.resizable;
        wd.fullscreen = settings.fullscreen && opt.screenshot.empty();
        wd.vsync = settings.vsync && opt.screenshot.empty();
        wd.visible = !opt.hidden;
        if (!window.create(wd))
            return false;
        // The game's icon (Build & Share writes app-icon.png), or Aven's.
        for (const std::string& icon : {std::string("app-icon.png"), settings.publish.icon})
            if (!icon.empty() && fs::exists(projectDir / icon)) {
                window.setIconFromFile(projectDir / icon);
                break;
            }
        device = rhi::createDevice(rhi::Backend::OpenGL, Window::glProcLoader());
        if (!device)
            return false;
        Log::info("Aven ", AVEN_VERSION, " running on ", device->description());
        assets = std::make_unique<Assets>(device.get());
        assets->setRoot(projectDir);
        if (!renderer.init(device.get(), assets.get()))
            return false;
        game = std::make_unique<Game>(*assets, window.input());
        game->loadProject(projectDir);
        game->setCursorLocked = [this](bool locked) { window.setCursorLocked(locked); };
        game->setFullscreen = [this](bool on) { window.setFullscreen(on); };
        game->isFullscreen = [this]() { return window.isFullscreen(); };
        if (!game->loadScene(opt.scene.empty() ? settings.startScene : opt.scene))
            return false;
        capture = !opt.screenshot.empty();
        // "Made with Aven" and the title screen. Automated screenshots go straight to the game.
        if (!capture || opt.intro) {
#ifdef __EMSCRIPTEN__
            game->startIntro(false); // a web page can't close its own tab: no Quit button
#else
            game->startIntro(true);
#endif
        }
        last = Window::time();
#ifdef __EMSCRIPTEN__
        emscripten_set_touchstart_callback("#canvas", nullptr, true, onTouch);
        emscripten_set_touchmove_callback("#canvas", nullptr, true, onTouch);
        emscripten_set_touchend_callback("#canvas", nullptr, true, onTouch);
        emscripten_set_touchcancel_callback("#canvas", nullptr, true, onTouch);
#endif
        return true;
    }

    // One frame of the game; false when it's time to stop.
    bool frame() {
        if (window.shouldClose() || game->quitRequested())
            return false;
        window.pollEvents();
        for (auto& press : opt.keyPresses) {
            bool down = press.frame == frameIndex, up = press.frame + press.frames == frameIndex;
            if (!down && !up)
                continue;
            // mouse_left / mouse_right / mouse_middle hold a mouse button instead of a key.
            if (press.key.rfind("mouse_", 0) == 0) {
                int button = press.key == "mouse_right" ? 1 : press.key == "mouse_middle" ? 2 : 0;
                window.input().onMouseButton(button, down);
            } else {
                window.input().onKey(Input::keyFromName(press.key), down);
            }
        }
        // Fingers: on-screen controls, and taps that act like the mouse.
        std::vector<TouchPoint> touches;
#ifdef __EMSCRIPTEN__
        touches = g_touches;
        // Touches come in page (CSS) pixels; the mouse, and so the game, uses the canvas's own size.
        double cssW = 0, cssH = 0;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        Vec2 win = window.windowSize();
        if (cssW > 0 && cssH > 0)
            for (auto& t : touches)
                t.position = {t.position.x * win.x / static_cast<float>(cssW), t.position.y * win.y / static_cast<float>(cssH)};
#endif
        for (auto& t : opt.touches)
            if (frameIndex >= t.frame && frameIndex < t.frame + t.frames)
                touches.push_back({1000 + static_cast<int>(&t - opt.touches.data()), t.at});
        TouchControls& controls = game->touchControls();
        if (touches.empty() && controls.visible() && window.input().mouseDown(MouseButton::Left))
            touches.push_back({-1, window.input().mousePosition()}); // on a computer, the mouse can use them too
        controls.update(touches, window.windowSize(), window.input());
        double now = Window::time();
        float dt = capture ? 1.0f / 60.0f : static_cast<float>(std::min(now - last, 0.1));
        last = now;
        Vec2 fb = window.framebufferSize();
        if (window.minimized() || fb.x < 1 || fb.y < 1) {
#ifndef __EMSCRIPTEN__
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // nothing to draw; don't spin
#endif
            return true;
        }
        game->setScreenSize(fb);
        game->setWindowSize(window.windowSize());
        game->update(dt);
        window.endFrame(); // browsers: input from here on belongs to the next frame
        device->beginFrame();
        RenderOptions renderOptions;
        renderOptions.debugDraw = opt.debugDraw;
        game->render(renderer, static_cast<int>(fb.x), static_cast<int>(fb.y), renderOptions);
        renderer.present(static_cast<int>(fb.x), static_cast<int>(fb.y));
        device->endFrame();
        window.swapBuffers();
        ++frameIndex;
        if (capture && frameIndex >= opt.frames) {
            auto pixels = renderer.readOutput();
            if (Assets::savePng(opt.screenshot, pixels.data(), renderer.width(), renderer.height(), false))
                Log::info("Saved screenshot to ", opt.screenshot, " (", device->stats().drawCalls, " draw calls)");
            else
                exitCode = 1;
            return false;
        }
        return true;
    }

    void finish() {
        game.reset();
        renderer.shutdown();
    }
};

} // namespace

int main(int argc, char** argv) {
    auto* player = new Player(); // intentionally lives until the program ends (the web never returns from main)
    if (!parseArgs(argc, argv, player->opt))
        return 0;
    // A double-clicked game has no console, so a failed start is shown in a dialog box.
    std::string firstError;
    int sink = Log::addSink([&firstError](const LogMessage& m) {
        if (m.level == LogLevel::Error && firstError.empty())
            firstError = m.text;
    });
    auto fail = [&](const std::string& title) {
        if (!player->opt.hidden && player->opt.screenshot.empty())
            showErrorDialog(title, firstError.empty() ? "The game couldn't start." : firstError);
        return 1;
    };
    player->projectDir = findProject(player->opt.project);
    if (player->projectDir.empty() || !ProjectSettings::isProject(player->projectDir)) {
        Log::error("No game found. Put the game folder (with project.aven) next to the player, or pass its path.");
        return fail("Aven");
    }
    if (!player->start())
        return fail(player->settings.name.empty() ? "Aven" : player->settings.name);
    Log::removeSink(sink);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(
        [](void* p) {
            if (!static_cast<Player*>(p)->frame())
                emscripten_cancel_main_loop();
        },
        player, 0, true);
    return 0;
#else
    while (player->frame()) {
    }
    player->finish();
    int code = player->exitCode;
    delete player;
    return code;
#endif
}
