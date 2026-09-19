#pragma once

#include "game/game_state.h"

namespace rich4 {

class Application;

// [RE 0x404E44] newGameDialog（定义见 src/app/new_game_dialog.cpp）
// 依据: 0x404E44 反编译（4773B 三段状态机, g_newGameState 0=选择/1=随机补齐AI/2=进入过渡）;
//       由 0x406DE7 newGameInit 经 runModal 调用; 返回 1=确认 / 0=取消
// restorePrevious 对应原版 lParam != 0（保留上次配置 + 恢复玩家 0 角色 + 10 tick 后自动確定）
// 确认时把界面选择结果写回 config
bool newGameDialog(Application& app, bool restorePrevious, NewGameConfig& config);

} // namespace rich4
