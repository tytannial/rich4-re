// 特殊地点三种小游戏（landingEvent case 6/7/8）。
// 依据: sub_415215/0x4154DC/0x4155FC + 共享 else 0x415457 反编译；
//       详见 docs/reverse/functions/41982d-p2-events.md §6。
//
// 当前状态：
//   case 7 七彩氣球 = 交互版已实现（balloonWndProc 0x414BBC + sub_412F6F/sub_4146EE/
//     sub_413F07 全逆向，2026-09-25）。
//   case 6 企鵝挖寶 / case 8 喜從天降 = 交互版未接入，人类同走共享 else 兜底
//     （与原版"动画过程关闭"行为一致）。
//
// 交互小游戏公共流程（三游戏同构，供后续接入）：
//   门控（人类 && settings[1]）→ 载 Panel.mkf 资源 → musicPlayScene 压栈 →
//   runModal(WndProc) → musicStackPopRestore → 返回得分（→点券）。
//   开场 WM_USER(0x401)：倒计时 150/150/360 tick（100/100/50ms）+ 开场计数 99 +
//   SetTimer + 画背景；开场计数归 0 → 0x405：阻塞播 panel[78] 标题 FLC @(0,0) 透明 →
//   cursorSelect(挖=42,1,0 / 气=9,3,5 / 接=41 无开场 FLC 差异见各段) → 游戏体。
//   HUD y=421（panel[79] 小数字帧 0..9，不透明）：倒计时 3 位 @(49,69,94) + 单位帧0
//   @(114)；挖宝/接钱四计数 @(185,205)/(276,296)/(367,387)/(458,478)；
//   总分 挖/接 %03d @(549,569,589)、气球 %04d @(529,549,569,589)。
//   结算（drawMiniScoreBig 0x414789）：得分 "%d" 大字 = panel[79] 帧 10+d，
//   x=353-66*len/2 起、字宽 66、y=150；停留 2000ms（sub_45285E）→ postModalExit。
//   音效（Effect.mkf）：气球 生成19/miss20/命中21；挖宝 {11,12,13,14,16,17,18,15}
//   （档位>55=13、<40=14）；接钱无。音乐场景：挖12/气11/接10。
//
// case 6 企鵝挖寶（digWndProc 0x414858，100ms）：9×9 网格 g_digCellValid/g_digGrid
//   （步距 4 word：+0 有宝、+4 低 nibble 翻开态/高 nibble 宝物类型 1..5）；
//   digInitLayout 0x412014 埋宝：5 类数量 g_digTreasureCounts{3,12,3,9,1}，
//   v=(64递减)*rand()>>15 随机落位；点击读 panel[81] 像素掩码 (px%9, px/9) →
//   digSetTarget 0x41211C（16.16 定点行走路径）→ sub_412287 行走挖取 → sub_4124C8；
//   得分=5*CntA+20*CntD+12*CntB+8*CntC（drawDigHud 0x413A4A）；资源 panel
//   78/79/80/81/82-85(企鹅)/86-90(5 宝物)。待精读 sub_412287/4124C8。
// case 8 喜從天降（moneyWndProc 0x414FCD，50ms）：开场 data.mkf[526] FLC（逐帧
//   flcDecodeFrame，播完直接结算）；娃娃=panel[100+char]（半宽=(宽-5)>>1，x 初值 320）、
//   钱袋 5 图=panel[95..99]、出生 y=110；sub_413248 生成/下落/接取（待精读）；
//   得分=10*CntA+5*CntB+3*CntC+CntD（drawMoneyHud 0x41417E，时间显示 tick>>1）；
//   评级 word_48BD56：>=60→3/>=50→2/>=40→0/<40→1。

#include "game/app/minigame.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/event_stack.h"
#include "game/app/item_lines.h"
#include "game/app/message_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/cursor.h"
#include "game/render/fli.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// ==================== 七彩氣球（case 7）====================

// [RE 0x4154FA/0x415597/0x4155A5] 气球常量：门控后 musicPlayScene(11)、
//   runModal(balloonWndProc) SetTimer 100ms；倒计时 dword_48BD2C=150、开场 99
constexpr int kBalloonCountdown = 150;  // [RE 0x414C1D] 150 tick = 15s
constexpr int kBalloonIntro = 99;       // [RE 0x414C27] 开场等待 tick
constexpr int kSfxBalloonNew = 19;      // [RE 0x41307B] g_balloonSoundTable[0] 新气球
constexpr int kSfxBalloonMiss = 20;     // [RE 0x414F14] +8 未命中
constexpr int kSfxBalloonHit = 21;      // [RE 0x414E13] +0x10 命中

// [RE 0x475004] 各类型上升速度（px/tick，type 0..11）
const uint8_t kBalloonSpeed[12] = {15, 15, 15, 15, 18, 18, 18, 24, 24, 24, 24, 18};
// [RE 0x475039] 特殊气球类型表（r∈[28,30) 时 rand()%10 取；0x80=诱饵气球：
//   高 nibble≠0 → 不移动不可点，8 tick 后消失，画帧 1）
const uint8_t kBalloonSpecial[10] = {9, 9, 9, 9, 9, 10, 10, 10, 10, 0x80};

struct Balloon {
    int16_t x = 0;    // g_balloons[4i]（0=空槽）
    int16_t y = 0;    // +2
    int16_t type = 0; // +4 word_48BC48（0..8=分值+1；9=×2；10=÷2；11=随机效果；
                      // 60=已破（高 nibble 每 tick -0x10 归零清槽）；高 nibble≠0 不可点）
};

struct BalloonCtx {
    Application* app = nullptr;
    UiImage scene;             // [RE 0x41555C] panel.mkf[91]：0 背景/1..12 气球/13 爆裂
    UiImage digits;            // [RE 0x415542] panel.mkf[79]：0..9 小数字/10..19 大数字
    FliDecoder title;          // [RE 0x415529] panel.mkf[78] 标题 FLC（640×480 透明）
    bool titleOk = false;
    Balloon balloons[16];
    int state = 0;             // 0=开场等待 1=标题FLC 2=游戏体 3=结算
    int intro = 0;             // dword_48BD84
    int countdown = 0;         // g_miniCountdown
    int score = 0;             // g_miniScore
    int gamePhase = 0;         // g_miniPhase（1=时间到 2=全消/进结算）
    int slowdown = 0;          // byte_48BD59（特殊气球效果：气球静止 tick）
    int wind = 0;              // dword_48BCC8（-1=速度×2 / +1=速度÷2）
    uint64_t settleMs = 0;
};

// [RE 0x450F04] 解码一帧；播完返回 true（同 lottery_dialog.cpp 辅助）
bool decodeFlcOnce(FliDecoder& flc) {
    if (!flc.valid()) {
        return true;
    }
    if (!flc.nextFrame()) {
        return true;
    }
    return flc.frameIndex() >= flc.frameCount();
}

// [RE 0x450F04] FLC 当前帧落到 (x,y)（flags&1 透明=色键 0）
void blitFlcFrame(Surface& dst, const FliDecoder& flc, int x, int y, bool transparent) {
    if (!flc.valid()) {
        return;
    }
    const uint16_t key = flc.colorKey();
    const uint16_t* src = flc.pixels();
    for (int row = 0; row < flc.height(); ++row) {
        const int py = y + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < flc.width(); ++col) {
            const int px = x + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            const uint16_t c = src[static_cast<size_t>(row) * flc.width() + col];
            if (transparent && c == key) {
                continue;
            }
            dst.pixels()[static_cast<size_t>(py) * Surface::kWidth + px] = c;
        }
    }
}

// [RE 0x412F6F] 气球移动/生成（绘制在 drawBalloon 中统一做；原版 off-screen 清槽后
//   同 tick 可再用于生成，拆分后下一 tick 复用，观感无差）
void balloonTick(BalloonCtx& ui) {
    for (int i = 0; i < 16; ++i) {
        Balloon& b = ui.balloons[i];
        if (b.x != 0) {
            if ((b.type & 0xF0) != 0) {
                b.type -= 16;  // 爆裂/诱饵倒计时（高 nibble 归零 → 清槽）
                if ((b.type & 0xF0) == 0) {
                    b.x = 0;
                    continue;
                }
            } else if (ui.slowdown == 0) {
                int sp = kBalloonSpeed[b.type & 0xF];
                if (ui.wind == -1) {
                    sp *= 2;       // [RE 0x41310D] 顺风加速
                } else if (ui.wind == 1) {
                    sp >>= 1;      // [RE 0x413111] 逆风减速
                }
                b.y = static_cast<int16_t>(b.y - sp);
            }
            continue;
        }
        if (ui.gamePhase != 0) {
            continue;  // [RE 0x4131A3] 时间到后不再生成
        }
        // [RE 0x4131B4] 生成概率：rand()%1000，97% 不出
        const int r = std::rand() % 1000;
        int type;
        if (r < 20) {
            type = r >> 2;                    // 0..4（普通大气球）
        } else if (r < 28) {
            type = ((27 - r) >> 1) + 5;       // 5..8（普通小气球）
        } else if (r < 30) {
            type = kBalloonSpecial[std::rand() % 10];  // 特殊/诱饵
        } else {
            continue;
        }
        // [RE 0x413021..0x41309B] 8 列候选 x=40..600 步 80，列内无 y>300 的气球才可生成
        int cols[8];
        int nc = 0;
        for (int cx = 40; cx < 640; cx += 80) {
            bool busy = false;
            for (int j = 0; j < 16; ++j) {
                if (ui.balloons[j].x == cx && ui.balloons[j].y > 300) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                cols[nc++] = cx;
            }
        }
        if (nc == 0) {
            continue;
        }
        ui.app->audio().playEffect(kSfxBalloonNew);
        b.x = static_cast<int16_t>(cols[std::rand() % nc]);
        b.y = 420;   // 屏幕下方升起
        b.type = static_cast<int16_t>(type);
    }
}

// [RE 0x414BBC 0x201/0x203] 点击：按下与抬起各射一枪（原版 quirk），一次可命中多个
void balloonClick(BalloonCtx& ui, int mx, int my) {
    if (ui.state != 2 || ui.gamePhase >= 2) {
        return;  // 开场/标题/结算忽略（[RE 0x414DB3]）
    }
    Application& app = *ui.app;
    // 原版单 DirectSound buffer（后播覆盖）：一次射击多球时只落最终一声
    int lastSound = 0;
    for (int i = 0; i < 16; ++i) {
        Balloon& b = ui.balloons[i];
        if (b.x == 0 || (b.type & 0xF0) != 0) {
            continue;
        }
        const int hw = (b.type >= 6) ? 18 : 22;  // [RE 0x414DD9/0x414F53] 小/大气球命中盒
        const int hh = (b.type >= 6) ? 26 : 30;
        if (mx < b.x - hw || mx > b.x + hw || my < b.y - hh || my > b.y + hh) {
            lastSound = kSfxBalloonMiss;
            continue;
        }
        lastSound = kSfxBalloonHit;
        switch (b.type) {
        case 9:
            ui.score *= 2;          // [RE 0x414E45] ×2 气球
            break;
        case 10:
            ui.score >>= 1;         // [RE 0x414E51] ÷2 气球
            break;
        case 11: {                  // [RE 0x414E86] "?" 气球随机效果
            switch (std::rand() % 6) {
            case 0: ui.countdown = 1; break;   // 立即收尾
            case 1: ui.slowdown = 20; break;   // 气球静止 2s
            case 2: ui.wind = -1; break;       // 加速风
            case 3: ui.wind = 1; break;        // 减速风
            case 4: ui.score = 0; break;       // 清零
            default: ui.score *= 2; break;     // 翻倍
            }
            break;
        }
        default:
            ui.score += b.type + 1;            // [RE 0x414ECE] 分值 = type+1
            if (ui.score >= 1000) {
                ui.score = 999;                // [RE 0x414EED] cap 999
            }
            break;
        }
        b.type = 60;  // [RE 0x414F01] 已破标记（3 tick 爆裂帧 13 后清槽）
    }
    if (lastSound != 0) {
        app.audio().playEffect(lastSound);
    }
}

// [RE 0x4146EE/0x412F6F/0x413F07/0x414789] 全场景即时重绘：背景 + 气球层 + HUD (+ 叠加)
void drawBalloon(BalloonCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.scene.frame(0), 0, 0);  // [RE 0x414714] 背景帧 0

    // 气球层 + 出界清槽（[RE 0x41315B] blitElementToCanvas 返回非 0 = 全裁剪）
    bool anyAlive = false;
    for (int i = 0; i < 16; ++i) {
        Balloon& b = ui.balloons[i];
        if (b.x == 0) {
            continue;
        }
        if (blitElement(dst, ui.scene.frame((b.type & 0xF) + 1), b.x, b.y, false)) {
            anyAlive = true;
        } else {
            b.x = 0;
        }
    }
    if (!anyAlive && ui.gamePhase == 1 && ui.state == 2) {
        ui.gamePhase = 2;  // [RE 0x413220] 时间到且气球全清 → 结算
    }

    // [RE 0x413F35..0x4140F0] HUD：倒计时 %03d + 单位帧 0；得分 %04d
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", ui.countdown);
    static const int kTimeX[3] = {49, 69, 94};
    for (int i = 0; i < 3; ++i) {
        blitElementOpaque(dst, ui.digits.frame(buf[i] - '0'), kTimeX[i], 421);
    }
    blitElementOpaque(dst, ui.digits.frame(0), 114, 421);
    std::snprintf(buf, sizeof(buf), "%04d", ui.score);
    static const int kScoreX[4] = {529, 549, 569, 589};
    for (int i = 0; i < 4; ++i) {
        blitElementOpaque(dst, ui.digits.frame(buf[i] - '0'), kScoreX[i], 421);
    }

    if (ui.state == 1) {
        blitFlcFrame(dst, ui.title, 0, 0, true);  // [RE 0x414D60] 标题 FLC 透明叠加
    }
    if (ui.state == 3) {
        // [RE 0x414789] 大字得分：帧 10+d，x=353-66*len/2，y=150，字宽 66
        std::snprintf(buf, sizeof(buf), "%d", ui.score);
        const int len = static_cast<int>(std::strlen(buf));
        int x = 353 - 66 * len / 2;
        for (int i = 0; i < len; ++i) {
            blitElementOpaque(dst, ui.digits.frame(10 + buf[i] - '0'), x, 150);
            x += 66;
        }
    }
}

// [RE 0x414BBC] balloonWndProc（100ms tick）
bool balloonHandler(const SDL_Event* event, void* user) {
    BalloonCtx& ui = *static_cast<BalloonCtx*>(user);
    Application& app = *ui.app;
    if (!event) {  // [RE 0x414C1D 0x401] 初始化
        std::memset(ui.balloons, 0, sizeof(ui.balloons));
        ui.countdown = kBalloonCountdown;
        ui.intro = kBalloonIntro;
        ui.score = 0;
        ui.gamePhase = 0;
        ui.slowdown = 0;
        ui.wind = 0;
        ui.state = 0;
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent:  // [RE 0x414C16 0x113]
        if (ui.state == 0) {
            if (--ui.intro <= 0) {
                ui.state = 1;  // [RE 0x414CA9 PostMessage 0x405]
            }
        } else if (ui.state == 1) {
            // [RE 0x414D60 0x405] sub_45144F(panel[78], 0, 0, 透明, 无音效) 阻塞播完
            if (!ui.titleOk || decodeFlcOnce(ui.title)) {
                ui.state = 2;
                app.cursor().select(9, 3, 5);  // [RE 0x414D8B] 枪光标（3 帧动画）
            }
        } else if (ui.state == 2) {
            if (ui.slowdown != 0) {
                --ui.slowdown;
            }
            if (ui.countdown != 0 && --ui.countdown == 0) {
                ui.gamePhase = 1;  // [RE 0x414CE0] 时间到
            }
            balloonTick(ui);
            if (ui.gamePhase == 2) {
                ui.state = 3;
                app.cursor().select(41, 1, 0);  // [RE 0x414D13] 恢复箭头
                ui.settleMs = SDL_GetTicks();
            }
        } else if (SDL_GetTicks() - ui.settleMs >= 2000) {
            // [RE 0x414D3D sub_45285E(0x7D0)] 大字停留 2s → postModalExit(得分)
            app.events().requestExit(ui.score);
            return true;
        }
        drawBalloon(ui);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:  // [RE 0x414BBC 0x201]
    case SDL_EVENT_MOUSE_BUTTON_UP:    // [RE 0x203 —— 按下/抬起各射一枪，原版 quirk]
        if (event->button.button == SDL_BUTTON_LEFT) {
            balloonClick(ui, static_cast<int>(event->button.x),
                         static_cast<int>(event->button.y));
        }
        return true;
    default:
        return true;  // 模态吞掉其余输入
    }
}

// [RE 0x4154DC] miniGameBalloonsRun：七彩氣球交互版；返回得分（<0 = 资源失败 → 调用方兜底）
// 依据: 0x4154DC 反编译（门控 0x4154FA、资源 panel[78 标题 FLC/79 数字/91 场景]、
//   音效表 g_balloonSoundTable={19,20,21}、musicPlayScene(11)、runModal balloonWndProc
//   0x414BBC 100ms）；玩法细节见本文件头注释与 docs/reverse/functions/41982d-p2-events.md §6
int miniGameBalloonsRun(Application& app) {
    GameState& st = app.gameState();
    auto blob78 = st.panel.read(78);  // [RE 0x415529] 标题 FLC
    auto blob79 = st.panel.read(79);  // [RE 0x415542] 数字
    auto blob91 = st.panel.read(91);  // [RE 0x41555C] 背景+气球
    if (!blob79 || !blob91) {
        RICH4_LOGW("minigame: panel.mkf[79/91] unavailable (RE 0x4154DC)");
        return -1;
    }
    BalloonCtx ui;
    ui.app = &app;
    if (!ui.digits.load(std::move(*blob79)) || ui.digits.frameCount() < 20 ||
        !ui.scene.load(std::move(*blob91)) || ui.scene.frameCount() < 14) {
        RICH4_LOGW("minigame: panel.mkf[79/91] invalid (RE 0x4154DC)");
        return -1;
    }
    if (blob78) {
        ui.titleOk = ui.title.open(std::move(*blob78));
    }
    app.audio().pushSceneMusic(11);  // [RE 0x415597] musicPlayScene(11)
    const int score = runModal(app, &balloonHandler, &ui, 100);  // [RE 0x4155A5]
    app.audio().resumeSceneMusic();  // [RE 0x4155AD] musicStackPopRestore
    RICH4_LOGI("minigame: balloons score=%d (RE 0x4154DC)", score);
    return score;
}

} // namespace

// [RE 0x415457] miniGameVisit：三种小游戏落地统一入口（case 6/7/8 =
//   miniGameDig 0x415215 / miniGameBalloons 0x4154DC / miniGameMoneyRain 0x4155FC；
//   调用方 landingEvent 0x41B152 做 g_playerPoints += 得分）。
// 依据: 0x415215/0x4154DC/0x4155FC 反编译；门控 0x41523A/0x4154FA/0x41561A
//   （alive==1 && byte_497159=settings[1] 動畫過程 → 交互版）；共享 else 0x415457
//   （rand()%20+50 + showMessage kFmtMiniPoints"得點券%d點" 2000ms + 台词
//   sub_44EF41(off_48084A=kValueLines 列 rand&1)）
void miniGameVisit(Application& app, int cellType) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        // 差异: 四大恶人（事件槽 4..7）忽略（原版亦随机得分）
        RICH4_LOGI("miniGame: cellType %d skip NPC p%d (差异: 四大恶人忽略)", cellType, p);
        return;
    }
    Player& pl = st.players[p];
    int score = -1;
    if (pl.alive == 1 && st.settings[1] != 0) {
        if (cellType == 7) {
            score = miniGameBalloonsRun(app);  // [RE 0x4154DC] 七彩氣球（已接入）
        }
        // [TODO] case 6 挖寶 / case 8 接錢 交互版接入后在此分支
    }
    if (score < 0) {
        // [RE 0x415457] 共享 else：score = rand()%20+50（50..69）
        score = std::rand() % 20 + 50;
        char text[64];
        std::snprintf(text, sizeof(text), "得點券%d點", score);  // [RE 0x463797 cp950]
        showMessage(app, text, 2000);                            // [RE 0x41548E]
        // [RE 0x4154B4..0x4154CF] 台词 off_48084A[char*27 + (rand&1)] = 列 0/1
        const int ci = pl.charIndex;
        if (ci >= 0 && ci < 12) {
            playLine(app, p, kValueLines[ci][std::rand() & 1]);
        }
    }
    pl.points = static_cast<uint16_t>(pl.points + score);  // [RE 0x41B152] += ax
    RICH4_LOGI("miniGame: cellType %d p%d score %d -> points %u (RE 0x415215/4DC/5FC)", cellType,
               p, score, pl.points);
}

} // namespace rich4
