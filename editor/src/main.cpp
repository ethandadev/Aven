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

namespace aven::embedded {
const unsigned char* find(const char* name, std::size_t* size);
}

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

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return {r / 255.0f, g / 255.0f, b / 255.0f, a}; }

// A calm dark theme with one blue accent, rounded corners and roomy spacing.
void applyTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = 6;
    st.ChildRounding = 6;
    st.FrameRounding = 5;
    st.PopupRounding = 6;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 5;
    st.TabRounding = 5;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 0;
    st.PopupBorderSize = 1;
    st.WindowPadding = {10, 10};
    st.FramePadding = {8, 5};
    st.ItemSpacing = {8, 6};
    st.ItemInnerSpacing = {6, 4};
    st.IndentSpacing = 16;
    st.ScrollbarSize = 13;
    st.GrabMinSize = 10;
    st.WindowTitleAlign = {0.0f, 0.5f};
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1;
    st.DockingSeparatorSize = 3;

    ImVec4* c = st.Colors;
    ImVec4 bg = rgb(24, 26, 31), panel = rgb(30, 33, 39), frame = rgb(42, 46, 54), frameHover = rgb(52, 57, 67);
    ImVec4 accent = rgb(59, 130, 246), accentHover = rgb(96, 155, 250), accentActive = rgb(37, 99, 235);
    c[ImGuiCol_Text] = rgb(226, 230, 237);
    c[ImGuiCol_TextDisabled] = rgb(128, 136, 150);
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = rgb(34, 37, 44, 0.98f);
    c[ImGuiCol_Border] = rgb(50, 54, 62);
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = rgb(60, 66, 78);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = rgb(62, 67, 78);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(78, 84, 97);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(92, 99, 114);
    c[ImGuiCol_CheckMark] = accentHover;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = frameHover;
    c[ImGuiCol_ButtonActive] = rgb(64, 70, 82);
    c[ImGuiCol_Header] = rgb(45, 70, 115);
    c[ImGuiCol_HeaderHovered] = rgb(52, 82, 135);
    c[ImGuiCol_HeaderActive] = accentActive;
    c[ImGuiCol_Separator] = rgb(50, 54, 62);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHover;
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accentHover;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = frameHover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = {0, 0, 0, 0};
    c[ImGuiCol_DockingPreview] = rgb(59, 130, 246, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = frame;
    c[ImGuiCol_TableBorderStrong] = rgb(50, 54, 62);
    c[ImGuiCol_TableBorderLight] = rgb(42, 46, 54);
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.02f);
    c[ImGuiCol_TextSelectedBg] = rgb(59, 130, 246, 0.35f);
    c[ImGuiCol_DragDropTarget] = accentHover;
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.55f};
    st.ScaleAllSizes(scale);
}

ImFont* addFont(const char* name, float size) {
    std::size_t bytes = 0;
    const unsigned char* data = embedded::find(name, &bytes);
    ImGuiIO& io = ImGui::GetIO();
    if (!data) {
        ImFontConfig cfg;
        cfg.SizePixels = size;
        return io.Fonts->AddFontDefault(&cfg);
    }
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // the font lives in the executable
    cfg.OversampleH = 2;
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2022, 0x2022, 0x2190, 0x21FF, 0x2026, 0x2026, 0};
    return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(bytes), size, &cfg, ranges);
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
    if (!window.create(wd))
        return 1;
    auto device = rhi::createDevice(rhi::Backend::OpenGL, Window::glProcLoader());
    if (!device)
        return 1;
    Log::info("Aven Editor ", AVEN_VERSION, " on ", device->description());

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

    float scale = std::max(1.0f, window.contentScale());
    Fonts fonts;
    fonts.ui = addFont("Roboto-Medium.ttf", 16.0f * scale);
    fonts.bold = addFont("Roboto-Medium.ttf", 18.0f * scale);
    fonts.big = addFont("Roboto-Medium.ttf", 28.0f * scale);
    fonts.code = addFont("Cousine-Regular.ttf", 15.0f * scale);
    io.FontDefault = fonts.ui;
    applyTheme(scale);

    ImGui_ImplGlfw_InitForOpenGL(window.native(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int exitCode = 0;
    {
        Editor editor(window, *device);
        editor.fonts = fonts;
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
