#pragma once

namespace rich4 {

class Application;

// [RE 0x440CAC] showMessage(text, ms)：居中显示文本 ms 毫秒（阻塞式）
// 依据: 0x440CAC 反编译; setTextFont(16, 0xF0F0F0, 0x101010, 3, 1) →
//       g_tipFrame+72（帧 5）背景框于 stru_48BDB8 + drawText(..., 4) →
//       sub_4528B9(ms) 延时 → sub_451EDB 清除
// 迁移: 原版 Win32 阻塞延时 → runModal + 16ms tick 定时自动退出（期间不响应输入）
void showMessage(Application& app, const char* text, int ms = 1500);

} // namespace rich4
