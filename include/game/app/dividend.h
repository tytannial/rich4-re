#pragma once

namespace rich4 {

class Application;

// [RE 0x42BA97] 15 号股东大会分红（advanceDay 15 号调用）
//   panel.mkf[76]（1 帧 592x432）：标题"上市公司分紅" + 逐存活玩家名 + 12 家上市公司
//   （stocks[s][1] = specPt 索引）按持股比例分 specPt.fund（int 截断）→ 分配后清零；
//   确认界面 sub_42B3EB（黑屏 + 帧 0 @(24,24)，1s×3 自动退/点击退，音效 Effect.mkf[61]）
//   → 分红入银行，不足由现金补，再不足淘汰玩家。
// 依据: 0x42BA97/0x42B3EB 反编译；详见 docs/reverse/functions/bank-system.md §6。
void dividendMeeting(Application& app);

} // namespace rich4
