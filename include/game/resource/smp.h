#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

struct SmpFrame {
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint32_t size = 0; // width * height * 2
};

// SMP 16bit 高彩位图，详见 docs/formats/smp.md。
// 注意：Smp 仅引用传入的 data，调用方需保证其生命周期。
class Smp {
public:
    bool parse(const std::vector<uint8_t>& data);

    size_t frameCount() const { return m_frames.size(); }
    const SmpFrame& frame(size_t index) const { return m_frames[index]; }

    std::vector<uint8_t> frameRgba(size_t index, bool rgb565 = false) const;

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    uint32_t m_dataOffset = 0;
    std::vector<SmpFrame> m_frames;
};

} // namespace rich4
