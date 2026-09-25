#pragma once

#include "aven/math/math.h"
#include "aven/platform/input.h"

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace aven {

struct WindowDesc {
    std::string title = "Aven";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
    bool visible = true; // hidden windows are used for tests and screenshots
    bool maximized = false;
    int samples = 0;
};

// An OS window with an OpenGL context. Input events are forwarded into an Input.
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const WindowDesc& desc);
    void destroy();
    bool isOpen() const { return handle_ != nullptr; }
    bool shouldClose() const;
    void requestClose();
    void cancelClose();

    void pollEvents();
    void swapBuffers();

    Vec2 framebufferSize() const; // pixels
    Vec2 windowSize() const;      // screen coordinates
    float contentScale() const;   // UI scale on high-DPI displays
    void setTitle(const std::string& title);
    // Window/taskbar icon from PNG files in memory (several sizes let the OS pick).
    // Unsupported on some platforms (macOS uses the app bundle's icon), where it does nothing.
    void setIcon(const std::vector<std::pair<const unsigned char*, std::size_t>>& pngs);
    void setIconFromFile(const std::string& pngPath);
    void setDefaultIcon(); // the Aven logo
    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return fullscreen_; }
    void setVsync(bool vsync);
    void setCursorLocked(bool locked); // hides and captures the mouse (first-person games)
    bool cursorLocked() const { return cursorLocked_; }
    bool focused() const;
    bool minimized() const;

    static double time();
    static void* glProcLoader(); // GLFW's getProcAddress, for the GL device

    Input& input() { return input_; }
    GLFWwindow* native() const { return handle_; }

    std::function<void(const std::vector<std::string>&)> onFileDrop;

private:
    GLFWwindow* handle_ = nullptr;
    Input input_;
    bool fullscreen_ = false;
    bool cursorLocked_ = false;
    int windowedX_ = 100, windowedY_ = 100, windowedW_ = 1280, windowedH_ = 720;

    static void installCallbacks(GLFWwindow* w);
};

} // namespace aven
