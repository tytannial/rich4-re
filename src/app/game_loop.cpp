#include "game/app/game_loop.h"

#include <algorithm>

#include "game/app/event_stack.h"
#include "game/app/game_panel.h"
#include "game/app/help_dialog.h"
#include "game/app/load_dialog.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/settings_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/surface.h"

namespace rich4 {

namespace {

constexpr int kTopBarH = 40;   // 顶部工具条高度
constexpr int kMiniMapW = 200; // 小地图尺寸
constexpr int kMiniMapH = 200;

// [RE 0x417E26] 游戏内绘制（WM_PAINT 分支；定义见 gameEventHandler）
// 顶部工具条 + 地图区 + 按 byte_49715D 布局的右侧面板；每 tick 全量重绘
void renderGameFrame(Application& app) {
    GameState& state = app.gameState();
    if (state.manualView) {
        // [RE 0x48BE18] dword_48BE18 非 0：用 dword_48BE1C/BE20 作为视口中心
        renderMap(app, state.viewTargetX, state.viewTargetY);
    } else {
        // 否则以当前玩家位置为中心
        renderMap(app, -1, 0);
    }
    renderGamePanel(app);
}

int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// [RE 0x417E26] WM_MOUSEMOVE 小地图区域命中（返回小地图内坐标）
// 区域: 布局 1 小地图在 (440,280)；布局 2 在 (440,80)-(640,280)
bool miniMapHitRegion(const GameState& state, int x, int y, int& mx, int& my) {
    const int layout = state.settings[5] & 3;
    const int miniY = kMiniMapY[layout];
    if (miniY == 0) {
        return false;
    }
    if (x <= 440 || x >= 440 + kMiniMapW || y <= miniY || y >= miniY + kMiniMapH) {
        return false;
    }
    mx = x - 440;
    my = y - miniY;
    return true;
}

// [RE 0x417D65] sub_417D65 工具条按钮分发
void handleTopBarButton(Application& app, int index) {
    switch (index) {
        case 0:
            // [RE 0x417D65] case 0: helpDialog(0x14, 60)
            helpDialog(app);
            break;
        case 1:
            // [RE 0x417D65] case 1: settingsDialog(1)
            settingsDialog(app, 1);
            break;
        case 3: {
            // [RE 0x417D65] case 3: loadDialog(1)（游戏内读档阶段 3 接入）
            const int slot = loadDialog(app);
            RICH4_LOGI("in-game load dialog returned slot %d (TODO RE 0x417D65)", slot);
            break;
        }
        default:
            // 2/4-10 依赖游戏内逻辑（存檔/地圖/卡片/道具/股票等）
            RICH4_LOGI("top bar button %d not implemented (RE 0x417D65)", index);
            break;
    }
}

// [RE 0x417E26] WM_MOUSEMOVE
void handleMouseMove(Application& app, int x, int y) {
    GameState& state = app.gameState();
    // 小地图拖拽：视口中心 = (761856 * Δ)>>16，限幅 220..2084
    if (state.dragMiniMap) {
        int mx = 0;
        int my = 0;
        if (!miniMapHitRegion(state, x, y, mx, my)) {
            state.dragMiniMap = false;
            return;
        }
        const int vx = (761856 * mx) >> 16;
        const int vy = (761856 * my) >> 16;
        if (vx != state.viewTargetX || vy != state.viewTargetY) {
            state.viewTargetX = clampInt(vx, 220, 2084);
            state.viewTargetY = clampInt(vy, 220, 2084);
            state.manualView = true;
            renderGameFrame(app);
        }
        return;
    }
    // 工具条悬停高亮：dword_48BDE4 = x/40
    if (x < 440 && y < kTopBarH) {
        const int idx = x / 40;
        if (idx != state.topBarHover) {
            state.topBarHover = idx;
            app.audio().playEffect(0); // [RE 0x4542CE] unk_48231A
            renderGameFrame(app);
        }
    } else if (state.topBarHover != -1) {
        state.topBarHover = -1;
        renderGameFrame(app);
    }
}

// [RE 0x417E26] WM_LBUTTONDOWN
void handleLeftButtonDown(Application& app, int x, int y) {
    GameState& state = app.gameState();
    const int layout = state.settings[5] & 3;
    const int miniY = kMiniMapY[layout];
    // 小地图箭头（旋转）：(443,miniY+3)-(493,miniY+28)，左/右各 25 像素
    if (miniY != 0 && x >= 443 && x <= 493 && y >= miniY + 3 && y <= miniY + 28) {
        state.pendingAction = (x - 443) / 25 + 1; // 1=左旋 / 2=右旋
        return;
    }
    // 小地图拖拽
    int mx = 0;
    int my = 0;
    if (miniMapHitRegion(state, x, y, mx, my)) {
        state.dragMiniMap = true;
        return;
    }
    // 工具条按钮
    if (x < 440 && y < kTopBarH) {
        state.pendingAction = x / 40 + 100;
        return;
    }
    // 地图区（sub_417559 物件信息框阶段 3 接入）
    if (x < 440 && y >= kTopBarH) {
        RICH4_LOGI("map click (%d,%d) TODO sub_417559 (RE 0x417E26)", x, y);
        return;
    }
}

// [RE 0x417E26] WM_LBUTTONUP
void handleLeftButtonUp(Application& app, int x, int y) {
    (void)x;
    (void)y;
    GameState& state = app.gameState();
    if (state.pendingAction == 1) {
        // [RE 0x417E26] byte_48BE28==1: dword_499088 = (dir - 1) & 7
        state.mapRotation = (state.mapRotation - 1) & 7;
        state.manualView = true;
        renderGameFrame(app);
    } else if (state.pendingAction == 2) {
        state.mapRotation = (state.mapRotation + 1) & 7;
        state.manualView = true;
        renderGameFrame(app);
    } else if (state.pendingAction >= 100) {
        handleTopBarButton(app, state.pendingAction - 100);
        renderGameFrame(app);
    }
    state.pendingAction = 0;
}

// [RE 0x417E26] gameEventHandler
// 依据: 0x417E26 反编译; WM_PAINT 绘制面板, WM_MOUSEMOVE 地图坐标换算
//       （×761856>>16 ≈ 11.625，限幅 220..2084）与小地图拖拽,
//       WM_LBUTTONDOWN 地图点击/玩家移动, WM_USER+1 绘制玩家信息面板
// 差异: sub_417559 物件信息框、日历/页签切换阶段 3 接入
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
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        handleMouseMove(app, static_cast<int>(event->motion.x), static_cast<int>(event->motion.y));
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            handleLeftButtonDown(app, static_cast<int>(event->button.x),
                                 static_cast<int>(event->button.y));
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event->button.button == SDL_BUTTON_LEFT) {
            handleLeftButtonUp(app, static_cast<int>(event->button.x),
                               static_cast<int>(event->button.y));
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            // [RE 0x417E26] WM_RBUTTONUP: 结束小地图拖拽
            app.gameState().dragMiniMap = false;
        }
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
