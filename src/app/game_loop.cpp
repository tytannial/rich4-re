#include "game/app/game_loop.h"

#include <algorithm>
#include <cstdio>

#include "game/app/ai_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/debug/debug.h"
#include "game/app/debug_keys.h"
#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_panel.h"
#include "game/app/help_dialog.h"
#include "game/app/item_bag_dialog.h"
#include "game/app/load_dialog.h"
#include "game/app/map_dialog.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/new_game.h"
#include "game/app/object_tip.h"
#include "game/app/query_dialog.h"
#include "game/app/save_dialog.h"
#include "game/app/settings_dialog.h"
#include "game/app/stock_market_dialog.h"
#include "game/app/trade_market.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/render/surface.h"

namespace rich4 {

// [RE 0x417E26] 游戏内整帧绘制（定义见本文件末尾；匿名空间内多处调用，需前置声明）
void renderGameFrame(Application& app);

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
        }
    }
}

// [RE 0x45144F / 0x4506C7] 事件 FLC 帧叠加到场景（得點券等；透明模式色键 = 调色板索引 0）
// 依据: 0x45144F flcOpen(res, x, y, flags) flags&1 = g_flcTransparent；透明像素 = 调色板索引 0
//       （0x4506C7 中索引 0 保留目标像素值）
void drawEventFlcFrame(Application& app) {
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int w = st.eventFlc.width();
    const int h = st.eventFlc.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    const uint16_t* src = st.eventFlc.pixels();
    const uint16_t key = st.eventFlc.colorKey();
    uint16_t* d = dst.pixels();
    for (int row = 0; row < h; ++row) {
        const int py = st.eventFlcY + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int px = st.eventFlcX + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            const uint16_t c = src[static_cast<size_t>(row) * w + col];
            if (c == key) {
                continue; // 色键：调色板索引 0 透明
            }
            d[static_cast<size_t>(py) * Surface::kWidth + px] = c;
        }
    }
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

// [RE 0x417D65] sub_417D65 工具条按钮分发（入口 disablePlayerControl + 收尾 sub_40DEFE）
void handleTopBarButton(Application& app, int index) {
    GameState& state = app.gameState();
    // [RE 0x417D65] 入口: setPauseDraw(0); disablePlayerControl();
    disablePlayerControl(app);
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
            // [RE 0x417D65] case 3: loadDialog(1) → 选槽后重载存档（游戏内读档）
            const int slot = loadDialog(app);
            if (slot >= 0) {
                // reloadGameFromSlotData 不重入游戏内循环：现有 gameEventHandler 模态继续渲染新局
                trace::logf("load dialog: slot=%d reload", slot);
                reloadGameFromSlotData(app, slot);
            }
            break;
        }
        case 4:
            // [RE 0x417D65] case 4: sub_404165（存檔）
            saveDialog(app);
            break;
        case 5:
            // [RE 0x417D65] case 5: sub_40A9BD（大地图）
            mapDialog(app);
            break;
        case 6:
            // [RE 0x424492] case 6: queryDialog（查詢面板：資產/地產/股票清單，[HELP 16]）
            queryDialog(app);
            break;
        case 7:
            // [RE 0x447D97] case 7: itemPanelFlow（道具栏）
            itemBagDialog(app);
            break;
        case 8:
            // [RE 0x441BAA] case 8: useCardFlow（卡片栏）
            useCardDialog(app);
            break;
        case 9:
            // [RE 0x4284BE] case 9: 交易市场（公佈欄，[HELP 11]）
            tradeMarketDialog(app);
            break;
        case 10:
            // [RE 0x417D65] case 10: sub_42B58F(0)（股票行情界面）
            stockMarketDialog(app);
            break;
        default:
            // 其余按钮（11+）无功能
            RICH4_LOGI("top bar button %d not implemented (RE 0x417D65)", index);
            break;
    }
    // [RE 0x40DEFE] sub_40DEFE 收尾：当前玩家有状态（住院/入狱/托管/托管等）→
    //   playerActionFlags |= 0x80 触发 beginPlayerTurn 自动跳过回合（道具把自己炸住院
    //   后不再卡在等待操作）；否则无可行动作时恢复控制（[RE 0x4196F1] enablePlayerControl）
    const int cur = state.currentPlayer;
    bool canResume = true;
    if (cur >= 0 && cur < 4 &&
        (state.players[cur].alive != 1 || state.players[cur].stateFlags != 0 ||
         state.players[cur].state37 != 0 || state.players[cur].byte54 != 0)) {
        state.playerActionFlags[cur] |= 0x80;
        canResume = false;
        RICH4_LOGI("topBar finish: player %d has state -> skip turn (RE 0x40DEFE)", cur);
    } else {
        canResume = state.playerActionState[cur] == 0 && state.sceneRequest == 0;
    }
    if (canResume) {
        state.gamePlayerControl = true;
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
    state.objectTipText.clear(); // [RE 0x417559] 左键操作清除物件提示
    // [RE 0x4186BE] 非玩家控制期（AI 回合/掷骰/移动/结算）：仅保留地图区物件提示，
    //   其余点击（工具条/日历/面板）忽略（原版 0x4186BE 在 byte_46CAFD 检查之前）
    if (!state.gamePlayerControl) {
        if (x < 440 && y >= kTopBarH) {
            showObjectTip(app, x, y);
            renderGameFrame(app);
        }
        return;
    }
    // [RE 0x417E26] 前进面板命中：panel[8] 字节控件图，ctrl = 像素值 + 10
    //   （13=GO、11=骰子数、12=拖动）；面板矩形 advancePanelX/Y/W/H
    if (state.gamePlayerControl && !state.panel8Mask.empty()) {
        const int lx = x - state.advancePanelX;
        const int ly = y - state.advancePanelY;
        if (lx >= 0 && ly >= 0 && lx < state.advancePanelW && ly < state.advancePanelH) {
            const int ctrl = static_cast<int>(
                state.panel8Mask[static_cast<size_t>(ly) * state.advancePanelW + lx]) + 10;
            // [RE 0x41820E/0x4182CF/0x41833B] 前进面板控件点击音效（dword_482322）
            app.audio().playEffect(1);
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
        app.audio().playEffect(1); // [RE 0x41867F] 小地图区点击音效（dword_482322）
        renderGameFrame(app);      // 按下态箭头（帧 18/19）
        return;
    }
    // [RE 0x4182FA] 右侧信息面板页签点击（layout≠2；x∈[616,640) y∈[0,280) → tab = y/70）
    if (layout != 2 && x >= 616 && x < 640 && y >= 0 && y < 280 &&
        state.currentPlayer >= 0 && state.currentPlayer < state.playerCount) {
        const int tab = (y / 70) & 3; // [RE 0x41831D] idiv 0x46(70)
        Player& pl = state.players[state.currentPlayer];
        if (pl.panelTab != tab) {
            app.audio().playEffect(1); // [RE 0x41833B] dword_482322
            pl.panelTab = static_cast<uint8_t>(tab);
            renderGameFrame(app);
        }
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
            app.audio().playEffect(1); // [RE 0x4183B7] 日历切换音效（dword_482322）
            state.calendarMode = 0;
            renderGameFrame(app);
            return;
        }
        if (x >= 478 && x <= 504) {
            RICH4_LOGI("calendar click: mode->1 (RE 0x417E26)");
            app.audio().playEffect(1); // [RE 0x4183FF] 日历切换音效（dword_482322）
            state.calendarMode = 1;
            renderGameFrame(app);
            return;
        }
    }
    // 工具条按钮
    if (x < 440 && y < kTopBarH) {
        state.pendingAction = x / 40 + 100;
        app.audio().playEffect(1); // [RE 0x418482] 工具条/页签点击音效（dword_482322）
        return;
    }
    // [RE 0x4186BE] 地图区左键按下 → 物件提示（原版 x<440 && y>40 → sub_417559）
    if (x < 440 && y >= kTopBarH) {
        showObjectTip(app, x, y);
        renderGameFrame(app);
        return;
    }
}

// [RE 0x417E26] WM_LBUTTONUP
void handleLeftButtonUp(Application& app, int x, int y) {
    (void)x;
    (void)y;
    GameState& state = app.gameState();
    // [RE 0x417559] 松开左键 → 物件提示消失
    if (!state.objectTipText.empty()) {
        state.objectTipText.clear();
        renderGameFrame(app);
    }
    // [RE 0x417E26] 结束小地图拖拽（原版无条件 byte_48BE29 = 0）
    state.dragMiniMap = false;
    if (state.pendingAction == 1) {
        // [RE 0x417E26] byte_48BE28==1: dword_499088 = (dir - 1) & 7
        //   仅旋转朝向，不切换视角模式（原版不改 dword_48BE18，视口仍跟随当前玩家）
        state.mapRotation = (state.mapRotation - 1) & 7;
        renderGameFrame(app);
    } else if (state.pendingAction == 2) {
        state.mapRotation = (state.mapRotation + 1) & 7;
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
        if (st.quitGame) {
            // [RE 0x411B46 + 0x401B9C LABEL_27] 结束游戏：saveConfig 后 g_quitGame=1 → 直接退出程序
            RICH4_LOGI("system menu: quit -> exit application (RE 0x411B46/0x401B9C)");
            st.quitGame = false;
            st.sceneRequest = 0;
            app.events().requestExit(0);
            app.quit();
            return true;
        }
        if (st.sceneRequest != 0) {
            // [RE 0x401B9C] 重新游戏（sceneRequest=1）→ 回主菜单（LABEL_5 重置后 mainMenuScene）
            RICH4_LOGI("system menu: scene=%d -> back to main menu (RE 0x401B9C)", st.sceneRequest);
            st.sceneRequest = 0;
            app.events().requestExit(0);
            return true;
        }
        if (st.surrenderRequest) {
            // [RE 0x411AE0] 認輸投降：淘汰当前玩家（演出/资产清算/拍卖）→ 多人类时死神复仇 → 交棒
            st.surrenderRequest = false;
            surrenderPlayer(app);
            if (st.sceneRequest == 0) { // 无人类存活时 sceneRequest=1，交由下一 tick 回主菜单
                nextPlayer(app);
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
        // [RE 0x45144F] 事件 FLC 阻塞播放期间（playEventFlc 循环内 pumpEvents 会再次
        //   收到本 timer）：原版 flcPlay 的 PeekMessage 只取消息不派发游戏逻辑——
        //   游戏状态机与地图渲染由 playEventFlc 自己掌控（preserveScene 模式更需
        //   保持表面为动画开始前画面，防止按传送后的玩家位置重居中）
        if (st.eventFlcActive) {
            return true;
        }
        // [RE 0x401B9C] idle loop: sub_4192F7(渲染) + sub_40D7C4(状态机)
        updateGameState(app);
        updateScroll(app);
        renderGameFrame(app);
        return true;
    }
    // [RE 0x418C55/0x45144F] 跳伞入场 / 事件 FLC（得點券/卡片/入狱住院等阻塞播放）期间禁止交互：
    //   原版 sub_45144F 阻塞播放期间 PeekMessage 只取走消息不派发（窗口过程不处理）
    {
        GameState& st = app.gameState();
        if ((st.pendingSpawnPlayer != 0 || st.parachuteActive || st.eventFlcActive) &&
            event->type != SDL_EVENT_QUIT) {
            return true;
        }
    }
    // [RE 0x4186CB/0x41889D/0x418910/0x418B93] byte_46CAFD==0（AI 回合/掷骰/移动/结算）
    //   期间锁定交互（原版 cmp byte_46CAFD,0 → jz 出口）：鼠标移动/抬起/右键直接消费；
    //   左键按下保留（地图区物件提示 0x4186BE 在 byte_46CAFD 检查之前）；键盘由各分支自检
    {
        GameState& st = app.gameState();
        if (!st.gamePlayerControl && event->type != SDL_EVENT_QUIT &&
            event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_MOUSE_BUTTON_DOWN) {
            return true;
        }
    }
    // [RE 0x401010] ESC 默认绑定"取消指令"（默认键位 bind[5] = 0x1B），由全局键盘钩子处理：
    // 抬起时合成 WM_RBUTTONUP → handleRightButtonUp（取消手动视角，与右键一致）。
    // 游戏内系统菜单仅由工具条"设置"按钮打开（0x417D65 case 1 → settingsDialog(1)）
    // [RE 0x401010] 全局键盘钩子 fn 的游戏内功能键（word_49717C 前進指令 / 49717E 選擇骰子數）
    if (event->type == SDL_EVENT_KEY_DOWN) {
        GameState& st = app.gameState();
        // [NEW] 调试热键（--debug 启用）：Ctrl+1..9 / Ctrl+Shift+1..5 / Ctrl+Shift+字母
        //   （测试辅助，无原版对应；不受 gamePlayerControl 限制，AI 回合/移动中亦可操作）
        //   mod 判定联合事件 mod 字段（支持 --game-key 合成按键）
        const SDL_Keymod dbgMod = static_cast<SDL_Keymod>(SDL_GetModState() | event->key.mod);
        if (st.debugMode && (dbgMod & SDL_KMOD_CTRL)) {
            const bool dbgShift = (dbgMod & SDL_KMOD_SHIFT) != 0;
            int dbgCode = 0;
            if (event->key.key >= SDLK_1 && event->key.key <= SDLK_9) {
                dbgCode = static_cast<int>(event->key.key - SDLK_1) + 1;
            } else if (dbgShift && event->key.key >= SDLK_A && event->key.key <= SDLK_Z) {
                dbgCode = static_cast<int>(event->key.key); // 'a'..'z'
            }
            if (dbgCode != 0 && handleDebugKey(app, dbgCode, dbgShift)) {
                renderGameFrame(app);
                return true;
            }
        }
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

// [RE 0x45144F] 仅叠加当前事件 FLC 帧（playEventFlc preserveScene 用——不重绘地图表面，
//   对应原版 saveBackground + 直接 blit 的"过渡动画"语义：住院 524/入狱 538 播放期间
//   场景保持动画开始前的画面）
void overlayEventFlcFrame(Application& app) {
    drawEventFlcFrame(app);
}

// [RE 0x417E26] 游戏内绘制（WM_PAINT 分支；定义见 gameEventHandler）
// 顶部工具条 + 地图区 + 按 byte_49715D 布局的右侧面板；每 tick 全量重绘
// 公开供阻塞动画流程（收租联动闪烁 0x451985）主动重绘
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
    // [RE 0x45144F] 事件 FLC 叠加（得點券；地图图元之上、面板之下）
    if (state.eventFlcActive && state.eventFlc.valid()) {
        drawEventFlcFrame(app);
    }
    // [RE 0x451985] 收租联动高亮（整条街/连锁店闪烁；地图图元之上、提示框之下）
    drawEstateHighlight(app);
    // [RE 0x417559] 右键物件提示框（地图区之上、面板之下）
    drawObjectTip(app);
    renderGamePanel(app);
}

void enterGameLoop(Application& app) {
    // [RE 0x401981] 依据: sub_401981(0) 压入 sub_417E26 并 PostMessage(0x401) 进入游戏内循环
    RICH4_LOGI("enter game loop (RE 0x401981)");
    // [NEW] named region 一次性登记（docs/testing.md §3.4；仅供脚本 clickr 消费，
    //   不改任何产品命中逻辑——矩形与既有判断同源常量）
    {
        char nm[16];
        for (int i = 0; i < 10; ++i) { // 工具条 [RE 0x417E26 dword_48BDE4 = x/40]
            std::snprintf(nm, sizeof(nm), "top.%d", i);
            debug::registerRegion(nm, i * 40, 0, 40, 40);
        }
        for (int t = 0; t < 4; ++t) { // 右侧面板页签 [RE 0x4182FA tab = y/70, x∈[616,640)]
            std::snprintf(nm, sizeof(nm), "tab.%d", t);
            debug::registerRegion(nm, 616, t * 70, 24, 70);
        }
    }
    // [RE 0x4019C8] 进游戏切可选曲目（sub_454D91(0) = 切下一首），替换选人场景音乐
    app.audio().playNextMusic();
    // [RE 0x452444] 游戏内音乐切换计时器初值（0x33 = 3 天；事件中设 0x33/0x11）
    app.gameState().musicTimer = 0x33;
    initTurnState(app);
    runModal(app, &gameEventHandler, &app, 16);
}

} // namespace rich4
