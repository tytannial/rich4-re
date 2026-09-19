#include "game/app/game_loop.h"

#include "game/app/event_stack.h"
#include "game/app/game_panel.h"
#include "game/app/map_render.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/surface.h"

namespace rich4 {

namespace {

// [RE 0x417E26] gameWndProc 的游戏内绘制（WM_PAINT 分支）
// sub_415D31 顶部工具条 + sub_415E70 地图区 + 按 byte_49715D 布局的右侧面板
// 迁移: 原版 dword_475110 脏区标志 + InvalidateRect → 阶段 1 每 tick 全量重绘
void renderGameFrame(Application& app) {
    renderMap(app, -1, 0);
    renderGamePanel(app);
}

// [RE 0x417E26] gameEventHandler
// 依据: 0x417E26 反编译; WM_PAINT 绘制面板, WM_MOUSEMOVE 地图坐标换算
//       （×761856>>16 ≈ 11.625，限幅 220..2084）与小地图拖拽,
//       WM_LBUTTONDOWN 地图点击/玩家移动, WM_USER+1 绘制玩家信息面板
// 差异: 地图交互（拖拽/点击/工具条）阶段 2 接入；当前每 tick 全量重绘 + ESC 退出
bool gameEventHandler(const SDL_Event* event, void* user) {
    Application& app = *static_cast<Application*>(user);
    if (!event) {
        // 模态进入（原版 PostMessage(0x401)）
        renderGameFrame(app);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        // 原版 WinMain idle loop 的 sub_4192F7（脏区刷新）
        renderGameFrame(app);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        // TODO(RE 0x417E26): 游戏内系统菜单（原版 ESC 打开系统菜单而非直接退出）
        app.events().requestExit(0);
        return true;
    }
    return false;
}

} // namespace

void enterGameLoop(Application& app) {
    // [RE 0x401981] 依据: sub_401981(0) 压入 sub_417E26 并 PostMessage(0x401) 进入游戏内循环
    RICH4_LOGI("enter game loop (RE 0x401981), ESC to exit");
    runModal(app, &gameEventHandler, &app, 16);
}

} // namespace rich4
