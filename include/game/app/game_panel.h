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

} // namespace rich4
