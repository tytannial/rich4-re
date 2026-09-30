#include "game/render/cursor.h"

#include "game/render/blit.h"
#include "game/render/surface.h"

#include <SDL3/SDL.h>

#include <utility>

namespace rich4 {

bool Cursor::init(std::vector<uint8_t> resource) {
    // [RE 0x4020FA] 依据: lpBuffer = sub_450441(dword_48A0E4, 0)（data.mkf[0] 光标资源）
    if (!m_image.load(std::move(resource)) || m_image.frameCount() == 0) {
        return false;
    }
    // dword_48A0E8 = sub_451A5A(32, 32, 0, 0) 背景保存元素
    m_saved.assign(static_cast<size_t>(kSize) * kSize, 0);
    // ShowCursor(0)：SDL 侧隐藏系统光标，由软件光标替代
    SDL_HideCursor();
    m_visible = false;
    m_initialized = true;
    return true;
}

void Cursor::shutdown() {
    m_initialized = false;
    m_visible = false;
    m_saved.clear();
    SDL_ShowCursor();
}

void Cursor::select(int cursorIndex, int frameCount, int frameDelay) {
    // [RE 0x4021F8] 依据: dword_48A0F4 = lpBuffer + 12*index + 12; word_48A170 = 帧数;
    // word_48A174 = 帧间隔; word_48A172/word_48A176 = 0
    m_cursorIndex = cursorIndex;
    m_frameCount = frameCount > 0 ? frameCount : 1;
    m_frameDelay = frameDelay > 0 ? frameDelay : 1;
    m_frame = 0;
    m_frameTick = 0;
}

const UiFrameView* Cursor::currentFrame() const {
    if (!m_initialized) {
        return nullptr;
    }
    const int index = m_cursorIndex + m_frame;
    if (index < 0 || index >= m_image.frameCount()) {
        return nullptr;
    }
    return &m_image.frame(index);
}

void Cursor::uncompose(Surface& dst) {
    // [RE 0x401F5E] 依据: sub_456280(word_46CB14, dword_48A0E8, dword_48A0EC, dword_48A0F0)
    // 把保存的背景区域写回表面（设备域；行距 = m_saveStride）
    if (!m_visible) {
        return;
    }
    for (int row = 0; row < m_saveH; ++row) {
        uint16_t* d = dst.pixels() + static_cast<size_t>(m_saveY + row) * dst.width() + m_saveX;
        const uint16_t* s = m_saved.data() + static_cast<size_t>(row) * m_saveStride;
        for (int col = 0; col < m_saveW; ++col) {
            d[col] = s[col];
        }
    }
    m_visible = false;
}

void Cursor::blitFrameDevice(Surface& dst, const UiFrameView& frame, int devX, int devY) const {
    // [NEW M4-A2] 帧按画布 scale 放大（设备尺寸 nearest 采样；0 透明）。
    //   光标资源为 SMP（16bit 像素直取）或 SPR（8bit 索引 + 调色板）——两种都支持
    const bool sprite = m_image.isSprite();
    const uint8_t* idx8 = sprite ? m_image.frameBytes(m_cursorIndex + m_frame) : nullptr;
    const uint16_t* palette = m_image.palette();
    const uint16_t* px16 = sprite ? nullptr : frame.pixels;
    if ((sprite && (!idx8 || !palette)) || (!sprite && !px16)) {
        return;
    }
    const int dw = dst.logicalToDevice(frame.width);
    const int dh = dst.logicalToDevice(frame.height);
    if (dw <= 0 || dh <= 0) {
        return;
    }
    for (int vy = 0; vy < dh; ++vy) {
        const int py = devY + vy;
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        const int sy = vy * frame.height / dh;
        const uint8_t* srow8 = idx8 ? idx8 + static_cast<size_t>(sy) * frame.width : nullptr;
        const uint16_t* srow16 =
            px16 ? px16 + static_cast<size_t>(sy) * frame.width : nullptr;
        uint16_t* drow = dst.pixels() + static_cast<size_t>(py) * dst.width();
        for (int vx = 0; vx < dw; ++vx) {
            const int px = devX + vx;
            if (px < 0 || px >= dst.width()) {
                continue;
            }
            const int sx = vx * frame.width / dw;
            if (sprite) {
                const uint8_t idx = srow8[sx];
                if (idx != 0) {
                    drow[px] = palette[idx];
                }
            } else {
                const uint16_t c = srow16[sx];
                if (c != 0) {
                    drow[px] = c;
                }
            }
        }
    }
}

void Cursor::compose(Surface& dst, int x, int y) {
    // [NEW] 演出段隐藏：不画也不保存背景（位置跟踪由 update 维持）
    if (m_hidden) {
        m_posX = x;
        m_posY = y;
        m_visible = false;
        return;
    }
    const UiFrameView* frame = currentFrame();
    if (!frame || !frame->pixels) {
        return;
    }

    // [RE 0x401E59] 依据: 光标位置 = (x - offsetX, y - offsetY);
    // 背景保存区域固定 32x32（原版 v2=v3=32），裁剪到屏幕;
    // 光标帧由 sub_4562A5→sub_455C52 按帧自身尺寸（如 23x23）blit
    // [NEW M4-A2] 鼠标坐标 x/y 为设备坐标：offset 与包围盒按画布 scale 设备化
    const int saveXDev = dst.logicalToDevice(frame->offsetX);
    const int saveYDev = dst.logicalToDevice(frame->offsetY);
    int saveX = x - saveXDev;
    int saveY = y - saveYDev;
    int saveW = dst.logicalToDevice(kSize);
    int saveH = saveW;
    if (saveX < 0) {
        saveW += saveX;
        saveX = 0;
    }
    if (saveY < 0) {
        saveH += saveY;
        saveY = 0;
    }
    if (saveX + saveW > dst.width()) {
        saveW = dst.width() - saveX;
    }
    if (saveY + saveH > dst.height()) {
        saveH = dst.height() - saveY;
    }
    if (saveW <= 0 || saveH <= 0) {
        m_visible = false;
        return;
    }

    m_saveX = saveX;
    m_saveY = saveY;
    m_saveW = saveW;
    m_saveH = saveH;
    m_saveStride = saveW;
    if (m_saved.size() < static_cast<size_t>(saveW) * saveH) {
        m_saved.assign(static_cast<size_t>(saveW) * saveH, 0);
    }
    for (int row = 0; row < saveH; ++row) {
        const uint16_t* s = dst.pixels() + static_cast<size_t>(saveY + row) * dst.width() + saveX;
        uint16_t* d = m_saved.data() + static_cast<size_t>(row) * m_saveStride;
        for (int col = 0; col < saveW; ++col) {
            d[col] = s[col];
        }
    }

    blitFrameDevice(dst, *frame, saveX, saveY);
    m_visible = true;
    m_posX = x;
    m_posY = y;
}

void Cursor::update(Surface& dst, int mouseX, int mouseY) {
    if (!m_initialized) {
        return;
    }
    // [RE 0x401F98] 动画帧推进：word_48A176 >= word_48A174 时 word_48A172 递增回绕
    if (m_frameCount > 1 && ++m_frameTick >= m_frameDelay) {
        m_frameTick = 0;
        if (++m_frame >= m_frameCount) {
            m_frame = 0;
        }
    }
    // [PORT 乐透花屏修正 2026-09-29] 原版 fptc 定时器独立于绘制，位置变化时需先恢复背景
    //   再重绘；重写管线 renderFrame 已保证"每帧 compose（从当前干净表面抓背景）→
    //   present → uncompose 恢复"，表面恒为无光标内容——若沿用旧"先 uncompose 上一帧
    //   保存块再 compose"的写法，即时模式界面（乐透/月结/游戏内每 tick 全量重绘）里
    //   上一帧保存块与新背景不符 → 光标旧位置 32x32 旧背景脏块闪现一帧（乐透"轻微闪烁
    //   花屏"根因）。改为 update 只推进动画与位置跟踪，重绘交 renderFrame 的
    //   "if (!visible) compose" 每帧从最新背景重抓。
    m_posX = mouseX;
    m_posY = mouseY;
    (void)dst;
}

void Cursor::getRect(const Surface& dst, int mouseX, int mouseY, int outRect[4]) const {
    // [RE 0x4024C0] 依据: 光标位置 + 当前帧 offset 得到 32x32 包围盒，裁剪到画布
    const UiFrameView* frame = currentFrame();
    const int ox = frame ? frame->offsetX : 0;
    const int oy = frame ? frame->offsetY : 0;
    outRect[0] = mouseX - ox;
    outRect[1] = mouseY - oy;
    outRect[2] = outRect[0] + kSize;
    outRect[3] = outRect[1] + kSize;
    if (outRect[0] < 0) {
        outRect[0] = 0;
    }
    if (outRect[1] < 0) {
        outRect[1] = 0;
    }
    if (outRect[2] > dst.width()) {
        outRect[2] = dst.width();
    }
    if (outRect[3] > dst.height()) {
        outRect[3] = dst.height();
    }
}

} // namespace rich4
