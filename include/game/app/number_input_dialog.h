#pragma once

namespace rich4 {

class Application;

// [RE 0x453544 / 0x452C02] numberInputDialog：数字输入框（panel.mkf[21] 背景/按键 +
//   panel.mkf[22] 索引图命中；数字键盘 + 滑块 + 最大/确定/清除/退格）
//   maxValue = 输入上限（clamp）；返回确认值，取消/ESC 返回 -1
int numberInputDialog(Application& app, int maxValue);

} // namespace rich4
