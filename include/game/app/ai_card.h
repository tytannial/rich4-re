#pragma once

namespace rich4 {

class Application;

// [RE 0x41E69E] aiCardSelect：AI/托管玩家用卡前的"是否用 + 选目标"判定。
// 依据: 0x41E69E（kCardAiPersona[卡] - aiPersonality[cur] >= 2 → 不用；== 1 时 1/3 概率）
//   → dword_475324[卡] 目标选择函数（逐函数逆向，见 441baa-card-effects.md §AI）。
// 输出: 命中返回 1，并写 st.aiCardTarget（玩家掩码 0x8000|mask / 地块 objId / 股票 0-based
//   索引 / 改建设施号）与 st.aiCardTarget2（抢夺卡目标卡 id）；未命中返回 0。
// 原版换屋/转向/被动卡(18..21) 的表项为 return 0（AI 不主动使用）。
int aiCardSelect(Application& app, int cardId);

} // namespace rich4
