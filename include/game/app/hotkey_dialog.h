#pragma once

namespace rich4 {

class Application;

// [RE 0x411A86] hotkeyDialog（熱鍵設定界面）
// 依据: 0x411A86 → runModal(sub_411122); 帧 1（328x336）居中;
//       word_48BB10 工作键位（初值 word_497168）; 确定时 saveConfig（0x411F80）
void hotkeyDialog(Application& app);

} // namespace rich4
