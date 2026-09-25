#pragma once

namespace rich4 {

class Application;

// [RE 0x440E1A / 0x43FF56] selectPlayerDialog：玩家列表选择对话框（嫁祸卡等惩罚目标选择）。
// 依据: 0x440E1A 反编译（data.mkf[518] 帧 count-2 横排框 + data.mkf[2] 角色头像 +
//       g_tipFrame 帧 5 提示框 @(220,140) + drawText align 4）+ 0x43FF56 回调
//       （hover 音效 + 三层绿色边框 80×78；LBUTTONDOWN → 返回槽对应玩家；右键/Esc → -1）。
// playerIds: 候选玩家索引（0..7）；count: 2..8（资源帧 = count-2）。
// 返回选中玩家索引；取消返回 -1。详见 docs/reverse/functions/44db81-fate-events.md。
int selectPlayerDialog(Application& app, const int* playerIds, int count, const char* prompt);

} // namespace rich4
