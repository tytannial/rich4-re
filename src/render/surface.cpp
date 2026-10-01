#include "game/render/surface.h"

#include "game/core/log.h"
#include "game/render/blit.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rich4 {

bool Surface::create(SDL_Renderer* renderer, int width, int height) {
    m_width = width > 0 ? width : kWidth;
    m_height = height > 0 ? height : kHeight;
    m_pixels.assign(static_cast<size_t>(m_width) * m_height, 0);

    // [PORT DDraw:BltFast] 后台表面 → 主表面
    // 替换依据: 0x401543 BltFast(dword_48A0DC, ..., dword_48A0E0) 把绘制结果送上屏幕 →
    //           SDL_UpdateTexture + SDL_RenderTexture；像素格式 RGB555 与原版资源一致
    //           （SDL3 命名为 SDL_PIXELFORMAT_XRGB1555，bit10-14=R / bit5-9=G / bit0-4=B）
    m_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB1555,
                                  SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
    if (!m_texture) {
        RICH4_LOGE("Surface: SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
    // [RE 0x4861B8] 全局裁剪窗口初始化 = 画布全尺寸（原版 640x480 常量）
    setClipRect(0, 0, m_width, m_height);
    return true;
}

void Surface::destroy() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_pixels.clear();
}

void Surface::present(SDL_Renderer* renderer) {
    if (!m_texture) {
        return;
    }
    SDL_UpdateTexture(m_texture, nullptr, m_pixels.data(),
                      m_width * static_cast<int>(sizeof(uint16_t)));
    SDL_RenderTexture(renderer, m_texture, nullptr, nullptr);
}

void Surface::clear() {
    std::memset(m_pixels.data(), 0, m_pixels.size() * sizeof(uint16_t));
}

void Surface::fillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    // [NEW M4-A2] 逻辑矩形 → 设备矩形（scale=1 恒等；相邻区间首尾一致取整）
    if (m_scale != 1.0f) {
        const int x0 = deviceX(x);
        const int y0 = deviceY(y);
        const int x1 = deviceX(x + w);
        const int y1 = deviceY(y + h);
        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
        if (w <= 0 || h <= 0) {
            return;
        }
    } else if (m_originX != 0 || m_originY != 0) {
        // [NEW M4-D] scale == 1 但绘制原点非 0（宽画布模态框架）也需平移
        x += m_originX;
        y += m_originY;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > m_width) {
        w = m_width - x;
    }
    if (y + h > m_height) {
        h = m_height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        uint16_t* dst = m_pixels.data() + static_cast<size_t>(y + row) * m_width + x;
        std::fill(dst, dst + w, color);
    }
}

bool Surface::saveBmp(const char* path) const {
    const int rowSize = (m_width * 3 + 3) & ~3;
    const uint32_t dataSize = static_cast<uint32_t>(rowSize) * m_height;
    std::FILE* fp = std::fopen(path, "wb");
    if (!fp) {
        RICH4_LOGE("Surface::saveBmp: cannot open %s", path);
        return false;
    }
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    const uint32_t fileSize = 54 + dataSize;
    std::memcpy(header + 2, &fileSize, 4);
    const uint32_t dataOffset = 54;
    std::memcpy(header + 10, &dataOffset, 4);
    const uint32_t infoSize = 40;
    std::memcpy(header + 14, &infoSize, 4);
    const int32_t w = m_width;
    const int32_t h = m_height;
    std::memcpy(header + 18, &w, 4);
    std::memcpy(header + 22, &h, 4);
    const uint16_t planes = 1;
    const uint16_t bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &dataSize, 4);
    std::fwrite(header, 1, sizeof(header), fp);

    std::vector<uint8_t> row(static_cast<size_t>(rowSize), 0);
    for (int y = m_height - 1; y >= 0; --y) {
        const uint16_t* src = m_pixels.data() + static_cast<size_t>(y) * m_width;
        for (int x = 0; x < m_width; ++x) {
            const uint16_t p = src[x];
            const uint8_t r = static_cast<uint8_t>(((p >> 10) & 0x1F) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x1F) * 255 / 31);
            const uint8_t b = static_cast<uint8_t>((p & 0x1F) * 255 / 31);
            row[static_cast<size_t>(x) * 3 + 0] = b;
            row[static_cast<size_t>(x) * 3 + 1] = g;
            row[static_cast<size_t>(x) * 3 + 2] = r;
        }
        std::fwrite(row.data(), 1, row.size(), fp);
    }
    std::fclose(fp);
    RICH4_LOGI("screenshot saved: %s", path);
    return true;
}

} // namespace rich4
