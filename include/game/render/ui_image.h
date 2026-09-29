#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

// SMP/SPR 帧头（资源内 12 字节）。
// 依据: docs/formats/smp.md / spr.md; 0x455C52 等 blit 按 [w,h,offX,offY,size] 访问
struct UiFrameHeader {
    uint16_t width;
    uint16_t height;
    int16_t offsetX;
    int16_t offsetY;
    uint32_t size; // 像素数据字节数（= w*h*2）
};

// 运行时帧视图：帧头 + 像素指针。
// [RE 0x450069] sub_450069 指针化后的等价结构
struct UiFrameView {
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t offsetX = 0;
    int16_t offsetY = 0;
    const uint16_t* pixels = nullptr;
};

// SMP/SPR 图像资源（对应原版 sub_450441 加载 + sub_450069 指针化的资源内存）。
class UiImage {
public:
    // [RE 0x450069] relocateFrames
    // 依据: SPR 帧 0 像素 = 基址 + data_offset + 512（跳过 256 色 RGB555 调色板）;
    //       SMP 帧 0 像素 = 基址 + data_offset; 后续帧 = 前一帧指针 + 前一帧 size
    bool load(std::vector<uint8_t> data);

    int frameCount() const { return static_cast<int>(m_frames.size()); }
    const UiFrameView& frame(int index) const { return m_frames[static_cast<size_t>(index)]; }
    bool isSprite() const { return m_isSprite; }
    const std::vector<uint8_t>& data() const { return m_data; }

    // [RE 0x4562A5] blitElementToCanvas：把 src 帧色键(0 透明)合成进本图集 dstFrame 像素。
    //   落点 = (x - src.offsetX, y - src.offsetY)（帧锚点语义，同 blitElement a5/a6 减 offset）；
    //   仅 SMP→SMP（16bit RGB555）。通关星星/选中标记预合成用（victory.cpp gameClearFlow）。
    bool blitIntoFrame(int dstFrame, const UiFrameView& src, int x, int y);

    // SPR 调色板（256 x RGB555，位于 data_offset 处）；非 SPR 返回 nullptr
    const uint16_t* palette() const { return m_palette; }
    // [RE 0x40986A] 图块调色板末项（索引 255）= 拥有者颜色：
    //   无主 = 0（黑，形成黑色轮廓）、有主 = 玩家色；-1（evtCell）保持原色
    void setPaletteEntry(int index, uint16_t color) const;
    uint16_t basePaletteEntry(int index) const;
    // 帧像素起始（SPR 为 8bit 索引；SMP 为 16bit，见 frame().pixels）
    const uint8_t* frameBytes(int index) const { return m_frameBytes[static_cast<size_t>(index)]; }
    // 帧像素字节数（SPR = w*h；SMP = w*h*2，即原版帧头 size 字段）
    uint32_t frameByteSize(int index) const { return m_frameSizes[static_cast<size_t>(index)]; }

private:
    std::vector<uint8_t> m_data;
    std::vector<UiFrameView> m_frames;
    std::vector<const uint8_t*> m_frameBytes;
    std::vector<uint32_t> m_frameSizes;
    const uint16_t* m_palette = nullptr;
    uint16_t m_basePalette[256] = {}; // 原始调色板副本（供恢复）
    bool m_isSprite = false;
};

} // namespace rich4
