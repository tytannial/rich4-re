#pragma once

namespace rich4 {

class Application;

// [RE 0x411A96] helpDialog（遊戲說明界面）
// 依据: 0x411A96 → sub_44EB39(-1,-1)；help.mkf[0] 界面框架（400x400 居中）;
//       8 个分类目录 + 页面文本（help.mkf[起始+页号]，BIG5 0x00 分隔行）
void helpDialog(Application& app);

} // namespace rich4
