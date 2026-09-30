#include "rynax/platform/window.h"

#include "rynax/core/embedded.h"
#include "rynax/core/fs.h"
#include "rynax/core/log.h"

#include <stb_image.h>

#include <algorithm>
#include <cstring>

#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace rynax {

void showErrorDialog(const std::string& title, const std::string& message) {
#if defined(__EMSCRIPTEN__)
    (void)title;
    (void)message;
#elif defined(_WIN32)
    auto wide = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring out(static_cast<size_t>(n > 0 ? n : 1), L'\0');
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
        return out;
    };
    MessageBoxW(nullptr, wide(message).c_str(), wide(title).c_str(), MB_OK | MB_ICONERROR);
#else
    // Started from a terminal: the message is already printed there.
    if (isatty(STDERR_FILENO))
        return;
    auto run = [](std::vector<std::string> args) {
        std::vector<char*> argv;
        for (auto& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
            return false;
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) != 127;
    };
#ifdef __APPLE__
    auto quoted = [](const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        return out + "\"";
    };
    run({"osascript", "-e", "display alert " + quoted(title) + " message " + quoted(message) + " as critical"});
#else
    if (!run({"zenity", "--error", "--title=" + title, "--no-markup", "--text=" + message}))
        run({"kdialog", "--title", title, "--error", message});
#endif
#endif
}

namespace {

int g_windowCount = 0;

void errorCallback(int code, const char* message) {
    Log::error("Window system error ", code, ": ", message);
}

bool ensureGlfw() {
    static bool initialized = false;
    if (initialized)
        return true;
    glfwSetErrorCallback(errorCallback);
    if (!glfwInit()) {
        Log::error("Could not start the window system. Is a display available?");
        return false;
    }
    initialized = true;
    return true;
}

Window* self(GLFWwindow* w) { return static_cast<Window*>(glfwGetWindowUserPointer(w)); }

} // namespace

Window::Window() = default;

Window::~Window() {
    destroy();
}

bool Window::create(const WindowDesc& desc) {
    if (!ensureGlfw())
        return false;
    glfwDefaultWindowHints();
#ifdef __EMSCRIPTEN__
    // WebGL 2 (OpenGL ES 3.0) in the page's canvas.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, desc.maximized ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, desc.samples);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWmonitor* monitor = nullptr;
    int w = desc.width, h = desc.height;
    if (desc.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        if (const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr) {
            w = mode->width;
            h = mode->height;
        }
    }
    handle_ = glfwCreateWindow(w, h, desc.title.c_str(), monitor, nullptr);
    if (!handle_) {
        Log::error("Could not create a window with OpenGL 3.3. Please update your graphics drivers.");
        return false;
    }
    ++g_windowCount;
    fullscreen_ = desc.fullscreen;
    windowedW_ = desc.width;
    windowedH_ = desc.height;
    glfwSetWindowUserPointer(handle_, this);
    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(desc.vsync ? 1 : 0);
    lastInput_ = glfwGetTime(); // (awake for the first moments: layouts settle over a few frames)
    installCallbacks(handle_);
    double mx, my;
    glfwGetCursorPos(handle_, &mx, &my);
    input_.onMouseMove({static_cast<float>(mx), static_cast<float>(my)});
    return true;
}

void Window::destroy() {
    if (!handle_)
        return;
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
    if (--g_windowCount == 0) {
        // Keep GLFW initialized; terminating and re-initializing is slow and some
        // platforms dislike it. The OS cleans up at exit.
    }
}

void Window::installCallbacks(GLFWwindow* w) {
    glfwSetKeyCallback(w, [](GLFWwindow* win, int key, int, int action, int) {
        self(win)->lastInput_ = glfwGetTime();
        if (action == GLFW_REPEAT)
            return;
        self(win)->input_.onKey(key, action == GLFW_PRESS);
    });
    glfwSetMouseButtonCallback(w, [](GLFWwindow* win, int button, int action, int) {
        self(win)->lastInput_ = glfwGetTime();
        self(win)->input_.onMouseButton(button, action == GLFW_PRESS);
    });
    glfwSetCursorPosCallback(w, [](GLFWwindow* win, double x, double y) {
        self(win)->lastInput_ = glfwGetTime();
        self(win)->input_.onMouseMove({static_cast<float>(x), static_cast<float>(y)});
    });
    glfwSetScrollCallback(w, [](GLFWwindow* win, double x, double y) {
        self(win)->lastInput_ = glfwGetTime();
        self(win)->input_.onScroll({static_cast<float>(x), static_cast<float>(y)});
    });
    glfwSetCharCallback(w, [](GLFWwindow* win, unsigned int c) {
        self(win)->lastInput_ = glfwGetTime();
        self(win)->input_.onChar(c);
    });
    glfwSetWindowFocusCallback(w, [](GLFWwindow* win, int focused) {
        self(win)->lastInput_ = glfwGetTime();
        if (!focused)
            self(win)->input_.releaseAll(); // avoid keys getting "stuck" after alt-tab
    });
    glfwSetDropCallback(w, [](GLFWwindow* win, int count, const char** paths) {
        Window* me = self(win);
        me->lastInput_ = glfwGetTime();
        if (!me->onFileDrop)
            return;
        std::vector<std::string> files(paths, paths + count);
        me->onFileDrop(files);
    });
}

void Window::waitEvents(double timeout) {
#ifndef __EMSCRIPTEN__
    if (timeout > 0) {
        input_.beginFrame(); // (first, as in pollEvents: the events that wake it belong to this frame)
        glfwWaitEventsTimeout(timeout); // returns as soon as anything happens, having handled it
        return;
    }
#endif
    pollEvents();
}

bool Window::shouldClose() const { return handle_ && glfwWindowShouldClose(handle_); }
void Window::requestClose() { if (handle_) glfwSetWindowShouldClose(handle_, GLFW_TRUE); }
void Window::cancelClose() { if (handle_) glfwSetWindowShouldClose(handle_, GLFW_FALSE); }

void Window::pollEvents() {
#ifndef __EMSCRIPTEN__
    input_.beginFrame(); // (in browsers, endFrame() does this: see there)
#endif
    glfwPollEvents();
#ifdef __EMSCRIPTEN__
    // Browsers report gamepads in the "standard" layout.
    // Only pads in the "standard" layout: browsers also list other devices (some mice, headsets,
    // drawing tablets, virtual-controller drivers) whose "sticks" can sit at -1 and walk the player.
    EmscriptenGamepadEvent pad{};
    bool found = false;
    if (emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS)
        for (int i = 0, n = emscripten_get_num_gamepads(); i < n && !found; ++i)
            found = emscripten_get_gamepad_status(i, &pad) == EMSCRIPTEN_RESULT_SUCCESS && pad.connected &&
                    std::strcmp(pad.mapping, "standard") == 0;
    if (found) {
        static const int kButtons[] = {0, 1, 2, 3, 4, 5, 8, 9, 16, 10, 11, 12, 15, 13, 14};
        bool buttons[static_cast<int>(PadButton::Count)];
        float axes[static_cast<int>(PadAxis::Count)];
        for (int i = 0; i < static_cast<int>(PadButton::Count); ++i)
            buttons[i] = kButtons[i] < pad.numButtons && pad.digitalButton[kButtons[i]];
        for (int i = 0; i < 4; ++i)
            axes[i] = i < pad.numAxes ? static_cast<float>(pad.axis[i]) : 0.0f;
        axes[4] = pad.numButtons > 6 ? static_cast<float>(pad.analogButton[6]) * 2 - 1 : -1;
        axes[5] = pad.numButtons > 7 ? static_cast<float>(pad.analogButton[7]) * 2 - 1 : -1;
        input_.setGamepad(true, buttons, axes);
        return;
    }
#else
    // The first joystick that's a gamepad (another device can sit in the first slot: a wheel, a
    // drawing tablet's buttons...).
    int padJoystick = -1;
    for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST && padJoystick < 0; ++j)
        if (glfwJoystickIsGamepad(j))
            padJoystick = j;
    if (padJoystick >= 0) {
        GLFWgamepadstate state;
        if (glfwGetGamepadState(padJoystick, &state)) {
            bool buttons[static_cast<int>(PadButton::Count)];
            float axes[static_cast<int>(PadAxis::Count)];
            for (int i = 0; i < static_cast<int>(PadButton::Count); ++i)
                buttons[i] = state.buttons[i] == GLFW_PRESS;
            for (int i = 0; i < static_cast<int>(PadAxis::Count); ++i)
                axes[i] = state.axes[i];
            input_.setGamepad(true, buttons, axes);
            return;
        }
    }
#endif
    bool none[static_cast<int>(PadButton::Count)]{};
    float zero[static_cast<int>(PadAxis::Count)]{};
    input_.setGamepad(false, none, zero);
}

void Window::endFrame() {
#ifdef __EMSCRIPTEN__
    // Browsers deliver keys, clicks, mouse moves and scrolling between frames, before the next
    // pollEvents(). Starting the next frame's input here, at the end of this one, keeps those events
    // for the frame that comes next: otherwise a new key press was already "held" by the time the
    // game looked, so key_pressed() and jumping never fired, and mouse movement and typing were lost.
    input_.beginFrame();
#endif
}

void Window::swapBuffers() { if (handle_) glfwSwapBuffers(handle_); }

Vec2 Window::framebufferSize() const {
    int w = 0, h = 0;
    if (handle_)
        glfwGetFramebufferSize(handle_, &w, &h);
    return {static_cast<float>(w), static_cast<float>(h)};
}

Vec2 Window::windowSize() const {
    int w = 0, h = 0;
    if (handle_)
        glfwGetWindowSize(handle_, &w, &h);
    return {static_cast<float>(w), static_cast<float>(h)};
}

float Window::contentScale() const {
    float x = 1, y = 1;
    if (handle_)
        glfwGetWindowContentScale(handle_, &x, &y);
    return x;
}

void Window::setTitle(const std::string& title) { if (handle_) glfwSetWindowTitle(handle_, title.c_str()); }

void Window::setIcon(const std::vector<std::pair<const unsigned char*, std::size_t>>& pngs) {
#ifdef __EMSCRIPTEN__
    (void)pngs;
    return;
#else
    if (!handle_ || glfwGetPlatform() == GLFW_PLATFORM_WAYLAND || glfwGetPlatform() == GLFW_PLATFORM_COCOA)
        return;
#endif
    std::vector<GLFWimage> images;
    std::vector<unsigned char*> decoded;
    for (auto& [data, size] : pngs) {
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
        if (!px)
            continue;
        decoded.push_back(px);
        images.push_back({w, h, px});
    }
#ifndef __EMSCRIPTEN__ // browsers use the page's icon
    if (!images.empty())
        glfwSetWindowIcon(handle_, static_cast<int>(images.size()), images.data());
#endif
    for (unsigned char* px : decoded)
        stbi_image_free(px);
}

void Window::setIconFromFile(const std::filesystem::path& pngPath) {
    auto bytes = fs::readBinary(pngPath);
    if (!bytes || bytes->empty()) {
        setDefaultIcon();
        return;
    }
    setIcon({{bytes->data(), bytes->size()}});
}

void Window::setDefaultIcon() {
    std::vector<std::pair<const unsigned char*, std::size_t>> pngs;
    for (const char* name : {"rynax_16.png", "rynax_32.png", "rynax_48.png", "rynax_64.png", "rynax_128.png"}) {
        std::size_t size = 0;
        if (const unsigned char* data = embedded::find(name, &size))
            pngs.push_back({data, size});
    }
    setIcon(pngs);
}

void Window::setFullscreen(bool fullscreen) {
    if (!handle_ || fullscreen == fullscreen_)
        return;
    if (fullscreen) {
        glfwGetWindowPos(handle_, &windowedX_, &windowedY_);
        glfwGetWindowSize(handle_, &windowedW_, &windowedH_);
        // The screen the window is mostly on (not always the main one).
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        int count = 0, best = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* m = glfwGetVideoMode(monitors[i]);
            if (!m)
                continue;
            int overlapW = std::min(windowedX_ + windowedW_, mx + m->width) - std::max(windowedX_, mx);
            int overlapH = std::min(windowedY_ + windowedH_, my + m->height) - std::max(windowedY_, my);
            int area = std::max(0, overlapW) * std::max(0, overlapH);
            if (area > best) {
                best = area;
                monitor = monitors[i];
            }
        }
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode)
            return; // no screen to fill (a headless session)
        glfwSetWindowMonitor(handle_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(handle_, nullptr, windowedX_, windowedY_, windowedW_, windowedH_, 0);
    }
    fullscreen_ = fullscreen;
}

void Window::setVsync(bool vsync) { glfwSwapInterval(vsync ? 1 : 0); }

void Window::setCursorLocked(bool locked) {
    if (!handle_ || locked == cursorLocked_)
        return;
    glfwSetInputMode(handle_, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
#ifndef __EMSCRIPTEN__
    if (locked && glfwRawMouseMotionSupported())
        glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
#endif
    cursorLocked_ = locked;
}

bool Window::focused() const { return handle_ && glfwGetWindowAttrib(handle_, GLFW_FOCUSED); }
bool Window::minimized() const { return handle_ && glfwGetWindowAttrib(handle_, GLFW_ICONIFIED); }

double Window::time() { return glfwGetTime(); }

#ifdef __EMSCRIPTEN__
void* Window::glProcLoader() { return nullptr; } // WebGL functions are linked directly
#else
void* Window::glProcLoader() { return reinterpret_cast<void*>(&glfwGetProcAddress); }
#endif

} // namespace rynax
