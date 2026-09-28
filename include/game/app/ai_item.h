#pragma once

#include <cstdint>

namespace rich4 {

class Application;
struct GameState;

// [RE 0x420E9A] aiItemSelect：AI/托管玩家用道具前的"是否用 + 选目标"判定。
// 依据: 0x420E9A（kItemAiPersona[道具] - aiPersonality >= 2 → 不用；==1 时 1/3 概率）
//   → funcs_420EE6 @ 0x4753A0[13] 目标函数（逐函数逆向，见 420e9a-item-ai.md）。
// 输出: 命中返回 1，并写 st.aiItemTarget（目标格 cellEnt id / 遥控骰子点数 1..6 / 核弹 objId）。
// 原版時光機(10) 目标函数为 return 0（AI 不用）。
int aiItemSelect(Application& app, int itemId);

// [RE 0x40B221] 前方 steps 步路径预测（≤8）→ out[]（0=无效）；
//   *hasBranch=true 表示某步多出口随机选择（原版返回位；娃娃/路障/骰子要求无分叉）。
// 卡片链共用（查封 6 格 / 烏龜 3 格候选域，[RE 0x42062B/0x420970]）。
void aiItemPredictPath(const GameState& st, int p, int steps, uint16_t out[8], bool* hasBranch);

// [RE 0x4221C0] aiDiceAdjustPerTurn：AI/托管回合（0x418E70，卡/道具与状态守卫之后、
//   sub_40DD1F 移动之前）骰子数动态调整。机车基数2/汽车基数3；挂道具（cellNo=+64）
//   按寿命（cellTable[槽].life=+4）：汽车 <15→1、>20→3、15..20 保持；机车 <15→1、
//   ≥15 不动作。无道具：前方 5 步路径（aiItemPredictPath）自家/无主 ownFree、他人 other
//   —— ownFree==0∧other>2 → 汽车 2+(rand&1)/机车 2；ownFree≥2∧other≤1 → 1。步行不调整。
void aiDiceAdjustPerTurn(Application& app);

} // namespace rich4
