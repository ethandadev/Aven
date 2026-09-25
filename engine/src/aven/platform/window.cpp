#include "aven/platform/window.h"

#include "aven/core/embedded.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"

#include <stb_image.h>

#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

namespace aven {

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
        if (action == GLFW_REPEAT)
            return;
        self(win)->input_.onKey(key, action == GLFW_PRESS);
    });
    glfwSetMouseButtonCallback(w, [](GLFWwindow* win, int button, int action, int) {
        self(win)->input_.onMouseButton(button, action == GLFW_PRESS);
    });
    glfwSetCursorPosCallback(w, [](GLFWwindow* win, double x, double y) {
        self(win)->input_.onMouseMove({static_cast<float>(x), static_cast<float>(y)});
    });
    glfwSetScrollCallback(w, [](GLFWwindow* win, double x, double y) {
        self(win)->input_.onScroll({static_cast<float>(x), static_cast<float>(y)});
    });
    glfwSetCharCallback(w, [](GLFWwindow* win, unsigned int c) { self(win)->input_.onChar(c); });
    glfwSetWindowFocusCallback(w, [](GLFWwindow* win, int focused) {
        if (!focused)
            self(win)->input_.releaseAll(); // avoid keys getting "stuck" after alt-tab
    });
    glfwSetDropCallback(w, [](GLFWwindow* win, int count, const char** paths) {
        Window* me = self(win);
        if (!me->onFileDrop)
            return;
        std::vector<std::string> files(paths, paths + count);
        me->onFileDrop(files);
    });
}

bool Window::shouldClose() const { return handle_ && glfwWindowShouldClose(handle_); }
void Window::requestClose() { if (handle_) glfwSetWindowShouldClose(handle_, GLFW_TRUE); }
void Window::cancelClose() { if (handle_) glfwSetWindowShouldClose(handle_, GLFW_FALSE); }

void Window::pollEvents() {
    input_.beginFrame();
    glfwPollEvents();
#ifdef __EMSCRIPTEN__
    // Browsers report gamepads in the "standard" layout.
    EmscriptenGamepadEvent pad;
    if (emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS && emscripten_get_num_gamepads() > 0 &&
        emscripten_get_gamepad_status(0, &pad) == EMSCRIPTEN_RESULT_SUCCESS && pad.connected) {
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
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1)) {
        GLFWgamepadstate state;
        if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) {
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

void Window::setIconFromFile(const std::string& pngPath) {
    auto bytes = fs::readBinary(pngPath);
    if (!bytes || bytes->empty()) {
        setDefaultIcon();
        return;
    }
    setIcon({{bytes->data(), bytes->size()}});
}

void Window::setDefaultIcon() {
    std::vector<std::pair<const unsigned char*, std::size_t>> pngs;
    for (const char* name : {"aven_16.png", "aven_32.png", "aven_48.png", "aven_64.png", "aven_128.png"}) {
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
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
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

} // namespace aven
