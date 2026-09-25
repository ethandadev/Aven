// Aven Editor: the place where games are made.
//
// Usage: aven-editor [project_folder] [--open scripts/player.blocks] [--select Player]
//                    [--panel hub|settings|reference] [--play]
//                    [--screenshot out.png --frames N] [--size WxH]
//        aven-editor --new folder [--template id]
//        aven-editor project_folder --export folder

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aven;
using namespace aven::editor;

namespace {

struct Args {
    EditorOptions editor;
    int width = 0, height = 0;
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (s == "--project")
            a.editor.project = next();
        else if (s == "--screenshot")
            a.editor.screenshot = next();
        else if (s == "--frames")
            a.editor.frames = std::max(2, std::atoi(next().c_str()));
        else if (s == "--open")
            a.editor.openFile = next();
        else if (s == "--panel")
            a.editor.openPanel = next();
        else if (s == "--select")
            a.editor.select = next();
        else if (s == "--play")
            a.editor.play = true;
        else if (s == "--new")
            a.editor.newProject = next();
        else if (s == "--template")
            a.editor.templateId = next();
        else if (s == "--export")
            a.editor.exportTo = next();
        else if (s == "--level")
            a.editor.level = std::atoi(next().c_str());
        else if (s == "--theme")
            a.editor.theme = next();
        else if (s == "--size") {
            std::string v = next();
            std::sscanf(v.c_str(), "%dx%d", &a.width, &a.height);
        } else if (s == "--help" || s == "-h") {
            std::printf("usage: aven-editor [project_folder] [--open file] [--select name] [--panel hub|settings|reference]\n"
                        "                   [--play] [--screenshot out.png --frames N] [--size WxH]\n"
                        "       aven-editor --new folder [--template id]     create a game from a template\n"
                        "       aven-editor project_folder --export folder   build a playable copy of a game\n");
            return false;
        } else if (!s.empty() && s[0] != '-') {
            a.editor.project = s;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args))
        return 0;
    bool screenshotMode = !args.editor.screenshot.empty();

    WindowDesc wd;
    wd.title = "Aven";
    wd.width = args.width ? args.width : (screenshotMode ? 1600 : 1440);
    wd.height = args.height ? args.height : (screenshotMode ? 900 : 900);
    wd.maximized = !screenshotMode && !args.width;
    wd.vsync = !screenshotMode;
    Window window;
    if (!window.create(wd)) {
        if (!screenshotMode)
            showErrorDialog("Aven", "Aven couldn't open its window. It needs OpenGL 3.3: updating the graphics "
                                    "driver usually fixes this.");
        return 1;
    }
    auto device = rhi::createDevice(rhi::Backend::OpenGL, Window::glProcLoader());
    if (!device) {
        if (!screenshotMode)
            showErrorDialog("Aven", "Aven couldn't start OpenGL 3.3. Updating the graphics driver usually fixes this.");
        return 1;
    }
    Log::info("Aven Editor ", AVEN_VERSION, " on ", device->description());
    window.setDefaultIcon();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    static std::string iniPath;
    if (screenshotMode) {
        io.IniFilename = nullptr; // screenshots always use the default layout
    } else {
        iniPath = (fs::userDataDir("Aven Editor") / "layout.ini").string();
        io.IniFilename = iniPath.c_str();
    }

    ImGui_ImplGlfw_InitForOpenGL(window.native(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int exitCode = 0;
    {
        Editor editor(window, *device);
        editor.setDpiScale(std::max(1.0f, window.contentScale()));
        if (!editor.init(args.editor)) {
            exitCode = 1;
        } else {
            double last = Window::time();
            int frame = 0;
            while (!editor.wantsQuit()) {
                window.pollEvents();
                if (window.shouldClose()) {
                    // Give the editor a chance to ask about unsaved work.
                    window.cancelClose();
                    editor.requestQuit();
                }
                double now = Window::time();
                float dt = static_cast<float>(std::min(now - last, 0.1));
                last = now;
                if (screenshotMode)
                    dt = 1.0f / 60.0f;
                if (window.minimized() && !screenshotMode) {
                    window.swapBuffers();
                    continue;
                }

                editor.beginFrame();
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                ImGuizmo::BeginFrame();
                editor.frame(dt);
                ImGui::Render();

                Vec2 fb = window.framebufferSize();
                device->beginFrame();
                rhi::PassDesc pass;
                pass.width = static_cast<int>(fb.x);
                pass.height = static_cast<int>(fb.y);
                pass.clearColor = true;
                pass.clearValue = {0.094f, 0.102f, 0.122f, 1.0f};
                device->beginPass(pass);
                device->endPass();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                device->endFrame();

                ++frame;
                if (screenshotMode && frame >= args.editor.frames) {
                    int w = static_cast<int>(fb.x), h = static_cast<int>(fb.y);
                    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
                    device->readPixels({}, 0, 0, 0, w, h, pixels.data());
                    for (size_t i = 3; i < pixels.size(); i += 4)
                        pixels[i] = 255;
                    if (Assets::savePng(args.editor.screenshot, pixels.data(), w, h, true))
                        Log::info("Saved screenshot ", args.editor.screenshot);
                    else
                        exitCode = 1;
                    break;
                }
                window.swapBuffers();
            }
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    device.reset();
    return exitCode;
}
