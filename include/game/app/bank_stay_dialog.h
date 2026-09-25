#pragma once

namespace rich4 {

class Application;

// [RE 0x436668] bankStayDialog：銀行停留面板（landingEvent case 14 → 路过柜员机后）
//   cellEntId 用于判定自家银行（specPt.owner == currentPlayer+1，0x436726 6000..8000）
//   - 人类：runModal(sub_435062) 面板（panel.mkf[23] 帧 0，申请/偿还贷款 + 週轉現金）
//   - AI：随机贷款 / 提前还款（0x4367AB..0x436953）
//   ★ 自家银行时帘子打开（0x434217 不画帧 1），点击中央区（268,51)-(591,273)
//     进入週轉現金（= 帮助「董事長特別融資」，即董事长预支银行资金）
void bankStayDialog(Application& app, int cellEntId);

// [RE 0x43695E → 0x436034] bankDueDialog：贷款到期催收窗（到期前 3 天，checkLoanDue 调用）
//   panel[23] + panel[2] + 右侧双面板 @(440,0)/@(440,280) + 50ms 柜员动画 +
//   3 条消息（"XXX您好" → "您向銀行借貸的 貸款即將到期。" → "請不要忘記喔！"）
void bankDueDialog(Application& app);

// [RE 0x433B7E] setLoanDate：loanDate 为 0 时设为今天+90 天（逐日跳过特殊日期）。
//   银行贷款与命运「人頭被盜用冒貸」(fateEvent id2) 共用。
void setLoanDate(Application& app, int player);

} // namespace rich4
