#pragma once

namespace rich4 {

class Application;

// [RE 0x4379C9] 路过银行（landingEvent case 14 / onPlayerActionPhase）：
//   拒绝往来中 → "銀行坦絕往來 還剩%d天！"；人类 → 柜员机 UI（panel.mkf[24]，存/取款）；
//   AI → 按 aiCashPct 调整现金/存款比例（月初 1-7 日 ×1.5、月末 26-31 日 ×0.5）
void bankVisitDialog(Application& app);

// [RE 0x436B0A] 週轉金结算：找银行经营者（specPt costType==7 且 owner!=0）——
//   one!=0：经营者週轉欠款 > 其他玩家存款总额 → 经营者垫付（"銀行資金準備不足…由經營者%s墊"）
//   one==0：全员週轉欠款催缴（"銀行經營權易主！"）—— 供取款/停留/跳伞入场调用
void bankAdvanceSettle(Application& app, int one);

} // namespace rich4
