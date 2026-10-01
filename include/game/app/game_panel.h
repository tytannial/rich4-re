#pragma once

#include "game/game_state.h"

namespace rich4 {

class Application;

// [RE 0x4523D5] stockIsHoliday：股市休市日（星期日或命中节日）；封装 isSpecialDate(gameDate)
bool stockIsHoliday(const GameState& st);

// [RE 0x452444] holidayDaily：每日节日处理（advanceDay 调用；插画 FLC + 节日音乐，
//   发卡链留后续）。见 docs/reverse/functions/452444-holiday-system.md
void holidayDaily(Application& app);

// [NEW M4-C2] 区域脏区（对齐原版 dword_475110 五位；renderGamePanel/renderGameFrameWith 参数）：
//   bit0 工具条 / bit1 地图场景层（含提示/GO/演出叠加/高亮）/ bit2 玩家条 / bit3 小地图 /
//   bit4 日历。kDirtyAll = 全量（现有 100+ 处 renderGameFrame 调用点行为不变）。
constexpr uint8_t kDirtyTopBar = 0x01;
constexpr uint8_t kDirtyMap = 0x02;
constexpr uint8_t kDirtyPlayerBar = 0x04;
constexpr uint8_t kDirtyMiniMap = 0x08;
constexpr uint8_t kDirtyCalendar = 0x10;
constexpr uint8_t kDirtyAll = 0x1F;

// [RE 0x417E26] 游戏内面板绘制（WM_PAINT 分支；定义见 game_loop.cpp gameEventHandler）
// sub_415D31（顶部工具条）+ 按 byte_49715D 布局:
//   0 = sub_415F69 + sub_4169BC / 1 = sub_415F69 + sub_416E6D /
//   2 = sub_4166F8 + sub_416E6D + sub_4169BC
// [NEW M4-C2] dirty：区域脏区位（默认 kDirtyAll = 全量，与原实现等价）
void renderGamePanel(Application& app, uint8_t dirty = kDirtyAll);

// [RE 0x41D433] refreshPlayerPanelFor：资金变化后立即重绘指定玩家面板
// 依据: 0x41D433 反编译; a1<=7 且 g_modalDepth<=1（无事件模态）→ 临时切 g_currentPlayer
//   → layout 2 = sub_4166F8 玩家条 / 其他 = sub_415F69 完整面板 → 还原。
// 调用点: transferMoney 0x41D2C6 尾（from==当前玩家）/ addMoney 0x41D3F4 尾。
// 模态内不重绘（事件面板覆盖中，等相位1/退出后的显式重绘）。
void refreshPlayerPanelFor(Application& app, int player);

} // namespace rich4
