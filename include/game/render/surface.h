#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

namespace rich4 {

// RGB555 系统内存后台缓冲。
// [RE 0x48A08C] dword_48A08C
// 依据: 0x401543 memcpy(dword_48A08C, data.mkf[601], 614400)（614400 = 640*480*2）;
//       0x451B36 sub_456F60(dword_48A08C, 0, 614400); 0x456418/0x4563F5 等以其为绘制目标;
//       0x40257A WM_PAINT 把绘制结果经 DDraw 后台表面 BltFast 到主表面
// 迁移: DDraw 后台表面 Lock/Unlock + BltFast → SDL 纹理每帧上传（RGB555 与原版一致）
// [NEW M4-A1] 画布尺寸运行期化：kWidth/kHeight 仅作设计尺寸（native 保真画布默认值），
//   实际画布尺寸由 create 决定；所有像素步长/边界必须走 width()/height()
class Surface {
public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;

    bool create(SDL_Renderer* renderer, int width = kWidth, int height = kHeight);
    void destroy();
    void present(SDL_Renderer* renderer);

    uint16_t* pixels() { return m_pixels.data(); }
    const uint16_t* pixels() const { return m_pixels.data(); }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // [NEW M4-A2] 绘制缩放：逻辑坐标（设计 640x480 坐标系）→ 设备像素的倍率。
    //   1.0 = native 恒等（默认）；>1 时全部绘制 API 接收逻辑坐标并内部按 scale 变换，
    //   文本以 设计字号×scale 光栅化后 1:1 写入（无二次放大）。
    //   地图层（renderMap/演出叠加）经 SurfaceScaleGuard 固定 1:1（worldScale=1，A2）。
    void setScale(float scale) { m_scale = scale > 0.0f ? scale : 1.0f; }
    float scale() const { return m_scale; }
    // 逻辑坐标/长度 → 设备像素（round；scale=1 恒等）
    int logicalToDevice(int v) const {
        return static_cast<int>(v * m_scale + (v >= 0 ? 0.5f : -0.5f));
    }
    // 起点 v、长度 len 的逻辑区间 → 设备长度（首尾独立取整，保证相邻区间不重叠/不丢行）
    int logicalSpanToDevice(int v, int len) const {
        return logicalToDevice(v + len) - logicalToDevice(v);
    }
    // 设备像素 → 逻辑坐标（round；scale=1 恒等）
    int deviceToLogical(int d) const {
        return m_scale == 1.0f
                   ? d
                   : static_cast<int>(d / m_scale + (d >= 0 ? 0.5f : -0.5f));
    }

    // [NEW M4-D] 绘制原点（逻辑坐标偏移）：右栏在宽屏下整体右移 panelX-440 时，右栏
    //   绘制函数内坐标保持 440 基准不动，由 origin 统一平移；native origin=(0,0) 恒等。
    //   坐标（x/y）与长度（span）一律经本组 helper 变换；全局裁剪矩形为画布逻辑坐标，
    //   不经 origin（保持 logicalToDevice）。
    void setOrigin(int x, int y) {
        m_originX = x;
        m_originY = y;
    }
    int originX() const { return m_originX; }
    int originY() const { return m_originY; }

    // [NEW M4-D 实机] 呈现放大过滤：true = 线性（平滑，减少马赛克；逻辑画布→窗口放大用），
    //   false = 最近邻（像素完美）。1:1 呈现时两者等价（native 不受影响）。
    void setLinearFilter(bool on) {
        m_linearFilter = on;
        applyFilter();
    }
    bool linearFilter() const { return m_linearFilter; }

    // [NEW M4-D 实机] 模态绘制边界（画布逻辑坐标；x0<0 = 禁用）：640 基准模态的绘制
    //   限制在基准区内（模拟原版 640×480 屏幕边界），防止"移出屏幕"的过场动画
    //   （选人界面确认后玩家槽/面板滑出）画进宽画布两侧黑边（观测为黑边残影）。
    //   由 Application::dispatchModalAware 在 centerBase 模态 dispatch 期间设置。
    void setPaintClip(int x0, int x1) {
        m_paintClipX0 = x0;
        m_paintClipX1 = x1;
    }
    bool paintClipEnabled() const { return m_paintClipX0 >= 0; }
    int paintClipX0() const { return m_paintClipX0; }
    int paintClipX1() const { return m_paintClipX1; }
    int deviceX(int v) const { return logicalToDevice(m_originX + v); }
    int deviceY(int v) const { return logicalToDevice(m_originY + v); }
    int spanX(int v, int len) const {
        return logicalToDevice(m_originX + v + len) - logicalToDevice(m_originX + v);
    }
    int spanY(int v, int len) const {
        return logicalToDevice(m_originY + v + len) - logicalToDevice(m_originY + v);
    }

    void clear();

    // [RE 0x4561BE] sub_4561BE
    // 依据: 0x4561BE 以 memset32 按行填充 (x,y,w,h)，颜色由 sub_4551F0 转换
    void fillRect(int x, int y, int w, int h, uint16_t color);

    // [NEW] 调试截图（24bit BMP）；原版无对应功能，用于与原版画面比对
    bool saveBmp(const char* path) const;

private:
    void applyFilter();

    std::vector<uint16_t> m_pixels;
    SDL_Texture* m_texture = nullptr;
    int m_width = kWidth;
    int m_height = kHeight;
    float m_scale = 1.0f;
    bool m_linearFilter = true; // [NEW M4-D 实机] 呈现放大过滤（默认线性）
    int m_originX = 0; // [NEW M4-D] 绘制原点（逻辑）
    int m_originY = 0;
    int m_paintClipX0 = -1; // [NEW M4-D 实机] 模态绘制边界（画布逻辑；-1 禁用）
    int m_paintClipX1 = -1;
};

// [NEW M4-D] 临时设置绘制原点的 RAII guard（右栏宽屏平移）。
class SurfaceOriginGuard {
public:
    SurfaceOriginGuard(Surface& surface, int x, int y)
        : m_surface(surface), m_oldX(surface.originX()), m_oldY(surface.originY()) {
        surface.setOrigin(x, y);
    }
    ~SurfaceOriginGuard() { m_surface.setOrigin(m_oldX, m_oldY); }
    SurfaceOriginGuard(const SurfaceOriginGuard&) = delete;
    SurfaceOriginGuard& operator=(const SurfaceOriginGuard&) = delete;

private:
    Surface& m_surface;
    int m_oldX;
    int m_oldY;
};

// [NEW M4-D 实机] 临时设置/恢复模态绘制边界（renderGameFrame 重绘游戏画面时关闭，
//   避免宽屏游戏画面被 640 基准边界裁掉）。
class SurfacePaintClipGuard {
public:
    SurfacePaintClipGuard(Surface& surface, int x0, int x1)
        : m_surface(surface), m_old0(surface.paintClipX0()), m_old1(surface.paintClipX1()) {
        surface.setPaintClip(x0, x1);
    }
    ~SurfacePaintClipGuard() { m_surface.setPaintClip(m_old0, m_old1); }
    SurfacePaintClipGuard(const SurfacePaintClipGuard&) = delete;
    SurfacePaintClipGuard& operator=(const SurfacePaintClipGuard&) = delete;

private:
    Surface& m_surface;
    int m_old0;
    int m_old1;
};

// [NEW M4-A2] 临时切换绘制缩放的 RAII guard（地图层 worldScale=1；A2 用）。
// 用法：进入地图/演出叠加等 1:1 绘制段时构造，离开自动恢复。
class SurfaceScaleGuard {
public:
    SurfaceScaleGuard(Surface& surface, float scale)
        : m_surface(surface), m_old(surface.scale()) {
        surface.setScale(scale);
    }
    ~SurfaceScaleGuard() { m_surface.setScale(m_old); }
    SurfaceScaleGuard(const SurfaceScaleGuard&) = delete;
    SurfaceScaleGuard& operator=(const SurfaceScaleGuard&) = delete;

private:
    Surface& m_surface;
    float m_old;
};

// [RE 0x45523E] sub_45523E（经 0x4551F0 funcs_455200[0] 分发）
// 依据: 0x45523E 返回 (c>>3)&0x1F | (c>>6)&0x3E0 | (c>>9)&0x7C00（RGB888→RGB555）
// 迁移: SDL 侧内部固定 RGB555，原版 dword_47637C 显示格式分支（sub_45175D 检测）不再需要
constexpr uint16_t rgb888To555(uint32_t c) {
    return static_cast<uint16_t>(((c >> 3) & 0x1Fu) | ((c >> 6) & 0x3E0u) | ((c >> 9) & 0x7C00u));
}

} // namespace rich4
