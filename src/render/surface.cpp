#include "game/render/surface.h"

#include "game/core/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rich4 {

bool Surface::create(SDL_Renderer* renderer) {
    m_pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);

    // [PORT DDraw:BltFast] 后台表面 → 主表面
    // 替换依据: 0x401543 BltFast(dword_48A0DC, ..., dword_48A0E0) 把绘制结果送上屏幕 →
    //           SDL_UpdateTexture + SDL_RenderTexture；像素格式 RGB555 与原版资源一致
    //           （SDL3 命名为 SDL_PIXELFORMAT_XRGB1555，bit10-14=R / bit5-9=G / bit0-4=B）
    m_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB1555,
                                  SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    if (!m_texture) {
        RICH4_LOGE("Surface: SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
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
    SDL_UpdateTexture(m_texture, nullptr, m_pixels.data(), kWidth * static_cast<int>(sizeof(uint16_t)));
    SDL_RenderTexture(renderer, m_texture, nullptr, nullptr);
}

void Surface::clear() {
    std::memset(m_pixels.data(), 0, m_pixels.size() * sizeof(uint16_t));
}

void Surface::fillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > kWidth) {
        w = kWidth - x;
    }
    if (y + h > kHeight) {
        h = kHeight - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        uint16_t* dst = m_pixels.data() + static_cast<size_t>(y + row) * kWidth + x;
        std::fill(dst, dst + w, color);
    }
}

bool Surface::saveBmp(const char* path) const {
    const int rowSize = (kWidth * 3 + 3) & ~3;
    const uint32_t dataSize = static_cast<uint32_t>(rowSize) * kHeight;
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
    const int32_t w = kWidth;
    const int32_t h = kHeight;
    std::memcpy(header + 18, &w, 4);
    std::memcpy(header + 22, &h, 4);
    const uint16_t planes = 1;
    const uint16_t bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &dataSize, 4);
    std::fwrite(header, 1, sizeof(header), fp);

    std::vector<uint8_t> row(static_cast<size_t>(rowSize), 0);
    for (int y = kHeight - 1; y >= 0; --y) {
        const uint16_t* src = m_pixels.data() + static_cast<size_t>(y) * kWidth;
        for (int x = 0; x < kWidth; ++x) {
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
