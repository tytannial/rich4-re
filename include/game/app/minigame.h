#pragma once

namespace rich4 {

class Application;

// [RE 0x415215/0x4154DC/0x4155FC] miniGameVisit：特殊地点落地三种小游戏
//   （landingEvent case 6/7/8 = 企鵝挖寶/七彩氣球/喜從天降；调用点 0x41B146/0x41B15E/
//   0x41B16C，返回 ax → g_playerPoints[104*cur] += ax，0x41B152）：
//   原版门控 `g_playerAlive[cur]==1 && byte_497159`（=settings[1] 动画过程）：
//   人类+动画开 → runModal 交互小游戏（三游戏均已接入：miniGameDigRun/
//   miniGameBalloonsRun/miniGameMoneyRun，见 src/app/minigame_dialog.cpp）；
//   否则 → 共享 else 0x415457：score = rand()%20+50（50..69）→
//   showMessage("得點券%d點", 2000)（0x463797，cp950 复核）→ 台词 sub_44EF41(p,0,
//   off_48084A[char*27 + (rand&1)] = kValueLines[char][rand&1]）。
//   资源加载失败时人类也回落 else。
// 差异: 四大恶人（事件槽 4..7）按项目决策直接忽略（原版 NPC 亦随机得分）。
void miniGameVisit(Application& app, int cellType);

// [RE 0x4135F4/0x41364D] 喜從天降（case 8）娃娃跟随鼠标的单步判定（纯函数，供 L0 单测锁定）。
// 原版语义（moneyTick 0x413248）: diff = dollX - cursorX；
//   |diff| <= 8 → 不动（**dir 不清零**、行走帧不推进 = 原版 quirk）；
//   diff > 8 → dir=1 且 dollX -= 10；diff < -8 → dir=2 且 dollX += 10；移动时行走帧 +1 回绕。
// 返回新的 dollX；dir/walk 就地更新。
// [PORT Win32:GetCursorPos] 原版全屏 640x480 下光标坐标即游戏逻辑坐标；窗口化后调用方
//   必须先把光标换算为逻辑坐标并夹取到 0..639（原版 SetCursorPos 使光标不可能越界）。
int moneyDollFollowStep(int dollX, int cursorLogicalX, int& dir, int& walk, int walkHalf);

} // namespace rich4
