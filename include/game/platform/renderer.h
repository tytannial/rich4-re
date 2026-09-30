#pragma once

#include <SDL3/SDL.h>

namespace rich4 {

// [PORT DDraw:SetDisplayMode/CreateSurface] 显示与表面
// 替换依据: 0x4015D6 SetDisplayMode(640,480,16) + CreateSurface（主表面 512x512 /
//           后台表面 640x480）→ SDL_Renderer + 逻辑分辨率 640x480；
//           原版 16bit 调色板表面（sub_44F935 设色）在纹理上传时展开为 RGBA
// 逻辑分辨率固定 640x480，实际窗口可宽屏缩放（letterbox / 拉伸可配置）。
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool create(SDL_Window* window, int logicalWidth, int logicalHeight, bool vsync = true);
    void destroy();

    void beginFrame();
    void present();

    SDL_Renderer* handle() const { return m_renderer; }
    int logicalWidth() const { return m_logicalWidth; }
    int logicalHeight() const { return m_logicalHeight; }

    void setWidescreen(bool enabled);
    // [NEW M4-A2] 重设逻辑呈现尺寸（free preset 窗口 resize 时与画布同步）
    void setLogicalSize(int width, int height);

private:
    SDL_Renderer* m_renderer = nullptr;
    int m_logicalWidth = 640;
    int m_logicalHeight = 480;
};

} // namespace rich4
