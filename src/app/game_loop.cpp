#include "game/app/game_loop.h"

#include <algorithm>

#include "game/app/ai_dialog.h"
#include "game/app/event_stack.h"
#include "game/app/game_panel.h"
#include "game/app/help_dialog.h"
#include "game/app/load_dialog.h"
#include "game/app/map_dialog.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/save_dialog.h"
#include "game/app/settings_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/surface.h"

namespace rich4 {

namespace {

constexpr int kTopBarH = 40;   // 顶部工具条高度
constexpr int kMiniMapW = 200; // 小地图尺寸
constexpr int kMiniMapH = 200;

// [RE 0x45144F] 跳伞动画帧（FLC）叠加到地图区
// 依据: 0x45144F 播放 data.mkf[charIndex+559] 到 (0,40) 区域
void drawParachuteFrame(Application& app) {
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int w = st.parachute.width();
    const int h = st.parachute.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    const uint16_t* src = st.parachute.pixels();
    uint16_t* d = dst.pixels();
    const int ox = (440 - w) / 2;
    const int oy = 40 + (440 - h) / 2;
    int drawn = 0;
    for (int row = 0; row < h; ++row) {
        const int py = oy + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int px = ox + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            const uint16_t c = src[static_cast<size_t>(row) * w + col];
            if (c == 0) {
                continue; // 色键 0 透明
            }
            d[static_cast<size_t>(py) * Surface::kWidth + px] = c;
            ++drawn;
        }
    }
    RICH4_LOGI("parachute draw: %d px frame=%d (RE 0x45144F)", drawn, st.parachute.frameIndex());
}

// [RE 0x417E26] 游戏内绘制（WM_PAINT 分支；定义见 gameEventHandler）
// 顶部工具条 + 地图区 + 按 byte_49715D 布局的右侧面板；每 tick 全量重绘
void renderGameFrame(Application& app) {
    GameState& state = app.gameState();
    if (state.manualView) {
        // [RE 0x48BE18] dword_48BE18 非 0：用平滑视口中心（原版 dword_48BE1C/BE20）
        renderMap(app, state.viewSmoothX, state.viewSmoothY);
    } else {
        // 否则以当前玩家位置为中心
        renderMap(app, -1, 0);
    }
    if (state.parachuteActive && state.parachute.valid()) {
        drawParachuteFrame(app);
    }
    renderGamePanel(app);
}

// [NEW] 小地图点击平滑跳转：每 tick 向目标视口中心插值（原版直接跳转）
void updateScroll(Application& app) {
    GameState& state = app.gameState();
    if (!state.viewScrolling) {
        return;
    }
    state.viewSmoothX += (state.viewTargetX - state.viewSmoothX) / 4;
    state.viewSmoothY += (state.viewTargetY - state.viewSmoothY) / 4;
    const int dx = state.viewTargetX - state.viewSmoothX;
    const int dy = state.viewTargetY - state.viewSmoothY;
    if (dx > -2 && dx < 2 && dy > -2 && dy < 2) {
        state.viewSmoothX = state.viewTargetX;
        state.viewSmoothY = state.viewTargetY;
        state.viewScrolling = false;
    }
}

// [NEW] 设置平滑跳转目标（从当前视口中心开始插值）
void startScroll(Application& app, int targetX, int targetY) {
    GameState& state = app.gameState();
    if (!state.viewScrolling) {
        state.viewSmoothX = state.viewX;
        state.viewSmoothY = state.viewY;
    }
    state.viewTargetX = targetX;
    state.viewTargetY = targetY;
    state.manualView = true;
    state.viewScrolling = true;
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
        case 2:
            // [RE 0x417D65] case 2: sub_41E345（託管AI / 玩家資訊）
            aiDialog(app);
            break;
        case 3: {
            // [RE 0x417D65] case 3: loadDialog(1)（游戏内读档阶段 3 接入）
            const int slot = loadDialog(app);
            RICH4_LOGI("in-game load dialog returned slot %d (TODO RE 0x417D65)", slot);
            break;
        }
        case 4:
            // [RE 0x417D65] case 4: sub_404165（存檔）
            saveDialog(app);
            break;
        case 5:
            // [RE 0x417D65] case 5: sub_40A9BD（大地圖）
            mapDialog(app);
            break;
        default:
            // 2/4-10 依赖游戏内逻辑（存檔/地圖/卡片/道具/股票等）
            RICH4_LOGI("top bar button %d not implemented (RE 0x417D65)", index);
            break;
    }
}

// [RE 0x417E26] WM_MOUSEMOVE
void handleMouseMove(Application& app, int x, int y) {
    GameState& state = app.gameState();
    // 小地图拖拽：视口中心 = (761856 * Δ)>>16，限幅 220..2084（拖拽立即跟随）
    if (state.dragMiniMap) {
        int mx = 0;
        int my = 0;
        if (!miniMapHitRegion(state, x, y, mx, my)) {
            state.dragMiniMap = false;
            return;
        }
        const int vx = clampInt((761856 * mx) >> 16, 220, 2084);
        const int vy = clampInt((761856 * my) >> 16, 220, 2084);
        if (vx != state.viewTargetX || vy != state.viewTargetY) {
            state.viewTargetX = vx;
            state.viewTargetY = vy;
            state.viewSmoothX = vx;
            state.viewSmoothY = vy;
            state.manualView = true;
            state.viewScrolling = false;
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
    // [RE 0x417E26] 前进面板命中：panel[8] 字节控件图，ctrl = 像素值 + 10
    //   （13=GO、11=骰子数、12=拖动）；面板矩形 advancePanelX/Y/W/H
    if (state.gamePlayerControl && !state.panel8Mask.empty()) {
        const int lx = x - state.advancePanelX;
        const int ly = y - state.advancePanelY;
        if (lx >= 0 && ly >= 0 && lx < state.advancePanelW && ly < state.advancePanelH) {
            const int ctrl = static_cast<int>(
                state.panel8Mask[static_cast<size_t>(ly) * state.advancePanelW + lx]) + 10;
            if (ctrl == 13) { // GO → sub_419703 + sub_41D546 + sub_40DD1F
                state.manualView = false;
                disablePlayerControl(app);
                startPlayerMove(app);
                return;
            }
            if (ctrl == 11) { // 选择骰子数：按点击的骰子图标行设数量
                Player& pl = state.players[state.currentPlayer];
                const int travel = pl.travel & 3;
                if (travel == 1) {
                    for (int i = 0; i < 2; ++i) {
                        if (ly >= 19 * i + 16 && ly <= 19 * i + 32) {
                            pl.diceCount = static_cast<uint8_t>(i + 1);
                        }
                    }
                } else if (travel == 2) {
                    for (int j = 0; j < 3; ++j) {
                        if (ly >= 16 * j + 9 && ly <= 16 * j + 25) {
                            pl.diceCount = static_cast<uint8_t>(j + 1);
                        }
                    }
                }
                return;
            }
        }
    }
    const int layout = state.settings[5] & 3;
    const int miniY = kMiniMapY[layout];
    // 小地图箭头（旋转）：(443,miniY+3)-(493,miniY+28)，左/右各 25 像素
    if (miniY != 0 && x >= 443 && x <= 493 && y >= miniY + 3 && y <= miniY + 28) {
        state.pendingAction = (x - 443) / 25 + 1; // 1=左旋 / 2=右旋
        renderGameFrame(app);                     // 按下态箭头（帧 18/19）
        return;
    }
    // 小地图拖拽 / 点击平滑跳转
    int mx = 0;
    int my = 0;
    if (miniMapHitRegion(state, x, y, mx, my)) {
        state.dragMiniMap = true;
        // [NEW] 点击小地图平滑跳转到该处（原版直接跳转）
        startScroll(app, clampInt((761856 * mx) >> 16, 220, 2084),
                    clampInt((761856 * my) >> 16, 220, 2084));
        renderGameFrame(app);
        return;
    }
    // [RE 0x417E26] 日历点击切换显示模式（byte_497164）：
    //   原版条件 byte_49715D!=1（有日历的布局）且 y∈[288,314]，
    //   左箭头 x∈[448,474]→大数字(0)、右箭头 x∈[478,504]→月历网格(1)
    if (layout != 1 && y >= 288 && y <= 314) {
        if (x >= 448 && x <= 474) {
            RICH4_LOGI("calendar click: mode->0 (RE 0x417E26)");
            state.calendarMode = 0;
            renderGameFrame(app);
            return;
        }
        if (x >= 478 && x <= 504) {
            RICH4_LOGI("calendar click: mode->1 (RE 0x417E26)");
            state.calendarMode = 1;
            renderGameFrame(app);
            return;
        }
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
    // [RE 0x417E26] 结束小地图拖拽（原版无条件 byte_48BE29 = 0）
    state.dragMiniMap = false;
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

// [RE 0x417E26] WM_RBUTTONUP：小地图上右键 → 取消手动视角，回到当前玩家
// 见 0x417E26 反编译; if (dword_48BE18 && 布局 && 鼠标在小地图区) dword_48BE18 = 0
void handleRightButtonUp(Application& app, int x, int y) {
    GameState& state = app.gameState();
    if (!state.manualView) {
        return;
    }
    int mx = 0;
    int my = 0;
    if (!miniMapHitRegion(state, x, y, mx, my)) {
        return;
    }
    state.manualView = false;
    state.viewScrolling = false;
    renderGameFrame(app);
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
        // [RE 0x401B9C] 内层循环检测场景切换/退出请求（游戏内菜单结果）
        GameState& st = app.gameState();
        if (st.quitGame || st.sceneRequest != 0) {
            RICH4_LOGI("system menu: scene=%d quit=%d -> exit game loop (RE 0x401B9C)",
                       st.sceneRequest, st.quitGame ? 1 : 0);
            st.quitGame = false;
            st.sceneRequest = 0;
            app.events().requestExit(0);
            return true;
        }
        if (st.surrenderRequest) {
            // [RE 0x411AE0] 認輸投降：当前玩家出局
            st.surrenderRequest = false;
            if (st.currentPlayer >= 0 && st.currentPlayer < st.playerCount) {
                st.players[st.currentPlayer].alive &= ~1;
            }
            renderGameFrame(app);
            return true;
        }
        // [RE 0x417E26] WM_TIMER(0x113): dword_48BDD4 ^= 1（前进面板 GO 闪烁，约 500ms）
        static int s_blinkTick = 0;
        if (++s_blinkTick >= 31) {
            s_blinkTick = 0;
            st.advancePanelBlink ^= 1;
        }
        // [RE 0x401B9C] idle loop: sub_4192F7(渲染) + sub_40D7C4(状态机)
        updateGameState(app);
        updateScroll(app);
        renderGameFrame(app);
        return true;
    }
    // [RE 0x401010] ESC 默认绑定"取消指令"（默认键位 bind[5] = 0x1B），由全局键盘钩子处理：
    // 抬起时合成 WM_RBUTTONUP → handleRightButtonUp（取消手动视角，与右键一致）。
    // 游戏内系统菜单仅由工具条"设置"按钮打开（0x417D65 case 1 → settingsDialog(1)）
    // [RE 0x401010] 全局键盘钩子 fn 的游戏内功能键（word_49717C 前進指令 / 49717E 選擇骰子數）
    if (event->type == SDL_EVENT_KEY_DOWN) {
        GameState& st = app.gameState();
        const uint8_t vk = sdlKeycodeToVk(event->key.key);
        const uint16_t* bind = app.input().bindings();
        if (st.gamePlayerControl && vk != 0 && vk == (bind[10] & 0xFF)) {
            // [RE 0x401010] word_49717C 前進指令 → setPauseDraw(0); sub_419703(); sub_41D546(); sub_40DD1F()
            RICH4_LOGI("advance key: start move (RE 0x401010/0x40DD1F)");
            disablePlayerControl(app);
            startPlayerMove(app);
            return true;
        }
        if (st.gamePlayerControl && vk != 0 && vk == (bind[11] & 0xFF)) {
            // [RE 0x401010] word_49717E 選擇骰子數（机车 1-2 / 汽车 1-3）
            Player& pl = st.players[st.currentPlayer];
            const int maxDice = (pl.travel == 2) ? 3 : (pl.travel == 1 ? 2 : 1);
            if (maxDice > 1) {
                pl.diceCount = static_cast<uint8_t>(pl.diceCount % maxDice + 1);
            }
            RICH4_LOGI("dice count: %d (RE 0x401010)", pl.diceCount);
            return true;
        }
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
            // [RE 0x417E26] WM_RBUTTONUP: 小地图上右键回到当前玩家视角
            handleRightButtonUp(app, static_cast<int>(event->button.x),
                                static_cast<int>(event->button.y));
        }
        return true;
    }
    return false;
}

} // namespace

void enterGameLoop(Application& app) {
    // [RE 0x401981] 依据: sub_401981(0) 压入 sub_417E26 并 PostMessage(0x401) 进入游戏内循环
    RICH4_LOGI("enter game loop (RE 0x401981)");
    initTurnState(app);
    runModal(app, &gameEventHandler, &app, 16);
}

} // namespace rich4
