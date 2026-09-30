#include "game/render/ui_image.h"

#include <cstring>
#include <utility>

namespace rich4 {

namespace {

constexpr uint32_t kMagicSmp = 0x00504D53u; // 'SMP\0'
constexpr uint32_t kMagicSpr = 0x00525053u; // 'SPR\0'
constexpr uint32_t kPaletteSize = 512;      // 256 x RGB555

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool UiImage::load(std::vector<uint8_t> data) {
    m_data = std::move(data);
    m_frames.clear();
    m_frameBytes.clear();
    m_frameSizes.clear();
    m_palette = nullptr;
    m_isSprite = false;

    if (m_data.size() < 12) {
        return false;
    }
    const uint32_t magic = readU32(m_data.data());
    const bool isSmp = magic == kMagicSmp;
    const bool isSpr = magic == kMagicSpr;
    if (!isSmp && !isSpr) {
        return false;
    }
    m_isSprite = isSpr;

    const uint32_t count = readU32(m_data.data() + 4);
    const uint32_t dataOffset = readU32(m_data.data() + 8);
    if (count == 0 || count > 4096 || 12 + static_cast<size_t>(count) * 12 > m_data.size()) {
        return false;
    }
    const size_t pixelBase = static_cast<size_t>(dataOffset) + (isSpr ? kPaletteSize : 0);
    if (pixelBase > m_data.size()) {
        return false;
    }
    if (isSpr) {
        // [RE 0x45663E] 调色板 = 资源基址 + data_offset（256 x RGB555）
        m_palette = reinterpret_cast<const uint16_t*>(m_data.data() + dataOffset);
        std::memcpy(m_basePalette, m_palette, sizeof(m_basePalette));
    }

    // [RE 0x450069] 依据: 帧 0 指针 = 基址 + data_offset (+512 SPR 调色板);
    // 后续帧指针 = 前一帧指针 + 前一帧 size（像素数据顺序存放）
    const uint8_t* pixels = m_data.data() + pixelBase;
    m_frames.reserve(count);
    m_frameBytes.reserve(count);
    m_frameSizes.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        UiFrameHeader header{};
        std::memcpy(&header, m_data.data() + 12 + static_cast<size_t>(i) * 12, sizeof(header));
        UiFrameView view;
        view.width = header.width;
        view.height = header.height;
        view.offsetX = header.offsetX;
        view.offsetY = header.offsetY;
        view.pixels = reinterpret_cast<const uint16_t*>(pixels);
        m_frames.push_back(view);
        m_frameBytes.push_back(pixels);
        m_frameSizes.push_back(header.size);
        pixels += header.size;
    }
    return true;
}

bool UiImage::blitIntoFrame(int dstFrame, const UiFrameView& src, int x, int y) {
    // [RE 0x4562A5] blitElementToCanvas(dstDesc, srcDesc, x, y)
    //   = blitElement(dstW, dstH, dstPixels, src, x, y, 0)：目标坐标先减 src 帧 offset，
    //   逐像素色键（0 跳过）。SMP→SMP 16bit。
    if (m_isSprite || dstFrame < 0 || dstFrame >= static_cast<int>(m_frames.size())) {
        return false;
    }
    const UiFrameView& d = m_frames[static_cast<size_t>(dstFrame)];
    const int dx = x - src.offsetX;
    const int dy = y - src.offsetY;
    uint16_t* dst = reinterpret_cast<uint16_t*>(
        const_cast<uint8_t*>(m_frameBytes[static_cast<size_t>(dstFrame)]));
    for (int r = 0; r < src.height; ++r) {
        const int ry = dy + r;
        if (ry < 0 || ry >= d.height) {
            continue;
        }
        const uint16_t* srow = src.pixels + static_cast<size_t>(r) * src.width;
        uint16_t* drow = dst + static_cast<size_t>(ry) * d.width;
        for (int c = 0; c < src.width; ++c) {
            const int rx = dx + c;
            if (rx < 0 || rx >= d.width) {
                continue;
            }
            const uint16_t p = srow[c];
            if (p != 0) {
                drow[rx] = p;
            }
        }
    }
    return true;
}

void UiImage::setPaletteEntry(int index, uint16_t color) const {
    // [RE 0x40986A] 图块绘制前替换调色板项（索引 255 = 拥有者颜色/黑色轮廓）
    if (!m_isSprite || !m_palette || index < 0 || index >= 256) {
        return;
    }
    const_cast<uint16_t*>(m_palette)[index] = color;
}

uint16_t UiImage::basePaletteEntry(int index) const {
    if (!m_isSprite || index < 0 || index >= 256) {
        return 0;
    }
    return m_basePalette[index];
}

} // namespace rich4
