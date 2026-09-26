#pragma once

namespace rich4 {

class Application;

// [RE 0x475D5C] g_cardEffectFuncs 卡片效果（P4）。
// 契约（useCardFlow 0x441BAA）：返回非 0 = 结束流程（已消耗）；返回 0 = 重弹面板（不消耗）。
// 消耗由效果函数内部 cardBagRemove 完成。
// AI 分支：原版 sub_41E6F2(0) = dword_48BE58 预选目标（由 AI 用卡链 sub_41E69E 填写，P5）；
//   useCardFlow 仅 alive==1 进入，故当前只实现人类路径。
// 逐卡规格见 docs/reverse/functions/441baa-card-effects.md。

// id1 均富卡 [RE 0x4420D8]：全员现金取均值（超出均值部分 1/100 记债给使用者）
int cardEffectEqualRich(Application& app);
// id2 均貧卡 [RE 0x4421B4]：选一名玩家，两人现金取均（差额 1/100 记债给使用者）
int cardEffectEqualPoor(Application& app);
// id3 購地卡 [RE 0x442325]：强制收购当前所站他人地块（价 = (level*priceBase+priceAdd)*M）
int cardEffectBuyLand(Application& app);
// id6 轉向卡 [RE 0x442F4D]：选一名玩家令其掉头（turnToAdjacentCell 0x40C78C）
int cardEffectTurn(Application& app);
// id8 拍賣卡 [RE 0x443225]：脚下地强制拍卖（卖方=使用者，补偿原主 M*base*(level+2)/5；流标归公）
int cardEffectAuction(Application& app);
// id11 怪獸卡 [RE 0x443917]：选一块地彻底夷平（demolishAtObjId mode2）+ FLC 557
int cardEffectMonster(Application& app);
// id12 拆除卡 [RE 0x443B0F]：选目标降一级 / 拆连锁 / 拆路面道具 + FLC 529
int cardEffectDemolish(Application& app);
// id13 搶奪卡 [RE 0x443E3D]：选一名玩家夺取其一张卡或道具（记债卡价/道具价）
int cardEffectSteal(Application& app);

} // namespace rich4
