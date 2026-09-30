#pragma once

#include <cstdint>

namespace rich4 {

// [NEW] cp950 双字节 → Unicode 映射表（生成物，见 tools/gen_big5_tables.py，勿手改）
// 依据: 原版 Windows 经 MultiByteToWideChar(950) 转码；非 Windows 无该 API →
//       内嵌表统一两平台（docs/cross-platform.md）
constexpr int kBig5TableLeadBase = 0x81;
constexpr int kBig5TableLeadCount = 0xFE - 0x81 + 1; // 126

// 索引 = (lead - 0x81) * 256 + tail；0 = 未映射
extern const uint16_t kBig5ToUnicode[kBig5TableLeadCount * 256];

} // namespace rich4
