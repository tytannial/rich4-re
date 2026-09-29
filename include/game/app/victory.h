#pragma once

namespace rich4 {

class Application;

// [RE 0x41D89E] checkVictory：过天胜利判定（时间上限 g_gameDaysLimit / 胜利资金 g_winMoney）。
//   取存活玩家中 playerTotalAssets(0x4239B9) 最高者；命中 (天数上限到 且 有资产) ∨ (资产达标) →
//   赢家胜利大笑 kMoneyLines[charIndex][21] expr3、其余玩家 alive=0；
//   单人人类赢 → 其余 AI 角色 newGameConfig.aiUsed=2 + sceneRequest=2；单人 AI 赢 →
//   sceneRequest=defeatFlow；多人人类赢 → sceneRequest=3；多人 AI 赢 → sceneRequest=1。
//   返回 1=结束（已置 sceneRequest）、0=继续（无限模式或未达阈值）。
// 依据: 0x41D89E 反编译; 调用点 advanceDay 0x41CFB9（++g_dayCount 之后、每日处理之前）。
int checkVictory(Application& app);

// [RE 0x407842] defeatFlow：AI 赢(玩家输)的失败界面。runModal(defeatWndProc 0x406B14, 1000ms)
//   10 秒倒计时：确认（確定键 / 左键抬起）→ 失败鼓励台词 kMoneyLines[charIndex][23] expr3、
//   清其余玩家 aiUsed、alive[0]=1、返回 4（读档继续）；超时 → 返回 1（回主菜单）。
//   playExitFlc=true 对应原版 lpBuffer==0（破产 defeatFlow(0)）额外播 FLC556 失败演出。
// 依据: 0x407842 反编译。
int defeatFlow(Application& app, bool playExitFlc);

// [RE 0x4075C1] gameClearFlow：通关结算。标记 clearedMaps[mapIndex]=1；
//   4 图全通 → 原版 END/THANKS/OVER.AVI（Indeo5，A5 决策不实现）降级为 quitGame + TODO；
//   否则选下一张地图（mapSelectWndProc 0x4060E9 交互面板属视觉精修批，功能版自动选下一未通关图）。
//   选定图写入 mapIndex/newGameConfig.mapIndex，供 run LABEL_6 续局 newGameInit(1) 使用。
void gameClearFlow(Application& app);

} // namespace rich4
