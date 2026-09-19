#pragma once

#include <SDL3/SDL.h>

namespace rich4 {

// 逻辑分辨率固定 640x480，实际窗口可宽屏缩放（letterbox / 拉伸可配置）。
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool create(SDL_Window* window, int logicalWidth, int logicalHeight);
    void destroy();

    void beginFrame();
    void present();

    SDL_Renderer* handle() const { return m_renderer; }
    int logicalWidth() const { return m_logicalWidth; }
    int logicalHeight() const { return m_logicalHeight; }

    void setWidescreen(bool enabled);

private:
    SDL_Renderer* m_renderer = nullptr;
    int m_logicalWidth = 640;
    int m_logicalHeight = 480;
};

} // namespace rich4
