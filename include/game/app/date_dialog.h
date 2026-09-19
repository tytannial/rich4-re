#pragma once

#include <cstdint>

namespace rich4 {

class Application;

// [RE 0x4119E3] dateDialog（日期更改界面）
// 依据: 0x4119E3 中 _dos_getdate + runModal(sub_410AC3)；确认后 dword_48BB50/dword_497160 = 日期
// 日期打包: BYTE0 = 日, BYTE1 = 月, HIWORD = 年（对应 dword_48BB84）
// 返回新日期；取消返回 0（原版 -1 语义）
uint32_t dateDialog(Application& app, uint32_t currentDate);

} // namespace rich4
