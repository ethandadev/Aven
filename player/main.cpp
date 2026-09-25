// Aven Player: runs a finished game. Exported games ship this executable next to
// their project folder, so double-clicking it starts the game.
//
// Usage: aven-player [project_folder] [--scene path] [--screenshot out.png --frames N]
//                    [--size WxH] [--hidden]

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/platform/window.h"
#include "aven/render/rhi.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

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
    struct KeyPress {
        int frame;
        std::string key;
        int frames; // how long it's held
    };
    std::vector<KeyPress> keyPresses;
};

stdfs::path findProject(const stdfs::path& hint) {
    if (!hint.empty())
        return hint;
    stdfs::path exe = fs::executableDir();
    for (const stdfs::path& p : {exe / "game", exe, stdfs::current_path()})
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
        else if (a == "--size") {
            std::string s = next();
            std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height);
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
        } else if (a == "--help" || a == "-h") {
            std::printf("usage: aven-player [project_folder] [--scene path] [--screenshot out.png --frames N]\n"
                        "                   [--size WxH] [--hidden] [--press FRAME:KEY[:FRAMES]]\n");
            return false;
        } else if (!a.empty() && a[0] != '-') {
            o.project = a;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt))
        return 0;
    stdfs::path projectDir = findProject(opt.project);
    if (projectDir.empty() || !ProjectSettings::isProject(projectDir)) {
        Log::error("No game found. Put the game folder (with project.aven) next to the player, or pass its path.");
        return 1;
    }

    ProjectSettings settings;
    settings.load(projectDir);
    WindowDesc wd;
    wd.title = settings.name;
    wd.width = opt.width ? opt.width : settings.width;
    wd.height = opt.height ? opt.height : settings.height;
    wd.resizable = settings.resizable;
    wd.fullscreen = settings.fullscreen && opt.screenshot.empty();
    wd.vsync = settings.vsync && opt.screenshot.empty();
    wd.visible = !opt.hidden;

    Window window;
    if (!window.create(wd))
        return 1;
    auto device = rhi::createDevice(rhi::Backend::OpenGL, Window::glProcLoader());
    if (!device)
        return 1;
    Log::info("Aven ", AVEN_VERSION, " running on ", device->description());

    Assets assets(device.get());
    assets.setRoot(projectDir);
    SceneRenderer renderer;
    if (!renderer.init(device.get(), &assets))
        return 1;

    int exitCode = 0;
    {
        Game game(assets, window.input());
        game.loadProject(projectDir);
        game.setCursorLocked = [&](bool locked) { window.setCursorLocked(locked); };
        game.setFullscreen = [&](bool on) { window.setFullscreen(on); };
        game.isFullscreen = [&]() { return window.isFullscreen(); };
        if (!game.loadScene(opt.scene.empty() ? settings.startScene : opt.scene))
            return 1;

        bool capture = !opt.screenshot.empty();
        double last = Window::time();
        int frame = 0;
        while (!window.shouldClose() && !game.quitRequested()) {
            window.pollEvents();
            for (auto& press : opt.keyPresses) {
                bool down = press.frame == frame, up = press.frame + press.frames == frame;
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
            double now = Window::time();
            float dt = capture ? 1.0f / 60.0f : static_cast<float>(std::min(now - last, 0.1));
            last = now;
            Vec2 fb = window.framebufferSize();
            if (window.minimized() || fb.x < 1 || fb.y < 1)
                continue;
            game.setScreenSize(fb);
            game.update(dt);
            device->beginFrame();
            game.render(renderer, static_cast<int>(fb.x), static_cast<int>(fb.y));
            renderer.present(static_cast<int>(fb.x), static_cast<int>(fb.y));
            device->endFrame();
            window.swapBuffers();
            ++frame;
            if (capture && frame >= opt.frames) {
                auto pixels = renderer.readOutput();
                if (Assets::savePng(opt.screenshot, pixels.data(), renderer.width(), renderer.height(), false))
                    Log::info("Saved screenshot to ", opt.screenshot, " (", device->stats().drawCalls, " draw calls)");
                else
                    exitCode = 1;
                break;
            }
        }
    }
    renderer.shutdown();
    return exitCode;
}
