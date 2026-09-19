#include "game/resource/smp.h"

#include <cstring>

namespace rich4 {

namespace {
uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void expand(uint16_t v, bool rgb565, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (rgb565) {
        r = static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31);
        g = static_cast<uint8_t>(((v >> 5) & 0x3F) * 255 / 63);
        b = static_cast<uint8_t>((v & 0x1F) * 255 / 31);
    } else {
        r = static_cast<uint8_t>(((v >> 10) & 0x1F) * 255 / 31);
        g = static_cast<uint8_t>(((v >> 5) & 0x1F) * 255 / 31);
        b = static_cast<uint8_t>((v & 0x1F) * 255 / 31);
    }
}
} // namespace

bool Smp::parse(const std::vector<uint8_t>& data) {
    if (data.size() < 12 || std::memcmp(data.data(), "SMP", 3) != 0) {
        return false;
    }
    m_data = data.data();
    m_size = data.size();
    const uint32_t count = readU32(data.data() + 4);
    m_dataOffset = readU32(data.data() + 8);
    m_frames.clear();
    m_frames.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t off = 12 + static_cast<size_t>(i) * 12;
        if (off + 12 > data.size()) {
            break;
        }
        const uint8_t* p = data.data() + off;
        SmpFrame f;
        f.width = readU16(p);
        f.height = readU16(p + 2);
        f.x = readU16(p + 4);
        f.y = readU16(p + 6);
        f.size = readU32(p + 8);
        m_frames.push_back(f);
    }
    return true;
}

std::vector<uint8_t> Smp::frameRgba(size_t index, bool rgb565) const {
    if (index >= m_frames.size()) {
        return {};
    }
    const SmpFrame& f = m_frames[index];
    std::vector<uint8_t> rgba(static_cast<size_t>(f.width) * f.height * 4, 0);

    size_t pixelStart = m_dataOffset;
    for (size_t i = 0; i < index; ++i) {
        pixelStart += m_frames[i].size;
    }

    const size_t count = static_cast<size_t>(f.width) * f.height;
    for (size_t i = 0; i < count; ++i) {
        const size_t src = pixelStart + i * 2;
        if (src + 2 > m_size) {
            break;
        }
        const uint16_t v = readU16(m_data + src);
        if (v == 0) {
            continue;
        }
        uint8_t r, g, b;
        expand(v, rgb565, r, g, b);
        const size_t dst = i * 4;
        rgba[dst + 0] = r;
        rgba[dst + 1] = g;
        rgba[dst + 2] = b;
        rgba[dst + 3] = 255;
    }
    return rgba;
}

} // namespace rich4
