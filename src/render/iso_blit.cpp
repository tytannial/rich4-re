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
    static constexpr float kTexU[4] = {0.0f, 32.0f, 32.0f, 0.0f};
    static constexpr float kTexV[4] = {0.0f, 0.0f, 32.0f, 32.0f};

    int clipL = 0;
    int clipT = 0;
    int clipR = dst.width();
    int clipB = dst.height();
    if (useClipRect) {
        getClipRect(clipL, clipT, clipR, clipB);
    }

    int yMin = quad[0][1];
    int yMax = quad[0][1];
    for (int i = 1; i < 4; ++i) {
        yMin = std::min(yMin, static_cast<int>(quad[i][1]));
        yMax = std::max(yMax, static_cast<int>(quad[i][1]));
    }
    yMin = std::max(yMin, clipT);
    yMax = std::min(yMax, clipB - 1);
    if (yMin > yMax) {
        return;
    }

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
        const int xStart = std::max(static_cast<int>(std::ceil(xL)), clipL);
        const int xEnd = std::min(static_cast<int>(std::floor(xR)), clipR - 1);
        if (xStart > xEnd) {
            continue;
        }
        uint16_t* row = dst.pixels() + static_cast<size_t>(y) * dst.width();
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
            row[x] = palette[tile[iv * 32 + iu]];
            tu += du;
            tv += dv;
        }
    }
}

} // namespace rich4
