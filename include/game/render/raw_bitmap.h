#pragma once

#include <cstdint>
#include <vector>

namespace rich4 {

// [RE 0x450441 / 0x451801] 无头 16bit RGB555 裸位图（RAW）
// 依据: 该类资源无魔数、无自描述头；sub_450441 读入后仅按资源头 data_offset/data_length
//       调 convertPixelFormat 转像素格式，**不走** relocateFrames（区别于 SPR/SMP）。
//       宽高不写在资源内，由调用方按 MKF index 硬编码。详见 docs/formats/raw_bitmap.md。
// 重写: 按已知尺寸族（字节数 → W×H）解析为 RGB555 像素数组（与引擎 surface 同布局）。
struct RawBitmap {
    int width = 0;
    int height = 0;
    const uint16_t* pixels = nullptr; // 行主序，width*height 个 RGB555
    bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

// 按解压后字节数推断尺寸（已知族）；未知尺寸返回 invalid。
RawBitmap decodeRawBitmap(const std::vector<uint8_t>& blob);

} // namespace rich4
