#include "game/render/blit.h"

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

} // namespace rich4
