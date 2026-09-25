#pragma once

namespace rich4 {

class Application;

// [RE 0x4315CC] 乐透格落地（landingEvent case 9）：
//   人类玩家（alive==1）→ 投注界面 runModal(sub_42F7FC)；AI/托管且 cash > 1000（严格大于）
//   → 随机挑一个未售号自动投注（无界面）。共有数据：lotteryNumbers[36]（g_miscTable36A）
//   + publicFund（dword_499080 奖金池）。
//   投注界面：每注 1000 元入奖金池；一次只能买一注（点号后进入退出流程）。
// 依据: 0x4315CC/0x42F7FC 反编译；详见 docs/reverse/functions/lottery-system.md。
void lotteryVisit(Application& app);

// [RE 0x431712] 15 号乐透开奖（advanceDay 0x41D094，**紧接**分红 dividendMeeting 之后）：
//   号码表全空（无人投注）则直接返回不弹界面；否则 runModal(sub_43010C) 开奖演出：
//   摇号（全玩家注数 ≤10 → 随机 1..36，否则从已售号中随机）→ 中奖：奖金池发给中奖者
//   （addMoney 入现金）+ 清空号码表；无人中奖：奖金池累积到下月（号码保留）。
// 依据: 0x431712/0x43010C 反编译；详见 docs/reverse/functions/lottery-system.md。
void lotteryDrawMeeting(Application& app);

} // namespace rich4
