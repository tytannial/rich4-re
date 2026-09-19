#include "game/render/ui_image.h"

#include <cstring>

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

    // [RE 0x450069] 依据: 帧 0 指针 = 基址 + data_offset (+512 SPR 调色板);
    // 后续帧指针 = 前一帧指针 + 前一帧 size（像素数据顺序存放）
    const uint8_t* pixels = m_data.data() + pixelBase;
    m_frames.reserve(count);
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
        pixels += header.size;
    }
    return true;
}

} // namespace rich4
