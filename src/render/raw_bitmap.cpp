#include "game/render/raw_bitmap.h"

namespace rich4 {

// 已知 RAW 尺寸族（字节数 → W×H），见 docs/formats/raw_bitmap.md：
//   80000  = 200×200×2  事件/节日卡插画   (Data.mkf 4–127)
//   194776 = 388×251×2  事件/结局大插画   (Data.mkf 441–516)
//   614400 = 640×480×2  全屏背景          (jump.mkf 0–7 / Panel.mkf 92 / Data.mkf 601)
RawBitmap decodeRawBitmap(const std::vector<uint8_t>& blob) {
    RawBitmap r;
    switch (blob.size()) {
        case 80000:
            r.width = 200;
            r.height = 200;
            break;
        case 194776:
            r.width = 388;
            r.height = 251;
            break;
        case 614400:
            r.width = 640;
            r.height = 480;
            break;
        default:
            return r;
    }
    r.pixels = reinterpret_cast<const uint16_t*>(blob.data());
    return r;
}

} // namespace rich4
