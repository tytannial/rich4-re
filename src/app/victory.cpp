#include "game/app/victory.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// ===== [RE 0x4060E9] mapSelectWndProc 地图选择界面（通关后选下一图）=====
// 原版为全屏演出界面（双缓冲 offscreen 每帧重铺）：地图预览水平滚动 + 胜利角色
//   左右行走（panel[charIdx+100] 25 帧 = 5 站立 + 2 方向×10 行走）+ 装饰动画
//   （panel[93]，帧表 byte_46CCC4）；单人 = 面板底帧(含预合成蓝星)滑入 + 悬停项帧高亮
//   + 点击红勾(帧8)合成进项帧、10 tick 后退出；多人(byte_48A435) = 仅「恭喜過關」大字。
//   重写即时模式：每 tick 全屏重画（滚动背景→sprite→面板），消除旧版叠画重影。
struct MapSelState {
    Application* app = nullptr;
    UiImage ui;            // jumpUi = JUMP.MKF[8]（蓝星/红勾已预合成进面板/项帧）
    int panelFrame = 15;   // [RE 0x4061D4] 面板底帧 普通=15 / 时空=20
    int itemBase = 10;     // [RE 0x4061DE] 地图项帧基 普通=10(→11-14) / 时空=15(→16-19)
    int panelY = -80;      // [RE dword_48A421] 面板滑入 y（-80 → 360）
    int slideStep = 30;    // [RE dword_48A425] 每 tick +=step--
    int hover = 0;         // [RE byte_48A439] 1..4，0=无
    bool multiple = false; // [RE byte_48A435] 多人→仅「恭喜過關」文字
    // 演出层（0x406283..0x40644B）
    std::vector<uint8_t> preview;  // [RE 0x4075E3] JUMP.MKF[map+4·mode] 640×480 RGB555
    int scrollX = 0;               // [RE dword_48A415] +=4/tick，1280 环绕
    UiImage hero;                  // [RE dword_48A3BC] panel[charIdx+100]
    UiImage deco;                  // [RE dword_48A38C] panel[93]
    int walkFrames = 10;           // [RE dword_48A429] (hero 帧数-4)>>1
    int dir = 0;                   // [RE byte_48A436] 0=向右入场 1=向左
    int walkFrame = 0;             // [RE byte_48A437] 0..walkFrames-1
    int decoFrame = 0;             // [RE byte_48A438] 0..5
    int walkX = 0;                 // [RE dword_48A419] -100..740
    int walkY = 0;                 // [RE dword_48A41D] 0=空闲待随机
    // 退出状态机（byte_48A43A/48A43B）
    int exitState = 0;             // 0=正常 1=退出 2=红勾等待
    int exitCount = 0;             // [RE byte_48A43B] 10 tick
};

// [RE byte_46CCC4] 装饰帧表（panel[93]，6 帧 × 2 方向）
constexpr uint8_t kMapSelDecoFrames[12] = {0, 1, 2, 3, 5, 6, 12, 13, 14, 15, 17, 18};

// 每帧全屏重画（对应原版 WM_TIMER 0x40633D Lock→blit→Unlock 双缓冲）
void redrawMapSelect(MapSelState& ms) {
    Application& app = *ms.app;
    GameState& st = app.gameState();
    Surface& s = app.surface();
    if (!ms.preview.empty()) {
        blitScrolledMap(s, ms.preview.data(), ms.scrollX); // [RE 0x406355]
    }
    if (ms.hero.frameCount() > 0) {
        // [RE 0x406375..0x4063D2] 角色行走：帧 = ((dir^1)·walkFrames + walkFrame + 5)
        const int hf = std::min((ms.dir ^ 1) * ms.walkFrames + ms.walkFrame + 5,
                                ms.hero.frameCount() - 1);
        const int hx = ms.dir ? ms.walkX + 90 : ms.walkX - 90;
        blitSpriteFrame(s, ms.hero, hf, hx, ms.walkY);
    }
    if (ms.deco.frameCount() > 0) {
        // [RE 0x4063EB..0x40644B] 装饰：帧表[6·dir+frame]，与角色对侧
        const int df = kMapSelDecoFrames[ms.dir * 6 + ms.decoFrame];
        const int dx = ms.dir ? ms.walkX - 90 : ms.walkX + 90;
        if (df < ms.deco.frameCount()) {
            blitSpriteFrame(s, ms.deco, df, dx, ms.walkY);
        }
    }
    if (ms.multiple) {
        // [RE 0x406551] 多人：仅大字「恭喜勝利過關！！」（无面板/项）
        app.text().setFont(48, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
        app.text().drawText(s, "恭喜勝利過關！！", 320, 240, 2); // [RE byte_463176]
        return;
    }
    // [RE 0x4064D2] 面板底（已含蓝星预合成）滑入；0x4064EC 悬停未通关项帧高亮
    if (ms.ui.frameCount() > ms.panelFrame) {
        blitElementOpaque(s, ms.ui.frame(ms.panelFrame), 320, ms.panelY);
    }
    if (ms.hover != 0 && st.clearedMaps[ms.hover - 1] == 0 &&
        ms.ui.frameCount() > ms.itemBase + ms.hover) {
        blitElementOpaque(s, ms.ui.frame(ms.itemBase + ms.hover), 320,
                          40 * (ms.hover - 1) + 300);
    }
}

bool mapSelHandler(const SDL_Event* event, void* user) {
    MapSelState& ms = *static_cast<MapSelState*>(user);
    GameState& st = ms.app->gameState();
    if (!event) {
        redrawMapSelect(ms);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        // [RE 0x406250] 红勾等待 10 tick → 退出
        if (ms.exitState == 2 && --ms.exitCount <= 0) {
            ms.exitState = 1;
            ms.app->events().requestExit(1);
            return true;
        }
        // [RE 0x406283] 预览滚动（4 字节/tick，1280 环绕）
        ms.scrollX += 4;
        if (ms.scrollX >= 1280) {
            ms.scrollX = 0;
        }
        // [RE 0x4062A0..0x4062E8] 角色横穿：walkY==0 → 随机 y+换向+定起点；否则 ±10/tick
        if (ms.walkY != 0) {
            if (ms.dir != 0) {
                ms.walkX -= 10;
                if (ms.walkX <= -100) {
                    ms.walkY = 0;
                }
            } else {
                ms.walkX += 10;
                if (ms.walkX >= 740) {
                    ms.walkY = 0;
                }
            }
        } else {
            ms.walkY = std::rand() % 360 + 100;
            ms.dir ^= 1;
            ms.walkX = ms.dir != 0 ? 740 : -100;
        }
        // [RE 0x406365..0x4063EF] 帧循环
        if (++ms.walkFrame == ms.walkFrames) {
            ms.walkFrame = 0;
        }
        if (++ms.decoFrame == 6) {
            ms.decoFrame = 0;
        }
        // [RE 0x40646C] 面板滑入：panelY += step--（30→1），过 360 截断
        if (ms.panelY < 360) {
            ms.panelY += ms.slideStep--;
            if (ms.panelY > 360) {
                ms.panelY = 360;
            }
        }
        redrawMapSelect(ms);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION && !ms.multiple && ms.panelY == 360) {
        // [RE 0x4065A2] 悬停区 x∈(200,440) y∈(280,440)，item=(y-280)/40+1
        const int x = static_cast<int>(event->motion.x);
        const int y = static_cast<int>(event->motion.y);
        int h = 0;
        if (x > 200 && x < 440 && y > 280 && y < 440) {
            h = std::clamp((y - 280) / 40 + 1, 1, 4);
        }
        if (h != ms.hover) {
            ms.hover = h;
            if (h > 0 && st.clearedMaps[h - 1] == 0) {
                ms.app->audio().playEffect(0); // [RE 0x406705 g_uiSoundHover]
            }
            redrawMapSelect(ms);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (ms.multiple) { // [RE 0x4067F9] 多人：点击退出 → 主菜单
            ms.app->events().requestExit(0);
            return true;
        }
        // [RE 0x406821] 单人：滑入到位 + 悬停未通关项 → 确认
        if (ms.panelY == 360 && ms.exitState == 0 && ms.hover != 0 &&
            st.clearedMaps[ms.hover - 1] == 0) {
            ms.app->audio().playEffect(2); // [RE 0x406835 g_uiSoundConfirm]
            // [RE 0x4068B1] 红勾（帧8）预合成进选中项帧 @(214,8) → 该项常显勾
            if (ms.ui.frameCount() > 8 &&
                ms.ui.frameCount() > ms.itemBase + ms.hover) {
                ms.ui.blitIntoFrame(ms.itemBase + ms.hover, ms.ui.frame(8), 214, 8);
            }
            st.mapIndex = ms.hover - 1;         // [RE 0x406935] word_4991B8 = 选中
            st.newGameConfig.mapIndex = ms.hover - 1;
            RICH4_LOGI("mapSelect: chose map=%d (RE 0x4060E9)", ms.hover - 1);
            ms.exitState = 2;                   // [RE 0x40694A] byte_48A43A=2, byte_48A43B=10
            ms.exitCount = 10;
            redrawMapSelect(ms);
        }
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_RIGHT) {
        if (ms.multiple) { // [RE 0x406970] 多人右键退出；单人不可退（必须选图）
            ms.app->events().requestExit(0);
            return true;
        }
        return true;
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
        // [RE 0x407623] jumpUi = JUMP.MKF[8]
        if (auto blob = st.jump.read(8)) {
            ms.ui.load(std::move(*blob));
        }
        // [RE 0x4076B1..0x407723] 蓝星（帧10）预合成：已通关地图 → 悬停项帧 @(32,22)
        //   + 面板底帧 @(220, 40·i+28)（blitElementToCanvas 0x4562A5 语义）
        if (ms.ui.frameCount() > 10) {
            const UiFrameView star = ms.ui.frame(10);
            for (int i = 0; i < 4; ++i) {
                if (st.clearedMaps[i] != 0) {
                    ms.ui.blitIntoFrame(ms.itemBase + 1 + i, star, 32, 22);
                    ms.ui.blitIntoFrame(ms.panelFrame, star, 220, 40 * i + 28);
                }
            }
        }
        // [RE 0x4075E3..0x407609] 当前（刚通关）地图预览 = JUMP.MKF[map + 4·mode]
        if (auto blob = st.jump.read(static_cast<size_t>(st.mapIndex + 4 * st.gameMode))) {
            if (blob->size() >= 614400) {
                ms.preview = std::move(*blob);
            }
        }
        // [RE 0x407637..0x40768C] 首个未淘汰玩家（=胜者）角色行走 sprite panel[charIdx+100]
        //   + 装饰 panel[93]
        int hi = 0;
        while (hi < st.playerCount && st.players[hi].alive == 0) {
            ++hi;
        }
        if (hi < st.playerCount) {
            const int ci = st.players[hi].charIndex;
            if (ci >= 0 && ci < 12) {
                if (auto blob = st.panel.read(static_cast<size_t>(ci + 100))) {
                    if (ms.hero.load(std::move(*blob))) {
                        ms.walkFrames = (ms.hero.frameCount() - 4) >> 1; // [RE 0x4061F5]
                    }
                }
            }
        }
        if (auto blob = st.panel.read(93)) {
            ms.deco.load(std::move(*blob));
        }
        std::vector<uint16_t> bg;
        saveRegion(bg, app.surface(), 0, 0, 640, 480);
        app.audio().playSceneMusic(6, false); // [RE 0x407771] musicPlayScene(0x8006) 不压栈
        trace::logf("dialog open name=map_select");
        runModal(app, &mapSelHandler, &ms, 50); // [RE 0x40621E] SetTimer 50ms
        app.audio().stopMusic();                // [RE 0x407788] musicStop
        restoreRegion(app.surface(), bg, 0, 0, 640, 480);
    }
    RICH4_LOGI("gameClearFlow: cleared=%d -> map=%d (RE 0x4075C1/0x4060E9)", cleared, st.mapIndex);
}

} // namespace rich4
