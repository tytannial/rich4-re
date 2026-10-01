#include "game/render/iso_blit.h"

#include <algorithm>
#include <cmath>

#include "game/render/blit.h"
#include "game/render/surface.h"

namespace rich4 {

void isoBlitQuad(Surface& dst, const uint8_t* tile, const int16_t quad[4][2],
                 const uint16_t* palette, bool useClipRect) {
    // [RE 0x4557A1] 依据: 4 顶点四边形 + 32x32 8bit 纹理逐行扫描填充；
    // 顶点纹理坐标 (0,0)→(32,0)→(32,32)→(0,32)
    // [NEW M4-A2] 逻辑域扫描：每个逻辑像素采纹理后展开为设备块
    //   [logicalToDevice(lx), logicalToDevice(lx+1))——像素完美（无逐设备像素反查抖动），
    //   且 scale=1 时块=1 像素、计算与原始逐像素路径完全一致（逐字节等价）。
    static constexpr float kTexU[4] = {0.0f, 32.0f, 32.0f, 0.0f};
    static constexpr float kTexV[4] = {0.0f, 0.0f, 32.0f, 32.0f};

    int clipL = 0;
    int clipT = 0;
    int clipR = dst.width();
    int clipB = dst.height();
    if (useClipRect) {
        getClipRect(clipL, clipT, clipR, clipB); // 逻辑
        clipL = dst.logicalToDevice(clipL);
        clipT = dst.logicalToDevice(clipT);
        clipR = dst.logicalToDevice(clipR);
        clipB = dst.logicalToDevice(clipB);
    }

    int yMin = quad[0][1];
    int yMax = quad[0][1];
    for (int i = 1; i < 4; ++i) {
        yMin = std::min(yMin, static_cast<int>(quad[i][1]));
        yMax = std::max(yMax, static_cast<int>(quad[i][1]));
    }
    // 行扫描按逻辑坐标裁剪（设备 clip → 逻辑）
    const int logClipT = dst.deviceToLogical(clipT);
    const int logClipB = dst.deviceToLogical(clipB);
    const int logClipL = dst.deviceToLogical(clipL);
    const int logClipR = dst.deviceToLogical(clipR);
    yMin = std::max(yMin, logClipT);
    yMax = std::min(yMax, logClipB);
    if (yMin > yMax) {
        return;
    }

    uint16_t* base = dst.pixels();
    const int dstW = dst.width();
    for (int y = yMin; y <= yMax; ++y) {
        float xL = 1e9f;
        float xR = -1e9f;
        float uL = 0.0f;
        float vL = 0.0f;
        float uR = 0.0f;
        float vR = 0.0f;
        const float yf = static_cast<float>(y);
        for (int e = 0; e < 4; ++e) {
            const int a = e;
            const int b = (e + 1) & 3;
            const float y0 = static_cast<float>(quad[a][1]);
            const float y1 = static_cast<float>(quad[b][1]);
            if (y0 == y1) {
                continue;
            }
            const float lo = std::min(y0, y1);
            const float hi = std::max(y0, y1);
            if (yf < lo || yf >= hi) {
                continue;
            }
            const float t = (yf - y0) / (y1 - y0);
            const float x = static_cast<float>(quad[a][0]) + t * (quad[b][0] - quad[a][0]);
            const float u = kTexU[a] + t * (kTexU[b] - kTexU[a]);
            const float v = kTexV[a] + t * (kTexV[b] - kTexV[a]);
            if (x < xL) {
                xL = x;
                uL = u;
                vL = v;
            }
            if (x > xR) {
                xR = x;
                uR = u;
                vR = v;
            }
        }
        if (xL > xR) {
            continue;
        }
        const int xStart = std::max(static_cast<int>(std::ceil(xL)), logClipL);
        const int xEnd = std::min(static_cast<int>(std::floor(xR)), logClipR - 1);
        if (xStart > xEnd) {
            continue;
        }
        // 目标设备块行（逻辑 y → 设备 [y0, y1)）
        const int dy0 = std::max(dst.deviceY(y), clipT);
        const int dy1 = std::min(dst.deviceY(y + 1), clipB);
        if (dy0 >= dy1) {
            continue;
        }
        const float dx = xR - xL;
        const float du = (dx > 1e-6f) ? (uR - uL) / dx : 0.0f;
        const float dv = (dx > 1e-6f) ? (vR - vL) / dx : 0.0f;
        float tu = uL + du * (static_cast<float>(xStart) - xL);
        float tv = vL + dv * (static_cast<float>(xStart) - xL);
        for (int x = xStart; x <= xEnd; ++x) {
            int iu = static_cast<int>(tu);
            int iv = static_cast<int>(tv);
            if (iu < 0) {
                iu = 0;
            } else if (iu > 31) {
                iu = 31;
            }
            if (iv < 0) {
                iv = 0;
            } else if (iv > 31) {
                iv = 31;
            }
            const uint16_t col = palette[tile[iv * 32 + iu]];
            // 设备块填充（逻辑 x → 设备 [x0, x1)）
            const int dxb0 = std::max(dst.deviceX(x), clipL);
            const int dxb1 = std::min(dst.deviceX(x + 1), clipR);
            if (dxb0 < dxb1) {
                for (int yy = dy0; yy < dy1; ++yy) {
                    uint16_t* row = base + static_cast<size_t>(yy) * dstW;
                    for (int xx = dxb0; xx < dxb1; ++xx) {
                        row[xx] = col;
                    }
                }
            }
            tu += du;
            tv += dv;
        }
    }
}

} // namespace rich4
