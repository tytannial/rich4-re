#pragma once

namespace rich4 {

class Application;

// [RE 0x424492 → 0x423CF3] queryDialog：工具条 case 6（sub_417D65）→ 查詢面板
// 依据: 0x424492 反编译; panel.mkf[9]（25 帧：帧 0/1/2 页背景 / 3/4 玩家按钮 /
//       5/6 EXIT 钮 / 7/8 翻页箭头 / 11 子页签底 / 12 页签底 / 13..24 神明图标）
//       + panel.mkf[74]（13 帧道具图标）→ runModal(sub_423CF3)；
//       三页签 資產清單/地產清單/股票清單（off_47540C 0x47540C），
//       地產清單 5 子页签 全 部/住宅區/商業區/房 屋/連鎖店（off_4753D4）。
// 对应帮助 [HELP 16]「查詢：資產清單 / 地產清單 / 股票清單」。
void queryDialog(Application& app);

} // namespace rich4
