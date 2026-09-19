#pragma once

#include <SDL3/SDL.h>

namespace rich4 {

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
