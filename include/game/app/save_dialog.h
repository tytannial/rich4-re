#pragma once

namespace rich4 {

class Application;

// [RE 0x404165] saveDialog（存檔界面）
// 依据: 0x404165 → data.mkf[520] 界面 + data.mkf[2] 头像 + runModal(sub_4039C2);
//       点击槽位经 saveDialog/saveGameToSlot 写入 SAVE%d.DAT
// 返回选中槽索引（0-5）；取消返回 -1
int saveDialog(Application& app);

} // namespace rich4
