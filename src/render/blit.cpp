#include "game/render/blit.h"

#include <cstring>

#include "game/render/surface.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x4861B8] 全局裁剪窗口（left, top, right, bottom）
int g_clipLeft = 0;
int g_clipTop = 0;
int g_clipRight = Surface::kWidth;
int g_clipBottom = Surface::kHeight;

struct ClipResult {
    int dx = 0;
    int dy = 0;
    int sx = 0;
    int sy = 0;
    int w = 0;
    int h = 0;
    bool visible = false;
};

// 原版 sub_455C52 / sub_455FD9 / sub_455E24 共用的裁剪逻辑：
// 目标起点 = (x - offsetX, y - offsetY)，源区域 (srcX, srcY, w, h)，
// 越界时缩小绘制范围并同步推进源偏移。
ClipResult clipBlit(const UiFrameView& src, int x, int y, int srcX, int srcY, int w, int h,
                    bool useClipRect, int dstW, int dstH) {
    int clipL = 0;
    int clipT = 0;
    int clipR = dstW;
    int clipB = dstH;
    if (useClipRect) {
        getClipRect(clipL, clipT, clipR, clipB);
    }

    ClipResult r;
    int dx = x - src.offsetX;
    int dy = y - src.offsetY;

    if (dx >= clipR || dx + w <= clipL || dy >= clipB || dy + h <= clipT) {
        return r;
    }
    if (dx < clipL) {
        const int over = clipL - dx;
        srcX += over;
        w -= over;
        dx = clipL;
    }
    if (dx + w > clipR) {
        w = clipR - dx;
    }
    if (dy < clipT) {
        const int over = clipT - dy;
        srcY += over;
        h -= over;
        dy = clipT;
    }
    if (dy + h > clipB) {
        h = clipB - dy;
    }
    if (w <= 0 || h <= 0 || srcX < 0 || srcY < 0) {
        return r;
    }
    r.dx = dx;
    r.dy = dy;
    r.sx = srcX;
    r.sy = srcY;
    r.w = w;
    r.h = h;
    r.visible = true;
    return r;
}

} // namespace

void setClipRect(int left, int top, int right, int bottom) {
    g_clipLeft = left;
    g_clipTop = top;
    g_clipRight = right;
    g_clipBottom = bottom;
}

void getClipRect(int& left, int& top, int& right, int& bottom) {
    left = g_clipLeft;
    top = g_clipTop;
    right = g_clipRight;
    bottom = g_clipBottom;
}

bool blitElement(Surface& dst, const UiFrameView& src, int x, int y, bool useClipRect) {
    return blitElementRegion(dst, src, x, y, 0, 0, src.width, src.height, useClipRect);
}

bool blitElementRegion(Surface& dst, const UiFrameView& src, int x, int y, int srcX, int srcY,
                       int w, int h, bool useClipRect) {
    const ClipResult c =
        clipBlit(src, x, y, srcX, srcY, w, h, useClipRect, dst.width(), dst.height());
    if (!c.visible) {
        return false;
    }
    const uint16_t* s = src.pixels + static_cast<size_t>(c.sy) * src.width + c.sx;
    uint16_t* d = dst.pixels() + static_cast<size_t>(c.dy) * dst.width() + c.dx;
    for (int row = 0; row < c.h; ++row) {
        for (int col = 0; col < c.w; ++col) {
            const uint16_t p = s[col];
            if (p) {
                d[col] = p;
            }
        }
        s += src.width;
        d += dst.width();
    }
    return true;
}

bool blitElementRegionOpaque(Surface& dst, const UiFrameView& src, int x, int y, int srcX,
                             int srcY, int w, int h, bool useClipRect) {
    const ClipResult c =
        clipBlit(src, x, y, srcX, srcY, w, h, useClipRect, dst.width(), dst.height());
    if (!c.visible) {
        return false;
    }
    const uint16_t* s = src.pixels + static_cast<size_t>(c.sy) * src.width + c.sx;
    uint16_t* d = dst.pixels() + static_cast<size_t>(c.dy) * dst.width() + c.dx;
    for (int row = 0; row < c.h; ++row) {
        for (int col = 0; col < c.w; ++col) {
            d[col] = s[col];
        }
        s += src.width;
        d += dst.width();
    }
    return true;
}

bool blitElementOpaque(Surface& dst, const UiFrameView& src, int x, int y) {
    return blitElementRegionOpaque(dst, src, x, y, 0, 0, src.width, src.height, false);
}

void drawRectBorder(Surface& dst, int x, int y, int w, int h, uint16_t color) {
    // [RE 0x45620F] 依据: 上/下边整行 memset32，中间行只画左右两个像素
    if (w <= 0 || h <= 0) {
        return;
    }
    dst.fillRect(x, y, w, 1, color);
    dst.fillRect(x, y + h - 1, w, 1, color);
    for (int row = 1; row < h - 1; ++row) {
        dst.fillRect(x, y + row, 1, 1, color);
        dst.fillRect(x + w - 1, y + row, 1, 1, color);
    }
}

bool blitSpriteFrame(Surface& dst, const UiImage& spr, int frame, int x, int y) {
    // [RE 0x45663E] 依据: 8bit 索引像素经调色板取色, 0 透明, 裁剪到目标尺寸
    if (!spr.isSprite() || frame < 0 || frame >= spr.frameCount()) {
        return false;
    }
    const uint16_t* palette = spr.palette();
    const uint8_t* src = spr.frameBytes(frame);
    if (!palette || !src) {
        return false;
    }
    const UiFrameView& f = spr.frame(frame);
    int dx = x - f.offsetX;
    int dy = y - f.offsetY;
    int sx = 0;
    int sy = 0;
    int w = f.width;
    int h = f.height;
    if (dx < 0) {
        sx = -dx;
        w -= sx;
        dx = 0;
    }
    if (dy < 0) {
        sy = -dy;
        h -= sy;
        dy = 0;
    }
    if (dx + w > dst.width()) {
        w = dst.width() - dx;
    }
    if (dy + h > dst.height()) {
        h = dst.height() - dy;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    const uint8_t* s = src + static_cast<size_t>(sy) * f.width + sx;
    uint16_t* d = dst.pixels() + static_cast<size_t>(dy) * dst.width() + dx;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const uint8_t index = s[col];
            if (index) {
                d[col] = palette[index];
            }
        }
        s += f.width;
        d += dst.width();
    }
    return true;
}

void convertImageChannels(uint16_t* dst, const uint16_t* src, size_t pixelCount,
                          const uint8_t table[32]) {
    // [RE 0x4552B7 → 0x455337] 依据: 每像素 R/G/B 各 5bit 查表后重组 RGB555
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t p = src[i];
        const uint16_t r = table[(p >> 10) & 0x1Fu];
        const uint16_t g = table[(p >> 5) & 0x1Fu];
        const uint16_t b = table[p & 0x1Fu];
        dst[i] = static_cast<uint16_t>((r << 10) | (g << 5) | b);
    }
}

void scaleSurfaceChannels(Surface& dst, int x, int y, int w, int h, const uint8_t table[32]) {
    // [RE 0x4552E7] 依据: 对元素区域逐像素做分量缩放（调用方保证区域合法）
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > dst.width()) {
        w = dst.width() - x;
    }
    if (y + h > dst.height()) {
        h = dst.height() - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        uint16_t* line = dst.pixels() + static_cast<size_t>(y + row) * dst.width() + x;
        convertImageChannels(line, line, static_cast<size_t>(w), table);
    }
}

void grayscaleImage(uint16_t* pixels, size_t pixelCount) {
    // [RE 0x4553DA → 0x455442] 依据: gray = (R+G+B+16)>>2, 重组 R=G=B=gray; 0 保持
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t p = pixels[i];
        if (p == 0) {
            continue;
        }
        const uint16_t r = (p >> 10) & 0x1Fu;
        const uint16_t g = (p >> 5) & 0x1Fu;
        const uint16_t b = p & 0x1Fu;
        const uint16_t gray = static_cast<uint16_t>((r + g + b + 16) >> 2);
        pixels[i] = static_cast<uint16_t>((gray << 10) | (gray << 5) | gray);
    }
}

void blitScrolledMap(Surface& dst, const uint8_t* src, int offset) {
    // [RE 0x456180] 依据: 每行 1280 字节按 offset(&~3) 环绕拷贝
    constexpr int kRowBytes = Surface::kWidth * 2;
    int off = (offset & ~3) % kRowBytes;
    if (off < 0) {
        off += kRowBytes;
    }
    const int firstLen = kRowBytes - off;
    uint8_t* d = reinterpret_cast<uint8_t*>(dst.pixels());
    for (int row = 0; row < Surface::kHeight; ++row) {
        std::memcpy(d, src + off, static_cast<size_t>(firstLen));
        if (off > 0) {
            std::memcpy(d + firstLen, src, static_cast<size_t>(off));
        }
        d += kRowBytes;
        src += kRowBytes;
    }
}

} // namespace rich4
