#pragma once

#include <SDL3/SDL.h>

namespace rich4 {

// [PORT Win32:CreateWindowExA] 主窗口
// 替换依据: 0x401B9C CreateWindowExA(0, "Rich4", "Rich4", WS_POPUP(0x80000000),
//           屏幕宽高) → SDL_CreateWindow；原版全屏无边框，SDL 侧窗口化可缩放
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const char* title, int width, int height, bool resizable = true);
    void destroy();

    SDL_Window* handle() const { return m_window; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace rich4
