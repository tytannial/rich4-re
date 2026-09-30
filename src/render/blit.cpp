#include "game/render/blit.h"

#include <cstring>

#include "game/render/surface.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x4861B8] 全局裁剪窗口（left, top, right, bottom）
// 初值 = 设计尺寸；Surface::create 成功后会重置为运行期画布全尺寸
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

// [NEW M4-A2] 缩放 blit 核心（nearest 采样）：把源区域按画布 scale 采样到逻辑目标。
//   src = 源像素基址；srcPitchBytes = 源每行字节数；
//   palette != nullptr → 8bit 索引 + 调色板（SPR，索引 0 透明）；
//   palette == nullptr → RGB555（每像素 2B；opaque=false 时像素 == colorKey 透明）。
//   逻辑目标左上 (logX, logY)（调用方已减去帧 offset）；源区域 (srcX, srcY, w, h)，
//   逻辑尺寸 (w, h) 放大为设备 dw×dh；设备坐标 = round(逻辑 × scale)。
//   公共导出：UI 层逐像素叠加（FLC/RAW 位图）复用，避免每处重复设备化+采样。
bool blitScaled(Surface& dst, const uint8_t* src, int srcPitchBytes, const uint16_t* palette,
                int logX, int logY, int srcX, int srcY, int w, int h, bool useClipRect,
                bool opaque, uint16_t colorKey) {
    if (!src || w <= 0 || h <= 0) {
        return false;
    }
    const int dx0 = dst.logicalToDevice(logX);
    const int dy0 = dst.logicalToDevice(logY);
    const int dx1 = dst.logicalToDevice(logX + w);
    const int dy1 = dst.logicalToDevice(logY + h);
    const int dw = dx1 - dx0;
    const int dh = dy1 - dy0;
    if (dw <= 0 || dh <= 0) {
        return false;
    }
    int clipL = 0;
    int clipT = 0;
    int clipR = dst.width();
    int clipB = dst.height();
    if (useClipRect) {
        // [NEW M4-A2] 全局裁剪为设计逻辑坐标 → 设备化（scale=1 恒等）
        getClipRect(clipL, clipT, clipR, clipB);
        clipL = dst.logicalToDevice(clipL);
        clipT = dst.logicalToDevice(clipT);
        clipR = dst.logicalToDevice(clipR);
        clipB = dst.logicalToDevice(clipB);
    }
    const int vx0 = dx0 < clipL ? clipL : dx0;
    const int vy0 = dy0 < clipT ? clipT : dy0;
    const int vx1 = dx1 < clipR ? dx1 : clipR;
    const int vy1 = dy1 < clipB ? dy1 : clipB;
    if (vx0 >= vx1 || vy0 >= vy1) {
        return false;
    }
    const bool sprite = palette != nullptr;
    for (int vy = vy0; vy < vy1; ++vy) {
        const int sy = srcY + (vy - dy0) * h / dh;
        const uint8_t* srow = src + static_cast<size_t>(sy) * srcPitchBytes;
        uint16_t* drow = dst.pixels() + static_cast<size_t>(vy) * dst.width();
        for (int vx = vx0; vx < vx1; ++vx) {
            const int sx = srcX + (vx - dx0) * w / dw;
            if (sprite) {
                const uint8_t idx = srow[sx];
                if (!opaque && idx == 0) {
                    continue;
                }
                drow[vx] = palette[idx];
            } else {
                const uint16_t p = reinterpret_cast<const uint16_t*>(srow)[sx];
                if (!opaque && p == colorKey) {
                    continue;
                }
                drow[vx] = p;
            }
        }
    }
    return true;
}

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
    // [NEW M4-A2] scale != 1：逻辑目标缩放 + nearest 采样（scale == 1 走原路径，逐字节一致）
    if (dst.scale() != 1.0f) {
        return blitScaled(dst, reinterpret_cast<const uint8_t*>(src.pixels), src.width * 2,
                          nullptr, x - src.offsetX, y - src.offsetY, srcX, srcY, w, h, useClipRect,
                          false);
    }
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
    // [NEW M4-A2] scale != 1：逻辑目标缩放 + nearest 采样（scale == 1 走原路径，逐字节一致）
    if (dst.scale() != 1.0f) {
        return blitScaled(dst, reinterpret_cast<const uint8_t*>(src.pixels), src.width * 2,
                          nullptr, x - src.offsetX, y - src.offsetY, srcX, srcY, w, h, useClipRect,
                          true);
    }
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

void pressDown(Surface& dst, int x, int y, int w, int h, int shift, const uint8_t table[32]) {
    // [RE 0x451B9E] 依据: 自底向上逐行把内容右移 shift/下移 shift（长度 w-shift），
    // 再把顶边 shift 行 + 左边 shift 列（不含顶边）变暗
    if (shift <= 0 || w <= shift || h <= shift) {
        return;
    }
    // [NEW M4-A2] 越界检查按逻辑画布（scale != 1 时设备宽高 != 640x480）
    if (x < 0 || y < 0 || dst.logicalToDevice(x + w) > dst.width() ||
        dst.logicalToDevice(y + h) > dst.height()) {
        return;
    }
    // [NEW M4-A2] 位移与搬运在设备域（scale=1 恒等）
    const int dx = dst.logicalToDevice(x);
    const int dy = dst.logicalToDevice(y);
    const int dw = dst.logicalToDevice(x + w) - dx;
    const int dh = dst.logicalToDevice(y + h) - dy;
    const int dshift = dst.logicalToDevice(shift);
    if (dshift <= 0 || dw <= dshift || dh <= dshift) {
        return;
    }
    uint16_t* p = dst.pixels();
    for (int row = dh - 1 - dshift; row >= 0; --row) {
        uint16_t* d = p + static_cast<size_t>(dy + row + dshift) * dst.width() + (dx + dshift);
        const uint16_t* s = p + static_cast<size_t>(dy + row) * dst.width() + dx;
        std::memmove(d, s, static_cast<size_t>(dw - dshift) * sizeof(uint16_t));
    }
    scaleSurfaceChannels(dst, x, y, w, shift, table);          // 顶边
    scaleSurfaceChannels(dst, x, y + shift, shift, h - shift, table);  // 左边
}

bool blitSpriteFrame(Surface& dst, const UiImage& spr, int frame, int x, int y) {
    return blitSpriteFrameClipped(dst, spr, frame, x, y, false);
}

bool blitSpriteFrameClipped(Surface& dst, const UiImage& spr, int frame, int x, int y,
                            bool useClipRect) {
    // [RE 0x456770] 依据: 8bit 索引像素经调色板取色, 0 透明, 裁剪到全局裁剪矩形
    if (!spr.isSprite() || frame < 0 || frame >= spr.frameCount()) {
        return false;
    }
    const uint16_t* palette = spr.palette();
    const uint8_t* src = spr.frameBytes(frame);
    if (!palette || !src) {
        return false;
    }
    const UiFrameView& f = spr.frame(frame);
    // [NEW M4-A2] scale != 1：逻辑目标缩放 + nearest 采样（scale == 1 走原路径，逐字节一致）
    if (dst.scale() != 1.0f) {
        return blitScaled(dst, src, f.width, palette, x - f.offsetX, y - f.offsetY, 0, 0, f.width,
                          f.height, useClipRect, false);
    }
    const ClipResult c = clipBlit(f, x, y, 0, 0, f.width, f.height, useClipRect, dst.width(),
                                  dst.height());
    if (!c.visible) {
        return false;
    }
    const uint8_t* s = src + static_cast<size_t>(c.sy) * f.width + c.sx;
    uint16_t* d = dst.pixels() + static_cast<size_t>(c.dy) * dst.width() + c.dx;
    for (int row = 0; row < c.h; ++row) {
        for (int col = 0; col < c.w; ++col) {
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
    // [NEW M4-A2] 逻辑区域 → 设备区域（scale=1 恒等）
    if (dst.scale() != 1.0f) {
        const int x0 = dst.logicalToDevice(x);
        const int y0 = dst.logicalToDevice(y);
        const int x1 = dst.logicalToDevice(x + w);
        const int y1 = dst.logicalToDevice(y + h);
        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
    }
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
    // 源 = 640×480 地图预览位图（行距固定 1280B，共 480 行）；目标行距按运行期画布宽度；
    // 行数取源/画布较小者——画布高于 480 时下方不写（源没有对应行，越界读会崩溃）
    constexpr int kSrcRowBytes = Surface::kWidth * 2;
    constexpr int kSrcRows = Surface::kHeight;
    const int dstRowBytes = dst.width() * static_cast<int>(sizeof(uint16_t));
    int off = (offset & ~3) % kSrcRowBytes;
    if (off < 0) {
        off += kSrcRowBytes;
    }
    // [NEW M4-A2] scale != 1：逻辑 640x480 预览按画布缩放（nearest）+ 水平环绕采样；
    //   scale == 1 走下方原逐行环绕路径（逐字节一致）
    if (dst.scale() != 1.0f) {
        const uint16_t* srcPx = reinterpret_cast<const uint16_t*>(src);
        const int offPix = off / 2; // 源 4 字节对齐 → 2 像素对齐
        const int cw = dst.logicalToDevice(Surface::kWidth);
        const int ch = dst.logicalToDevice(Surface::kHeight);
        const int rows = ch < dst.height() ? ch : dst.height();
        const int cols = cw < dst.width() ? cw : dst.width();
        for (int vy = 0; vy < rows; ++vy) {
            const int sy = vy * kSrcRows / ch;
            if (sy >= kSrcRows) {
                continue;
            }
            const uint16_t* srow = srcPx + static_cast<size_t>(sy) * Surface::kWidth;
            uint16_t* drow = dst.pixels() + static_cast<size_t>(vy) * dst.width();
            for (int vx = 0; vx < cols; ++vx) {
                const int sx = (vx * Surface::kWidth / cw + offPix) % Surface::kWidth;
                drow[vx] = srow[sx];
            }
        }
        return;
    }
    const int firstLen = kSrcRowBytes - off;
    const int copyLen = firstLen < dstRowBytes ? firstLen : dstRowBytes;
    const int secondLen = (dstRowBytes - firstLen) < off ? dstRowBytes - firstLen : off;
    const int rows = dst.height() < kSrcRows ? dst.height() : kSrcRows;
    uint8_t* d = reinterpret_cast<uint8_t*>(dst.pixels());
    for (int row = 0; row < rows; ++row) {
        std::memcpy(d, src + off, static_cast<size_t>(copyLen));
        if (secondLen > 0) {
            std::memcpy(d + firstLen, src, static_cast<size_t>(secondLen));
        }
        d += dstRowBytes;
        src += kSrcRowBytes;
    }
}

bool saveRegion(std::vector<uint16_t>& out, const Surface& dst, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    // [NEW M4-A2] 逻辑区域 → 设备区域（save/restore 同一 scale 下自洽；scale=1 恒等）
    if (dst.scale() != 1.0f) {
        const int x0 = dst.logicalToDevice(x);
        const int y0 = dst.logicalToDevice(y);
        const int x1 = dst.logicalToDevice(x + w);
        const int y1 = dst.logicalToDevice(y + h);
        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
        if (w <= 0 || h <= 0) {
            return false;
        }
    }
    out.assign(static_cast<size_t>(w) * h, 0);
    const uint16_t* src = dst.pixels();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= dst.width()) {
                continue;
            }
            out[static_cast<size_t>(row) * w + col] =
                src[static_cast<size_t>(py) * dst.width() + px];
        }
    }
    return true;
}

void restoreRegion(Surface& dst, const std::vector<uint16_t>& bg, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    // [NEW M4-A2] 逻辑区域 → 设备区域（与 saveRegion 同一 scale 下自洽；scale=1 恒等）
    if (dst.scale() != 1.0f) {
        const int x0 = dst.logicalToDevice(x);
        const int y0 = dst.logicalToDevice(y);
        const int x1 = dst.logicalToDevice(x + w);
        const int y1 = dst.logicalToDevice(y + h);
        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
    }
    if (w <= 0 || h <= 0 || bg.size() < static_cast<size_t>(w) * h) {
        return;
    }
    uint16_t* d = dst.pixels();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= dst.width()) {
                continue;
            }
            d[static_cast<size_t>(py) * dst.width() + px] =
                bg[static_cast<size_t>(row) * w + col];
        }
    }
}

} // namespace rich4
