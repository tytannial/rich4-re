#pragma once

#include "game/game_state.h"

namespace rich4 {

class Application;

// [RE 0x4523D5] stockIsHoliday：股市休市日（星期日或命中节日）；封装 isSpecialDate(gameDate)
bool stockIsHoliday(const GameState& st);

// [RE 0x417E26] 游戏内面板绘制（WM_PAINT 分支；定义见 game_loop.cpp gameEventHandler）
// sub_415D31（顶部工具条）+ 按 byte_49715D 布局:
//   0 = sub_415F69 + sub_4169BC / 1 = sub_415F69 + sub_416E6D /
//   2 = sub_4166F8 + sub_416E6D + sub_4169BC
void renderGamePanel(Application& app);

// [RE 0x41D433] refreshPlayerPanelFor：资金变化后立即重绘指定玩家面板
// 依据: 0x41D433 反编译; a1<=7 且 g_modalDepth<=1（无事件模态）→ 临时切 g_currentPlayer
//   → layout 2 = sub_4166F8 玩家条 / 其他 = sub_415F69 完整面板 → 还原。
// 调用点: transferMoney 0x41D2C6 尾（from==当前玩家）/ addMoney 0x41D3F4 尾。
// 模态内不重绘（事件面板覆盖中，等相位1/退出后的显式重绘）。
void refreshPlayerPanelFor(Application& app, int player);

} // namespace rich4
