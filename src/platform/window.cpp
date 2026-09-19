#include "game/platform/window.h"

#include "game/core/log.h"

namespace rich4 {

Window::~Window() { destroy(); }

bool Window::create(const char* title, int width, int height, bool resizable) {
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    m_window = SDL_CreateWindow(title, width, height, flags);
    if (!m_window) {
        RICH4_LOGE("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    m_width = width;
    m_height = height;
    return true;
}

void Window::destroy() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

} // namespace rich4
