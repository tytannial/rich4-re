#include "game/app/game_loop.h"

#include "game/app/event_stack.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/surface.h"

#include <cstring>

namespace rich4 {

namespace {

// [RE 0x417E26] gameEventHandler（骨架）
// 依据: 0x417E26 反编译; WM_PAINT 绘制面板（sub_415D31/415E70/415F69/416E6D/4166F8/4169BC）,
//       WM_MOUSEMOVE 地图坐标换算（×761856>>16 ≈ 11.625，限幅 220..2084）与小地图拖拽,
//       WM_LBUTTONDOWN 地图点击/玩家移动, WM_USER+1 绘制玩家信息面板
// 差异: 面板绘制与地图交互在后续阶段接入；当前仅保留事件骨架
bool gameEventHandler(const SDL_Event* event, void* user) {
    (void)user;
    if (!event) {
        return true;
    }
    // TODO(RE 0x415D31/0x415E70/0x415F69/0x416E6D/0x4166F8/0x4169BC): 游戏内面板绘制
    // TODO(RE 0x417191/0x417559/0x417C67/0x417D65): 游戏内交互
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        // TODO(RE 0x417E26): 游戏内菜单（原版 ESC 打开系统菜单而非直接退出）
        return false;
    }
    return false;
}

} // namespace

void enterGameLoop(Application& app) {
    // [RE 0x401981] 依据: sub_401981(0) 压入 sub_417E26 并 PostMessage(0x401) 进入游戏内循环
    GameState& state = app.gameState();
    if (state.mapPreview.size() >= 614400) {
        // 显示地图预览（原版地图绘制流程使用 dword_48A354 缓冲）
        std::memcpy(app.surface().pixels(), state.mapPreview.data(), 614400);
    }
    RICH4_LOGI("enter game loop (RE 0x401981), ESC to exit");
    runModal(app, &gameEventHandler, nullptr);
}

} // namespace rich4
