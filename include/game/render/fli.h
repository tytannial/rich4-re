#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

// [RE 0x450CED / 0x450F04 / 0x45144F] FLC 帧动画解码器
// 依据: 0x450CED 头解析（magic 0xAF12，宽高/帧数/速度）；0x450F04 逐帧 chunk 解码
//       （do-while 跳过 0xF100 占位首帧，定位 0xF1FA 帧）；0x45144F 播放循环。
//       data.mkf[charIndex+559] 跳伞动画为 FLC。详见 docs/formats/flc.md。
// 重写: 仅支持 FLC(0xAF12)，按原版处理器实现 5 种 chunk——0x04 调色板 delta(0x450555)、
//       0x07 全帧 BRUN(0x45059A)、0x0C 局部 BRUN(0x450894)、0x0F 全帧 RLE(0x450A9D)、
//       0x10 全帧字面(0x450C35)，未知 chunk 忽略。调色板为 8bit RGB 直接值（非 6bit）。
//       输出 RGB555（与原版 dword_48A08C 表面格式一致）。
class FliDecoder {
public:
    bool open(std::vector<uint8_t> data);
    // 解码下一帧（到达末帧后回绕到第 0 帧）；返回 false 表示失败
    bool nextFrame();
    // 重置到第 0 帧
    void rewind();

    int width() const { return m_width; }
    int height() const { return m_height; }
    int frameCount() const { return m_frameCount; }
    int frameIndex() const { return m_frame; }
    int speedMs() const { return m_speed; }
    const uint16_t* pixels() const { return m_pixels.data(); }
    bool valid() const { return m_valid; }
    // [RE 0x4506C7] 透明模式（sub_45144F flags&1）色键 = 调色板索引 0（原版解码时该索引保留目标像素）
    uint16_t colorKey() const { return paletteColor(0); }

private:
    bool decodeChunk(const uint8_t* p, int size);
    void setPixel(int x, int y, uint8_t idx);
    uint16_t paletteColor(uint8_t idx) const;

    std::vector<uint8_t> m_data;
    std::vector<uint16_t> m_pixels; // RGB555
    std::vector<uint8_t> m_palette; // 256*3 RGB
    int m_width = 0;
    int m_height = 0;
    int m_frameCount = 0;
    int m_frame = 0;
    int m_speed = 100;
    int m_nextFrameOffset = 0;
    int m_frameStartOffset = 0;
    bool m_valid = false;
};

} // namespace rich4
