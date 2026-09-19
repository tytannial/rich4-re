#include "game/render/cursor.h"

#include "game/render/surface.h"

#include <SDL3/SDL.h>

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
    // 把保存的背景区域写回表面
    if (!m_visible) {
        return;
    }
    for (int row = 0; row < m_saveH; ++row) {
        uint16_t* d = dst.pixels() + static_cast<size_t>(m_saveY + row) * dst.width() + m_saveX;
        const uint16_t* s = m_saved.data() + static_cast<size_t>(row) * kSize;
        for (int col = 0; col < m_saveW; ++col) {
            d[col] = s[col];
        }
    }
    m_visible = false;
}

void Cursor::compose(Surface& dst, int x, int y) {
    const UiFrameView* frame = currentFrame();
    if (!frame || !frame->pixels) {
        return;
    }

    // [RE 0x401E59] 依据: 光标位置 = (x - offsetX, y - offsetY)，裁剪到 640x480;
    // 先 sub_451A97 保存背景，再 sub_4562A5 绘制光标
    int dx = x - frame->offsetX;
    int dy = y - frame->offsetY;
    int w = kSize;
    int h = kSize;
    int sx = 0;
    int sy = 0;
    if (dx < 0) {
        sx = -dx;
        w += dx;
        dx = 0;
    }
    if (dy < 0) {
        sy = -dy;
        h += dy;
        dy = 0;
    }
    if (dx + w > dst.width()) {
        w = dst.width() - dx;
    }
    if (dy + h > dst.height()) {
        h = dst.height() - dy;
    }
    if (w <= 0 || h <= 0) {
        m_visible = false;
        return;
    }

    m_saveX = dx;
    m_saveY = dy;
    m_saveW = w;
    m_saveH = h;
    for (int row = 0; row < h; ++row) {
        const uint16_t* s = dst.pixels() + static_cast<size_t>(dy + row) * dst.width() + dx;
        uint16_t* d = m_saved.data() + static_cast<size_t>(row) * kSize;
        for (int col = 0; col < w; ++col) {
            d[col] = s[col];
        }
    }

    const uint16_t* src = frame->pixels + static_cast<size_t>(sy) * frame->width + sx;
    for (int row = 0; row < h; ++row) {
        uint16_t* d = dst.pixels() + static_cast<size_t>(dy + row) * dst.width() + dx;
        for (int col = 0; col < w; ++col) {
            const uint16_t p = src[col];
            if (p) {
                d[col] = p;
            }
        }
        src += frame->width;
    }
    m_visible = true;
    m_posX = x;
    m_posY = y;
}

void Cursor::update(Surface& dst, int mouseX, int mouseY) {
    if (!m_initialized) {
        return;
    }
    // [RE 0x401F98] 动画帧推进：word_48A176 >= word_48A174 时 word_48A172 递增回绕
    bool frameChanged = false;
    if (m_frameCount > 1 && ++m_frameTick >= m_frameDelay) {
        m_frameTick = 0;
        if (++m_frame >= m_frameCount) {
            m_frame = 0;
        }
        frameChanged = true;
    }

    if (frameChanged || mouseX != m_posX || mouseY != m_posY) {
        uncompose(dst);
        compose(dst, mouseX, mouseY);
    }
}

void Cursor::getRect(int mouseX, int mouseY, int outRect[4]) const {
    // [RE 0x4024C0] 依据: 光标位置 + 当前帧 offset 得到 32x32 包围盒，裁剪到 640x480
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
    if (outRect[2] > Surface::kWidth) {
        outRect[2] = Surface::kWidth;
    }
    if (outRect[3] > Surface::kHeight) {
        outRect[3] = Surface::kHeight;
    }
}

} // namespace rich4
