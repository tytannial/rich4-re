#pragma once

namespace rich4 {

class Application;

// [RE 0x420E9A] aiItemSelect：AI/托管玩家用道具前的"是否用 + 选目标"判定。
// 依据: 0x420E9A（kItemAiPersona[道具] - aiPersonality >= 2 → 不用；==1 时 1/3 概率）
//   → funcs_420EE6 @ 0x4753A0[13] 目标函数（逐函数逆向，见 420e9a-item-ai.md）。
// 输出: 命中返回 1，并写 st.aiItemTarget（目标格 cellEnt id / 遥控骰子点数 1..6 / 核弹 objId）。
// 原版時光機(10) 目标函数为 return 0（AI 不用）。
int aiItemSelect(Application& app, int itemId);

} // namespace rich4
