#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

struct SprFrame {
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint32_t size = 0; // width * height
};

// SPR 8bit 调色板精灵，详见 docs/formats/spr.md。
// 注意：Spr 仅引用传入的 data，调用方需保证其生命周期。
class Spr {
public:
    bool parse(const std::vector<uint8_t>& data);

    size_t frameCount() const { return m_frames.size(); }
    const SprFrame& frame(size_t index) const { return m_frames[index]; }
    uint16_t paletteEntry(int index) const;

    // 解码为 RGBA8（像素索引 0 透明）。
    std::vector<uint8_t> frameRgba(size_t index, bool rgb565 = false) const;

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    uint32_t m_dataOffset = 0;
    std::vector<SprFrame> m_frames;
};

} // namespace rich4
