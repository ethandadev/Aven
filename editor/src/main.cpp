// Rynax Editor: the place where games are made.
//
// Usage: rynax-editor [project_folder] [--open scripts/player.blocks] [--select Player]
//                    [--panel hub|settings|reference] [--play]
//                    [--screenshot out.png --frames N] [--size WxH]
//        rynax-editor --new folder [--template id]
//        rynax-editor project_folder --export folder

#include "check.h"
#include "editor.h"
#include "menu.h"

#include "rynax/core/fs.h"
#include "rynax/core/log.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <GLFW/glfw3.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace rynax;
using namespace rynax::editor;

namespace {

// --input: mouse and keyboard played back on given frames, for testing the editor headless.
//   <frame> move X Y [over N frames]      <frame> down|up [button]
//   <frame> key Shift|Ctrl|Alt|Delete|Escape|Enter|Tab|A..Z down|up      <frame> type text
struct InputStep {
    int frame = 0;
    std::string command;
    std::vector<std::string> args;
};

std::vector<InputStep> loadInput(const std::string& path) {
    std::vector<InputStep> steps;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        InputStep st;
        if (!(ss >> st.frame >> st.command) || st.command[0] == '#')
            continue;
        std::string a;
        while (ss >> a)
            st.args.push_back(a);
        steps.push_back(std::move(st));
    }
    return steps;
}

ImGuiKey keyNamed(const std::string& n) {
    if (n == "Shift") return ImGuiKey_LeftShift;
    if (n == "Ctrl") return ImGuiKey_LeftCtrl;
    if (n == "Alt") return ImGuiKey_LeftAlt;
    if (n == "Delete") return ImGuiKey_Delete;
    if (n == "Escape") return ImGuiKey_Escape;
    if (n == "Enter") return ImGuiKey_Enter;
    if (n == "Tab") return ImGuiKey_Tab;
    if (n == "Backspace") return ImGuiKey_Backspace;
    if (n == "F2") return ImGuiKey_F2;
    if (n == "Up") return ImGuiKey_UpArrow;
    if (n == "Down") return ImGuiKey_DownArrow;
    if (n == "Left") return ImGuiKey_LeftArrow;
    if (n == "Right") return ImGuiKey_RightArrow;
    if (n == "Home") return ImGuiKey_Home;
    if (n == "End") return ImGuiKey_End;
    if (n == "Space") return ImGuiKey_Space;
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1])))
        return static_cast<ImGuiKey>(ImGuiKey_F1 + std::stoi(n.substr(1)) - 1);
    if (n.size() == 1 && std::isdigit(static_cast<unsigned char>(n[0])))
        return static_cast<ImGuiKey>(ImGuiKey_0 + (n[0] - '0'));
    if (n.size() == 1 && std::isalpha(static_cast<unsigned char>(n[0])))
        return static_cast<ImGuiKey>(ImGuiKey_A + (std::toupper(static_cast<unsigned char>(n[0])) - 'A'));
    return ImGuiKey_None;
}

// The same key as GLFW numbers it (the engine's Input), or -1.
int glfwKeyNamed(const std::string& n) {
    static const std::pair<const char*, int> named[] = {
        {"Shift", keys::LeftShift}, {"Ctrl", keys::LeftControl}, {"Alt", keys::LeftAlt}, {"Delete", keys::Delete},
        {"Escape", keys::Escape},   {"Enter", keys::Enter},      {"Tab", keys::Tab},     {"Backspace", keys::Backspace},
        {"Up", keys::Up},           {"Down", keys::Down},        {"Left", keys::Left},   {"Right", keys::Right},
        {"Home", keys::Home},       {"End", keys::End},          {"Space", keys::Space}};
    for (auto& [name, key] : named)
        if (n == name)
            return key;
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1])))
        return keys::F1 + std::stoi(n.substr(1)) - 1;
    if (n.size() == 1 && std::isalnum(static_cast<unsigned char>(n[0])))
        return std::toupper(static_cast<unsigned char>(n[0])); // (GLFW's letters and digits are their ASCII codes)
    return -1;
}

// Two widgets with the same ID under the mouse (ImGui counts them while it's over one): in tests that's
// an error, with the window and the mouse position, to find it by.
void reportIdConflict() {
    ImGuiContext& g = *GImGui;
    static ImGuiID reported = 0;
    if (g.HoveredIdPreviousFrameItemCount > 1 && g.HoveredIdPreviousFrame != reported) {
        reported = g.HoveredIdPreviousFrame;
        Log::error("Two widgets share an ImGui ID in '", g.HoveredWindow ? g.HoveredWindow->Name : "?", "' at (",
                   static_cast<int>(g.IO.MousePos.x), ", ", static_cast<int>(g.IO.MousePos.y), ")");
    }
}

struct InputPlayer {
    std::vector<InputStep> steps;
    float x = 0, y = 0, fromX = 0, fromY = 0, toX = 0, toY = 0;
    int glideStart = 0, glideFrames = 0;

    // Recorded input goes to ImGui (the editor) and to the engine's Input (a game playing in it).
    void apply(int frame, GLFWwindow* window, Input& input) {
        ImGuiIO& io = ImGui::GetIO();
        if (glideFrames > 0) {
            float t = std::min(1.0f, static_cast<float>(frame - glideStart) / static_cast<float>(glideFrames));
            x = fromX + (toX - fromX) * t;
            y = fromY + (toY - fromY) * t;
            if (t >= 1.0f)
                glideFrames = 0;
        }
        for (auto& st : steps) {
            if (st.frame != frame)
                continue;
            if (st.command == "move" && st.args.size() >= 2) {
                toX = std::strtof(st.args[0].c_str(), nullptr); // (a typo in a test script is 0, not a crash)
                toY = std::strtof(st.args[1].c_str(), nullptr);
                int over = st.args.size() >= 3 ? std::atoi(st.args[2].c_str()) : 0;
                if (over > 0) {
                    fromX = x;
                    fromY = y;
                    glideStart = frame;
                    glideFrames = over;
                } else {
                    x = toX;
                    y = toY;
                }
            } else if (st.command == "down" || st.command == "up") {
                int button = 0; // down [left|right|middle|0|1|2]
                if (!st.args.empty())
                    button = st.args[0] == "right" ? 1 : st.args[0] == "middle" ? 2 : std::atoi(st.args[0].c_str());
                io.AddMouseButtonEvent(button, st.command == "down");
                input.onMouseButton(button, st.command == "down");
            } else if (st.command == "key" && st.args.size() >= 2) {
                bool down = st.args[1] == "down";
                const std::string& k = st.args[0];
                if (k == "Shift") io.AddKeyEvent(ImGuiMod_Shift, down);
                if (k == "Ctrl") io.AddKeyEvent(ImGuiMod_Ctrl, down);
                if (k == "Alt") io.AddKeyEvent(ImGuiMod_Alt, down);
                io.AddKeyEvent(keyNamed(k), down);
                if (int key = glfwKeyNamed(k); key >= 0)
                    input.onKey(key, down);
            } else if (st.command == "type") {
                for (size_t w = 0; w < st.args.size(); ++w) // words, with the spaces between them
                    io.AddInputCharactersUTF8(((w ? " " : "") + st.args[w]).c_str());
            }
        }
        glfwSetCursorPos(window, x, y); // so the backend reads the same position
        io.AddMousePosEvent(x, y);
        input.onMouseMove({x, y});
    }
};

struct Args {
    EditorOptions editor;
    std::string input;
    int width = 0, height = 0;
    float scale = 0; // --scale 1.5: like 150% display scaling (for testing layouts)
    bool check = false, strict = false;
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
        else if (s == "--input")
            a.input = next();
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
        else if (s == "--check")
            a.check = true;
        else if (s == "--strict")
            a.strict = true;
        else if (s == "--scale")
            a.scale = static_cast<float>(std::atof(next().c_str()));
        else if (s == "--size") {
            std::string v = next();
            std::sscanf(v.c_str(), "%dx%d", &a.width, &a.height);
        } else if (s == "--help" || s == "-h") {
            std::printf("usage: rynax-editor [project_folder] [--open file] [--select name] [--panel hub|settings|reference]\n"
                        "                   [--play] [--screenshot out.png --frames N] [--size WxH]\n"
                        "       rynax-editor --new folder [--template id]     create a game from a template\n"
                        "       rynax-editor project_folder --export folder   build a playable copy of a game\n"
                        "       rynax-editor project_folder --check [--strict]\n"
                        "                   check scripts, scenes and files without a window (for CI); exit code 1 on errors\n"
                        "                   (--strict: on warnings too)\n");
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
    if (args.check)
        return runProjectCheck(args.editor.project.empty() ? std::filesystem::current_path() : std::filesystem::path(args.editor.project),
                               args.strict);
    bool screenshotMode = !args.editor.screenshot.empty();

    WindowDesc wd;
    wd.title = "Rynax";
    wd.width = args.width ? args.width : (screenshotMode ? 1600 : 1440);
    wd.height = args.height ? args.height : (screenshotMode ? 900 : 900);
    wd.maximized = !screenshotMode && !args.width;
    wd.vsync = !screenshotMode;
    Window window;
    if (!window.create(wd)) {
        if (!screenshotMode)
            showErrorDialog("Rynax", "Rynax couldn't open its window. It needs OpenGL 3.3: updating the graphics "
                                    "driver usually fixes this.");
        return 1;
    }
    auto device = rhi::createDevice(rhi::Backend::OpenGL, Window::glProcLoader());
    if (!device) {
        if (!screenshotMode)
            showErrorDialog("Rynax", "Rynax couldn't start OpenGL 3.3. Updating the graphics driver usually fixes this.");
        return 1;
    }
    Log::info("Rynax Editor ", RYNAX_VERSION, " on ", device->description());
    window.setDefaultIcon();
    if (!screenshotMode)
        menu::installNative(); // a Mac's menu bar at the top of the screen (elsewhere: nothing)

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    // Two widgets sharing an ID is our bug, not the user's: release builds don't show ImGui's red
    // "programmer error" box; test runs log it as an error instead (see the frame loop).
    io.ConfigDebugHighlightIdConflicts = RYNAX_TEST_HOOKS && !screenshotMode;
    static std::string iniPath;
    if (!screenshotMode)
        bringOverAvenData(); // (before anything reads the settings: the first run after Aven became Rynax)
    if (screenshotMode) {
        io.IniFilename = nullptr; // screenshots always use the default layout
    } else {
        iniPath = (fs::userDataDir("Rynax Editor") / "layout.ini").string();
        io.IniFilename = iniPath.c_str();
    }

    ImGui_ImplGlfw_InitForOpenGL(window.native(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int exitCode = 0;
    std::string restart; // the updated editor, when an update asked to restart
    {
        Editor editor(window, *device);
        // Windows and X11 measure windows in pixels, so a 150% screen needs everything 1.5x bigger.
        // macOS (and Wayland) measure in points and draw into a bigger framebuffer (2x on Retina):
        // there the layout stays at 1x and only the text is rasterized at 2x, or everything is double size.
        // Checked every frame: dragging the window to another screen can change both.
        float layoutNow = 0, densityNow = 0;
        auto updateScale = [&] {
            Vec2 points = window.windowSize(), pixels = window.framebufferSize();
            if (points.x <= 0 || pixels.x <= 0)
                return; // minimized
            float density = std::max(1.0f, std::round(pixels.x / points.x * 4.0f) / 4.0f);
            float layout = args.scale > 0 ? args.scale : std::max(1.0f, window.contentScale() / density);
            if (std::abs(layout - layoutNow) > 0.01f || std::abs(density - densityNow) > 0.01f) {
                layoutNow = layout;
                densityNow = density;
                editor.setDpiScale(layout, density);
            }
        };
        updateScale();
        if (!editor.init(args.editor)) {
            exitCode = 1;
        } else {
            double last = Window::time();
            int frame = 0;
            InputPlayer input;
            if (!args.input.empty())
                input.steps = loadInput(args.input);
            double restingSince = -1;
            while (!editor.wantsQuit()) {
                // A second after the last key or mouse move, with nothing moving on its own, the
                // editor rests: it waits for the next event, but wakes at least 10 times a second
                // (progress bars, messages that fade). Never in automated runs.
                bool rest = !screenshotMode && input.steps.empty() && editor.canRest() &&
                            Window::time() - window.lastInputTime() > 1.0;
                if (rest)
                    window.waitEvents(0.1);
                else
                    window.pollEvents();
                // "Resting" only after half a second of it, so the FPS counter doesn't flicker.
                if (!rest)
                    restingSince = -1;
                else if (restingSince < 0)
                    restingSince = Window::time();
                editor.setResting(rest && Window::time() - restingSince > 0.5);
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

                updateScale();
                editor.beginFrame();
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                if (!input.steps.empty())
                    input.apply(frame, window.native(), window.input());
                ImGui::NewFrame();
                ImGuizmo::BeginFrame();
                editor.frame(dt);
                if (screenshotMode)
                    reportIdConflict();
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
        restart = editor.installPendingUpdate();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    device.reset();
    if (!restart.empty() && !screenshotMode) {
        window.destroy();
        launchCommand(restart);
    }
    return exitCode;
}
