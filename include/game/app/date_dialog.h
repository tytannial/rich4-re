#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x4119E3] dateDialog（日期更改界面）
// 依据: 0x4119E3 中 _dos_getdate + runModal(sub_410AC3)；确认后 dword_48BB50/dword_497160 = 日期
// 日期打包: BYTE0 = 日, BYTE1 = 月, HIWORD = 年（对应 dword_48BB84）
// 返回新日期；取消返回 0（原版 -1 语义）
uint32_t dateDialog(Application& app, uint32_t currentDate);

// [RE 0x411E8F] loadConfig 尾部：_dos_getdate → 钳位年 ∈ [1998,2010]
// 依据: 0x411F21 dos_getdate; 年<0x7CE(1998)→1998-1-1, 年>0x7DA(2010)→2010-1-1，
//       否则系统日期；0x411A7C 尾跳转写 dword_497160（不写 dword_48BB50）
uint32_t clampedSystemDate();

} // namespace rich4
