#pragma once

namespace rich4 {

class Application;

// [RE 0x439BFA] 月初结息（advanceDay 新月 1 号调用）
//   列表 sub_439BFA（panel.mkf[25]：行图 11+i + 头像 47+3*charIndex + 存款/利息）
//   + 排行演出 sub_437E61（消息 #0092/#0093 → 全员存款 ×1.1 发息（贷款者不发）→
//   悲情人物（#0095..）与资产冠军（#0109/#0110..）演出 → #0122 → 退出）。
//   退出后按存活玩家清零 byte66/monthSettleA/monthSettleB（原版 sub_439BFA 尾）。
// 依据: 0x439BFA/0x437E61 反编译；dbl_464E88=1.1；详见 docs/reverse/functions/bank-system.md §5。
// 参数: app（游戏状态/音频/事件栈）
void monthlySettle(Application& app);

} // namespace rich4
