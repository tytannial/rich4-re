#include "game/app/victory.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "game/app/economy.h"        // playerTotalAssets [RE 0x4239B9]
#include "game/app/event_stack.h"    // runModal / kModalTimerEvent
#include "game/app/game_loop.h"      // renderGameFrame（sub_41906A(1) 刷新迁移）
#include "game/app/item_lines.h"     // kMoneyLines / playLine
#include "game/app/turn_system.h"    // playEventFlc（破产 FLC556）
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/platform/audio.h"

namespace rich4 {

// [RE 0x41D89E] checkVictory
int checkVictory(Application& app) {
    GameState& st = app.gameState();
    // [RE 0x41D8B4] 天数上限与胜利资金皆为 0（无限模式）时不判定
    if (st.gameDaysLimit == 0 && st.winMoney == 0) {
        return 0;
    }
    // [RE 0x41D8BD..0x41D8E6] 存活玩家中资产最高者
    int winner = -1;
    int32_t bestAssets = 0;
    for (int p = 0; p < st.playerCount; ++p) {
        if (st.players[p].alive != 0) {
            const int32_t v = playerTotalAssets(app, p);
            if (bestAssets < v) {
                bestAssets = v;
                winner = p;
            }
        }
    }
    if (winner < 0) {
        return 0;
    }
    // [RE 0x41D8FF] 胜利条件：(有资产 ∧ 天数上限到达) ∨ (资产达标)
    const bool byTime = bestAssets != 0 && st.gameDaysLimit != 0 && st.gameDaysLimit <= st.dayCount;
    const bool byMoney = st.winMoney != 0 && bestAssets >= st.winMoney;
    if (!byTime && !byMoney) {
        return 0;
    }
    // [RE 0x41D915] *g_currentPlayer = winner + sub_41906A(1)（强制刷新一次场景）
    st.currentPlayer = winner;
    renderGameFrame(app);
    // [RE 0x41D947] 赢家胜利大笑 off_4808AA（列24 expr3）
    const int wci = st.players[winner].charIndex;
    if (wci >= 0 && wci < 12) {
        playLine(app, winner, kMoneyLines[wci][21], 3);
    }
    // [RE 0x41D94F] 其余玩家标记淘汰（alive=0）
    for (int i = 0; i < st.playerCount; ++i) {
        if (i != winner) {
            st.players[i].alive = 0;
        }
    }
    if (st.humanCount == 1) {
        if ((st.players[winner].alive & 1) != 0) {
            // [RE 0x41D982] 单人人类赢：其余 AI 角色 charState=2（下局灰度+红X 不可选）+ scene 2
            for (int i = 0; i < st.playerCount; ++i) {
                if (i == winner) {
                    continue;
                }
                const int oci = st.players[i].charIndex;
                if (oci >= 0 && oci < 12) {
                    st.newGameConfig.aiUsed[oci] = true;
                }
            }
            st.sceneRequest = 2;
        } else {
            // [RE 0x41D9BB] 单人 AI 赢：*g_currentPlayer=0 → defeatFlow（失败界面，无 FLC556）
            st.currentPlayer = 0;
            st.sceneRequest = defeatFlow(app, /*playExitFlc=*/false);
        }
    } else {
        // [RE 0x41DA43] 多人：人类赢 → scene 3（结算后主菜单）；AI 赢 → scene 1（主菜单）
        st.sceneRequest = ((st.players[winner].alive & 1) != 0) ? 3 : 1;
    }
    RICH4_LOGI("checkVictory: winner=p%d byTime=%d byMoney=%d scene=%d (RE 0x41D89E)", winner,
               byTime ? 1 : 0, byMoney ? 1 : 0, st.sceneRequest);
    return 1;
}

namespace {

struct DefeatState {
    Application* app = nullptr;
    UiImage banner;                    // [RE 0x40785B] panel.mkf[112] 失败界面图集
    bool hasBanner = false;
    int countdown = 10;                // [RE dword_48A440]
    std::vector<uint16_t> fullBg;      // [RE 0x406BC1] saveBackground(640×480)
    std::vector<uint16_t> numBg;       // 数字区背景快照（回背景再叠新数字）
};

// [RE 0x406BE5/0x406C09] 标题帧 0 @(320,120)、提示帧 1 @(320,350)
void paintBanner(DefeatState& ds) {
    Surface& dst = ds.app->surface();
    if (ds.hasBanner) {
        if (ds.banner.frameCount() > 0) {
            blitElement(dst, ds.banner.frame(0), 320, 120, false);
        }
        if (ds.banner.frameCount() > 1) {
            blitElement(dst, ds.banner.frame(1), 320, 350, false);
        }
    }
}

// [RE 0x406CE8] 倒计时数字 @(320,350)：原版用图集帧 dword_48A3A8+12*count，
//   重写以文本渲染（图集帧映射待实机核对，见 docs/reverse/frame-anchor.md）
void paintCount(DefeatState& ds) {
    Surface& dst = ds.app->surface();
    restoreRegion(dst, ds.numBg, 280, 300, 80, 100);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", ds.countdown);
    ds.app->text().setFont(32, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    ds.app->text().drawText(dst, buf, 320, 350, 4);
}

// [RE 0x406B14] defeatWndProc：10s 倒计时模态
bool defeatHandler(const SDL_Event* event, void* user) {
    DefeatState& ds = *static_cast<DefeatState*>(user);
    if (!event) {
        ds.countdown = 10;
        saveRegion(ds.fullBg, ds.app->surface(), 0, 0, 640, 480);
        paintBanner(ds);
        saveRegion(ds.numBg, ds.app->surface(), 280, 300, 80, 100);
        paintCount(ds);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        --ds.countdown;
        if (ds.countdown <= 0) {
            // [RE 0x406D34] 超时 → postModalExit(0) → 回主菜单
            ds.app->events().requestExit(0);
            return true;
        }
        paintCount(ds);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        // [RE 0x406B52/0x406D50] WM_LBUTTONUP → result=1 → 读档继续
        ds.app->events().requestExit(1);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        const SDL_Keycode k = event->key.key;
        // [RE 0x406D4A] word_497178 確定键（CFG 载入，重写取 Enter/空格 作確定）
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
            ds.app->events().requestExit(1);
            return true;
        }
    }
    return false;
}

// ===== [RE 0x4060E9] mapSelectWndProc 地图选择面板（通关后选下一图）=====
struct MapSelState {
    Application* app = nullptr;
    UiImage ui;            // jumpUi = JUMP.MKF[8]
    int panelFrame = 15;   // [RE 0x4061D4] 面板底帧 普通=15 / 时空=20
    int itemBase = 10;     // [RE 0x4061DE] 地图项帧基 普通=10(→11-14) / 时空=15(→16-19)
    int panelY = -80;      // [RE dword_48A421] 面板滑入 y（-80 → 360）
    int slideStep = 30;    // [RE dword_48A425]
    int hover = 0;         // [RE byte_48A439] 1..4，0=无
    bool multiple = false; // [RE byte_48A435] 多人→仅「恭喜過關」文字
};

// [RE 0x40645A] 面板 + 4 地图项帧；已通关项标 CLEAR（原版附加勾选帧，观感精修待实机）
void drawMapSelect(MapSelState& ms) {
    Surface& s = ms.app->surface();
    GameState& st = ms.app->gameState();
    if (ms.ui.frameCount() > ms.panelFrame) {
        blitElementOpaque(s, ms.ui.frame(ms.panelFrame), 320, ms.panelY);
    }
    for (int item = 1; item <= 4; ++item) {
        const int fr = ms.itemBase + item;
        const int y = 40 * (item - 1) + 300;
        if (ms.ui.frameCount() > fr) {
            blitElementOpaque(s, ms.ui.frame(fr), 320, y);
        }
        if (st.clearedMaps[item - 1] != 0) {
            ms.app->text().setFont(16, 0xFF5050, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
            ms.app->text().drawText(s, "CLEAR", 468, y, 4);
        }
    }
    if (ms.multiple) {
        ms.app->text().setFont(48, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        ms.app->text().drawText(s, "恭喜勝利過關！！", 320, 240, 4); // [RE byte_463176]
    }
}

bool mapSelHandler(const SDL_Event* event, void* user) {
    MapSelState& ms = *static_cast<MapSelState*>(user);
    GameState& st = ms.app->gameState();
    if (!event) {
        drawMapSelect(ms);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (ms.panelY < 360) {
            ms.panelY = std::min(360, ms.panelY + ms.slideStep);
            ms.slideStep = std::max(1, ms.slideStep - 2);
            drawMapSelect(ms);
        }
        return true;
    }
    const bool ready = (ms.panelY >= 360);
    if (event->type == SDL_EVENT_MOUSE_MOTION && !ms.multiple && ready) {
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int h = 0;
        if (x >= 200 && x <= 440 && y >= 280 && y < 440) {
            h = std::clamp((y - 280) / 40 + 1, 1, 4);
        }
        if (h != ms.hover) {
            ms.hover = h;
            if (h > 0 && st.clearedMaps[h - 1] == 0) {
                ms.app->audio().playEffect(0); // [RE g_uiSoundHover]
            }
            drawMapSelect(ms);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            ms.app->events().requestExit(0);
            return true;
        }
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (ms.multiple) { // [RE 0x4067F9] 多人：点击退出
                ms.app->events().requestExit(0);
                return true;
            }
            if (!ready) { // [RE setPauseDraw] 点击提前到位
                ms.panelY = 360;
                ms.slideStep = 0;
                drawMapSelect(ms);
                return true;
            }
            const int x = static_cast<int>(event->button.x);
            const int y = static_cast<int>(event->button.y);
            if (x >= 200 && x <= 440 && y >= 280 && y < 440) {
                const int item = std::clamp((y - 280) / 40 + 1, 1, 4);
                if (st.clearedMaps[item - 1] == 0) { // [RE 0x406821] 已通关不可选
                    ms.app->audio().playEffect(2);   // [RE g_uiSoundConfirm]
                    st.mapIndex = item - 1;          // [RE 0x406935] word_4991B8 = 选中
                    st.newGameConfig.mapIndex = item - 1;
                    RICH4_LOGI("mapSelect: chose map=%d (RE 0x4060E9)", item - 1);
                    ms.app->events().requestExit(1);
                }
            }
            return true;
        }
    }
    return false;
}

} // namespace

// [RE 0x407842] defeatFlow
int defeatFlow(Application& app, bool playExitFlc) {
    GameState& st = app.gameState();
    DefeatState ds;
    ds.app = &app;
    if (auto blob = st.panel.read(112)) {
        if (ds.banner.load(std::move(*blob))) {
            ds.hasBanner = true;
        }
    }
    trace::logf("dialog open name=defeat");
    // [RE 0x406C37] SetTimer(1000ms) → runModal tickMs=1000
    const int confirmed = runModal(app, &defeatHandler, &ds, 1000);
    restoreRegion(app.surface(), ds.fullBg, 0, 0, 640, 480);
    if (confirmed) {
        // [RE 0x407956] 失败鼓励台词 off_4808B2（列26 expr3），对当前玩家（=赢家 AI）
        const int p = st.currentPlayer;
        if (p >= 0 && p < 4) {
            const int ci = st.players[p].charIndex;
            if (ci >= 0 && ci < 12) {
                playLine(app, p, kMoneyLines[ci][23], 3);
            }
        }
        if (playExitFlc) {
            // [RE 0x407980..0x4079a1] 破产路径 lpBuffer==0：sub_41906A(0) 刷新 + FLC556 失败演出
            renderGameFrame(app);
            playEventFlc(app, 556, 0, 40, 101);
        }
        // [RE 0x4079A9] 清其余玩家 charState（aiUsed=false），供下局可选
        for (int i = 1; i < st.playerCount; ++i) {
            const int ci = st.players[i].charIndex;
            if (ci >= 0 && ci < 12) {
                st.newGameConfig.aiUsed[ci] = false;
            }
        }
        st.players[0].alive = 1; // [RE 0x4079D0] g_playerAlive[0]=1（读档继续标记）
        RICH4_LOGI("defeatFlow: confirmed -> scene 4 loadGame (RE 0x407842)");
        return 4;
    }
    RICH4_LOGI("defeatFlow: timeout -> scene 1 main menu (RE 0x407842)");
    return 1;
}

// [RE 0x4075C1] gameClearFlow
void gameClearFlow(Application& app) {
    GameState& st = app.gameState();
    // [RE 0x4075CF] 标记当前地图已通关
    if (st.mapIndex >= 0 && st.mapIndex < 4) {
        st.clearedMaps[st.mapIndex] = 1;
    }
    int cleared = 0;
    for (int i = 0; i < 4; ++i) {
        if (st.clearedMaps[i] != 0) {
            ++cleared;
        }
    }
    trace::logf("clear map=%d cleared=%d", st.mapIndex, cleared);
    if (cleared >= 4) {
        // [RE 0x407797] 4 图全通：原版按模式播 END%02d/THANKS（时空之旅）或 END/OVER.AVI（普通）。
        //   A5 决策：不实现 AVI 解码（Indeo5 / FFmpeg GPLv3 不引入）→ TODO 静态致谢，直接 quitGame。
        RICH4_LOGI("gameClearFlow: ALL 4 cleared -> quit (AVI playback TODO A5) (RE 0x4075C1)");
        st.quitGame = true;
        return;
    }
    // [RE 0x407780] mapSelectWndProc 0x4060E9：单人选下一图，多人仅「恭喜過關」。
    //   headless(quickstart)：跳过交互面板，自动选下一未通关图（实机走下方 mapSelectDialog）
    if (st.debugQuickstart) {
        if (st.humanCount == 1) {
            for (int k = 1; k <= 4; ++k) {
                const int c = (st.mapIndex + k) % 4;
                if (st.clearedMaps[c] == 0) {
                    st.mapIndex = c;
                    st.newGameConfig.mapIndex = c;
                    break;
                }
            }
        }
        RICH4_LOGI("mapSelect: auto map=%d (headless bypass RE 0x4060E9)", st.mapIndex);
    } else {
        MapSelState ms;
        ms.app = &app;
        ms.multiple = (st.humanCount != 1);
        if (st.gameMode != 0) {
            ms.panelFrame = 20;
            ms.itemBase = 15;
        }
        if (auto blob = st.jump.read(8)) {
            ms.ui.load(std::move(*blob));
        }
        std::vector<uint16_t> bg;
        saveRegion(bg, app.surface(), 0, 0, 640, 480);
        trace::logf("dialog open name=map_select");
        runModal(app, &mapSelHandler, &ms, 50); // [RE 0x40621E] SetTimer 50ms 滑入动画
        restoreRegion(app.surface(), bg, 0, 0, 640, 480);
    }
    RICH4_LOGI("gameClearFlow: cleared=%d -> map=%d (RE 0x4075C1/0x4060E9)", cleared, st.mapIndex);
}

} // namespace rich4
