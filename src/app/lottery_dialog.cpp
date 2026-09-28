#include "game/app/lottery.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/app/map_objects.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/debug_hooks.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/cursor.h"
#include "game/render/fli.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

constexpr uint32_t kTextBlack = 0x101010;
constexpr uint32_t kTextRed = 0xFF0000;
constexpr uint32_t kJackpotColor = 0x4F35B1; // [RE 0x43010C] setTextFont(20, 5191089, ...)
constexpr int kBetAmount = 1000;              // [RE 0x4315CC] 每注 1000 元
constexpr int kSfxBet = 31;                   // [RE 0x47566B] 投注成功
constexpr int kSfxRoll = 57;                  // [RE 0x47567B] 摇奖开始
constexpr int kSfxWin = 58;                   // [RE 0x475683] 中奖动画

// [RE 0x452793] 千分位（无 '$' 前缀）
void formatMoney(char* out, size_t outSize, int32_t value) {
    char digits[24];
    std::snprintf(digits, sizeof(digits), "%d", value);
    const int len = static_cast<int>(std::strlen(digits));
    size_t n = 0;
    for (int i = len; i > 0 && n + 2 < outSize; --i) {
        if (i % 3 == 0 && n > 0) {
            out[n++] = ',';
        }
        out[n++] = digits[len - i];
    }
    out[n] = '\0';
}

// [RE 0x450F04] 把 FLC 当前帧落到目标 (x, y)（原版直接解码到 backbuffer 的位置）
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

// 解码一帧；本次解码后到达末帧返回 true（原版 sub_450F04 帧计数 == 总数时返回 0 = 播完）
bool decodeFlcOnce(FliDecoder& flc) {
    if (!flc.valid()) {
        return true;
    }
    if (!flc.nextFrame()) {
        return true; // 已播完（或数据错误）
    }
    return flc.frameIndex() >= flc.frameCount();
}

// 循环模式解码：播完回绕到第 0 帧继续（原版 flcOpen flags&4 循环）
void decodeFlcLoop(FliDecoder& flc) {
    if (!flc.valid()) {
        return;
    }
    if (!flc.nextFrame()) {
        flc.rewind();
        flc.nextFrame();
    }
}

// [RE 0x456418] 数字精灵串：从右往左绘制（$ 符号帧 11 + 千分位数字帧 0-9/逗号帧 10）
// 原版步进：数字 18、逗号 6（先 +6 再 -12）、'$' 18
void drawMoneyDigits(Surface& dst, const UiImage& digits, int32_t value, int x, int y) {
    char buf[32];
    formatMoney(buf, sizeof(buf), value);
    for (int i = static_cast<int>(std::strlen(buf)) - 1; i >= 0; --i) {
        const char c = buf[i];
        if (c == ',') {
            blitElement(dst, digits.frame(10), x, y, false);
            x -= 6;
        } else if (c >= '0' && c <= '9') {
            blitElement(dst, digits.frame(c - '0'), x, y, false);
            x -= 18;
        }
    }
    blitElement(dst, digits.frame(11), x, y, false); // '$'（原版 chText[0] = 36）
}

// ==================== 投注界面（[RE 0x42F7FC]） ====================

// [RE 0x4755F8] 消息文本（#NNNN 前缀触发语音；BIG5 原文见 lottery-system.md）
const char* const kBuyMsg[6] = {
    "#0011哈囉！\n一券在手，\n希望無窮！",           // 0 开场
    "#0012只要一千元，\n就有獲得大獎\n的機會！",       // 1
    "#0013請圈選您的\n幸運號碼～",                     // 2 选号
    "#0014拜拜！祝您中獎！",                           // 3 购买完成
    "#0015太可惜了！\n您的現金不足～",                 // 4 现金不足
    "#0016下次再來吧！",                               // 5 退出
};

// [RE 0x42F7FC] 选号区几何：9 列 × 4 行，(30,271) 起，格 64×48
constexpr int kGridX0 = 30;
constexpr int kGridY0 = 271;
constexpr int kCellW = 64;
constexpr int kCellH = 48;
constexpr int kGridCols = 9;
constexpr int kGridRows = 4;

struct BuyCtx {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;  // panel.mkf[12]（10 帧）
    const UiImage* digits = nullptr; // panel.mkf[13]（12 帧）
    FliDecoder flc;                  // panel.mkf[14]（213×68，5 帧，循环 @(8,8)）
    bool flcOk = false;
    FloatMessage msg;
    int state = 0;          // [RE 0x48C370] 0..5
    uint32_t animFlags = 0; // [RE 0x48C350] 低 4 位动画型 / 高半字节进度
    int blinkTimer = 0;     // [RE 0x48C34C] 闪烁倒计时
    int blinkFrame = 5;
    // [RE 0x42F7FC 点击] 购买后画帧 7 红圈（原版点击时绘制一次，状态 5 表面保留；
    //   重写全量重绘需记录格号每 tick 补画）
    int markedCell = -1;
};

// [RE 0x42F7FC WM_TIMER 绘制] 底图 + 已售格变暗 + 帧1 + 装饰 + FLC + 标题/奖金
void drawBuy(BuyCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const UiImage& sheet = *ui.sheet;
    const UiImage& digits = *ui.digits;

    // [RE 0x42F32C] 底图 + 已售号码格变暗（-10 通道；格 (31+64c, 272+48r) 62×46）+ 帧 1 @(210,-5)
    blitElementOpaque(dst, sheet.frame(0), 0, 0);
    for (int n = 0; n < 36; ++n) {
        if (st.lotteryNumbers[n] != 0) {
            scaleSurfaceChannels(dst, 31 + kCellW * (n % kGridCols), 272 + kCellH * (n / kGridCols),
                                 62, 46, kChannelTen);
        }
    }
    blitElement(dst, sheet.frame(1), 210, -5, false);

    // [RE 0x42F7FC 点击绘制] 购买格的红圈标记（帧 7 58×47 off(28,25) 手绘圈）
    if (ui.markedCell >= 0) {
        const int col = ui.markedCell % kGridCols;
        const int row = ui.markedCell / kGridCols;
        blitElement(dst, sheet.frame(7), col * kCellW + 62, row * kCellH + kGridY0 + 24, false);
        if (ui.markedCell == 2 || ui.markedCell == 3) {
            // 原版 quirk：号码 3/4 格用帧 1 的 (0,268) 64×30 覆盖 (210,263)（色键区域 blit）
            blitElementRegion(dst, sheet.frame(1), 210, 263, 0, 268, 64, 30, false);
        }
    }

    // 装饰动画（状态 5 退出中跳过，对齐原版 goto LABEL_58）
    if (ui.state != 5) {
        if ((ui.animFlags & 0xF) == 0) {
            if ((std::rand() >> 10) == 0) { // [RE 0x42F7FC] 1/32 启动
                ui.animFlags |= 1;
            }
        } else if ((ui.animFlags & 0xF) == 1) {
            const int top = static_cast<int>((ui.animFlags & 0xF0) >> 4);
            if (top >= 3) {
                ui.animFlags &= ~0xFu; // 清除（画面元素不画）
            } else {
                static const uint8_t kCornerAnim[3] = {3, 4, 3}; // [RE 0x475660]
                blitElement(dst, sheet.frame(kCornerAnim[top]), 273, 63, false);
                ui.animFlags += 16;
            }
        }
        // 闪烁帧 5/6 @(273,105)（仅消息期间随机启动；倒计时结束清除）
        if (ui.msg.active() || ui.blinkTimer != 0) {
            if (ui.blinkTimer != 0) {
                if (--ui.blinkTimer > 0) {
                    blitElement(dst, sheet.frame(ui.blinkFrame), 273, 105, false);
                }
            } else if ((std::rand() >> 11) < 4) {
                ui.blinkFrame = 5 + (std::rand() & 1);
                ui.blinkTimer = std::rand() & 7;
                if (ui.blinkTimer == 0) {
                    ui.blinkTimer = 1;
                }
                blitElement(dst, sheet.frame(ui.blinkFrame), 273, 105, false);
            }
        }
    }

    // FLC 循环帧 @(8,8)（透明）
    if (ui.flcOk) {
        decodeFlcLoop(ui.flc);
        blitFlcFrame(dst, ui.flc, 8, 8, true);
    }

    // [RE 0x42F7FC LABEL_58] 帧 9 标题 @(28,27) + 奖金金额（原版仅首帧绘制一次，
    //   依赖表面保留；重写即时绘制每 tick 重画，视觉等价）
    blitElement(dst, sheet.frame(9), 28, 27, false);
    drawMoneyDigits(dst, digits, st.publicFund, 184, 41);
}

// [RE 0x42F7FC] 购买：记号码 + 扣钱 + 音效 + 进入退出流程（#0014）
void buyNumber(BuyCtx& ui, int cell) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    st.lotteryNumbers[cell] = static_cast<uint8_t>(p + 1);
    st.players[p].cash -= kBetAmount;
    st.publicFund += kBetAmount;
    ui.markedCell = cell;
    app.audio().playEffect(kSfxBet); // [RE 0x42FDD6] audioPlayEffect(dword_47566B, 0)
    ui.state = 5;
    ui.msg.show(app, kBuyMsg[3]);
    RICH4_LOGI("lottery: player %d bought No.%d cash=%d pool=%d (RE 0x42F7FC)", p, cell + 1,
               st.players[p].cash, st.publicFund);
}

// [RE 0x42F7FC] 投注界面窗口过程
bool buyHandler(const SDL_Event* event, void* user) {
    BuyCtx& ui = *static_cast<BuyCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (!event) { // [RE 0x42F9xx 0x401] 初始化
        ui.state = 0;
        ui.animFlags = 0;
        ui.blinkTimer = 0;
        ui.msg.setup(*ui.sheet, 8, 360, 20, 20, 0, kTextBlack, 0); // 帧 8 板 @(360,20)
        if (st.players[st.currentPlayer].cash >= kBetAmount) {
            if (st.settings[1] != 0) {
                ui.state = 1; // #0011 → #0012 → #0013
                ui.msg.show(app, kBuyMsg[0]);
            } else {
                ui.state = 3; // 直接选号
            }
        } else {
            ui.state = 4; // #0015 → #0016
            ui.msg.show(app, kBuyMsg[4]);
        }
        app.cursor().select(28, 1, 0); // [RE 0x42FA0A] cursorSelect(28,1,0)
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: { // [RE 0x42F7FC 0x113] 100ms tick
        const bool done = ui.msg.advance(app);
        if (done) {
            switch (ui.state) {
            case 1:
                ui.state = 2;
                ui.msg.show(app, kBuyMsg[1]);
                break;
            case 2:
                ui.state = 3;
                ui.msg.show(app, kBuyMsg[2]);
                break;
            case 4:
                ui.state = 5; // 钱不足收尾：#0016
                ui.msg.show(app, kBuyMsg[5]);
                break;
            case 5:
                app.cursor().select(41, 1, 0); // [RE 0x42F950] cursorSelect(41,1,0)
                app.events().requestExit(0);
                return true;
            default:
                break;
            }
        }
        drawBuy(ui);
        if (ui.msg.active()) {
            ui.msg.redraw(app);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: // [RE 0x42FBxx 0x201 / 0x205]
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (ui.state <= 3) {
                if (ui.state < 3) {
                    ui.msg.finish(app); // [RE 0x42FBe8] floatMsgAdvance(1) 跳过消息
                    ui.state = 3;
                }
                const int mx = static_cast<int>(event->button.x);
                const int my = static_cast<int>(event->button.y);
                // 原版闭区间 [30,606]×[271,463]（右/下边界越界为原版 quirk，重写保护格号 0..35）
                if (mx >= kGridX0 && mx <= kGridX0 + kGridCols * kCellW && my >= kGridY0 &&
                    my <= kGridY0 + kGridRows * kCellH) {
                    const int col = (mx - kGridX0) / kCellW;
                    const int row = (my - kGridY0) / kCellH;
                    const int cell = kGridCols * row + col;
                    if (cell >= 0 && cell < 36 && st.lotteryNumbers[cell] == 0) {
                        buyNumber(ui, cell);
                    }
                }
            }
            return true;
        }
        if (event->button.button == SDL_BUTTON_RIGHT) {
            if (ui.state < 5) { // [RE 0x42F7FC Msg==517] cancel 音 + PostMessage(0x406,5)
                app.audio().playEffect(4); // g_uiSoundCancel
                ui.state = 5;
                ui.msg.show(app, kBuyMsg[5]);
            }
            return true;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_ESCAPE && ui.state < 5) {
            app.audio().playEffect(4);
            ui.state = 5;
            ui.msg.show(app, kBuyMsg[5]);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// ==================== 开奖界面（[RE 0x43010C]） ====================

// [RE 0x475610] 开奖消息（#NNNN 前缀触发语音）
const char* const kDrawText1 = "#0017嗨！\n又到了每月\n十五號樂透\n開獎時間～";
const char* const kDrawText2 = "#0018現在馬上為您\n開出這一期的\n號碼．．。";
const char* const kDrawText3 = "#0019本月份的得主\n是．．．。";
const char* const kDrawText4 = "#0032恭喜您獨得\n所有獎金！";
const char* const kDrawText5 = "#0033SORRY！\n本月份沒有人\n得獎～";
const char* const kDrawText6 = "#0034獎金將累積\n到下個月．\n．．．。";
const char* const kDrawText7 = "#0035希望下次\n得獎者就\n是您！";
const char* const kDrawText8 = "#0036行動要快喔！";

enum DrawState {
    kDrawInit = 0,
    kDrawMsg1 = 1,     // #0017 开场
    kDrawMsg2 = 2,     // #0018 即将开奖
    kDrawRolling = 3,  // panel[16] 摇奖 FLC → 摇号
    kDrawWinner = 4,   // #0019 得主（画面保持基础 + 摇奖末帧）
    kDrawWinFlc = 5,   // panel[17] 中奖 FLC
    kDrawWinDone = 6,  // #0032 恭喜（画面同 5）
    kDrawNoWin = 7,    // #0033SORRY（板 23 @320,200）
    kDrawMsgEnd1 = 8,  // #0034 累积
    kDrawMsgEnd2 = 9,  // #0035 希望下次（板 22 @300,47）
    kDrawSettle = 10,  // #0036 行动要快 → 结算退出
};

struct DrawCtx {
    Application* app = nullptr;
    const UiImage* sheet = nullptr; // panel.mkf[15]（47 帧）
    const UiImage* digits = nullptr; // panel.mkf[13]（12 帧）
    const UiImage* tip = nullptr;    // [RE 0x48BAD8] data.mkf[517]（小数字帧 8..17）
    FliDecoder rollFlc;              // panel.mkf[16]（275×270，42 帧，不透明 @183,75）
    FliDecoder winFlc;               // panel.mkf[17]（280×480，37 帧，透明 @205,0）
    bool rollOk = false;
    bool winOk = false;
    bool rollStarted = false;
    FloatMessage msg;
    int state = kDrawInit;  // [RE 0x48C37B]
    int timer = 0;          // [RE 0x48C37C]
    int winner = 0;         // [RE 0x48C377] 中奖玩家+1（0=无人）
    int number = 0;         // 开出号码 1..36
    bool numberDrawn = false;
    int waitTicks = 0;      // [RE 0x45285E(500)] 无人中奖 500ms 延时（50ms tick）
    bool pendingNoWin = false;
    // 人物动画（[RE 0x48C350] 位域拆分）
    int animType = 0; // 低 4 位：1/2 = 右/左人物眨眼 4 帧序列；3/4 = 一次性眨眼
    int animStep = -1; // 帧进度：-1=空闲 / 0=启动待处理 / 0..3=序列帧（type 1/2）
    // [RE 0x48C350 bit12/bit13] 上次选择锁存：type 1/2 眨眼完成时置位（原版只置不清，
    //   首次完成前为 false）；type 3/4 随机值与之相同 → 本 tick 不重绘（表面保留等价）
    bool eyeLatchRight = false; // 0x1000
    bool eyeLatchLeft = false;  // 0x2000
    // 眼睛覆盖帧持久记录（原版表面保留：画一次后持续显示直到人物被状态重绘覆盖）
    int eyeFrameRight = 0; // 0=无；7-10 @512,102
    int eyeFrameLeft = 0;  // 0=无；14-17 @52,89
    int mouthTimer = 0; // [RE 0x48C34C] 嘴部倒计时（张嘴持续 1..15 tick）
    int mouthFrame = 0; // [RE 0x48C34C 覆盖帧] 0 = 底图闭嘴；11 闭嘴 / 12·13 张嘴
};

// [RE 0x43010C] 人物眨眼帧序（[RE 0x475663] 右人物 @512,102 / [RE 0x475667] 左人物 @52,89）
const uint8_t kAnimSeqRight[4] = {8, 7, 8, 10};
const uint8_t kAnimSeqLeft[4] = {16, 17, 16, 15};

// [RE 0x43010C] 人物动画状态机（状态 4..8 跳过；原版 dword_48C350 case 0..4 位域）
void updateAnim(DrawCtx& ui) {
    if (ui.state >= kDrawWinner && ui.state <= kDrawMsgEnd1) {
        return;
    }
    switch (ui.animType) {
    case 0: {
        const int r = std::rand() >> 9; // 0..63
        if (r == 0) {
            ui.animType = 1; // 右人物眨眼 4 帧
            ui.animStep = -1;
        } else if (r == 1) {
            ui.animType = 2; // 左人物眨眼 4 帧
            ui.animStep = -1;
        } else if (r < 4) { // 2..3
            ui.animType = 3; // 右眼一次性（下一 tick 随机帧 + 锁存判断，见 drawAnim）
            ui.animStep = 0;
        } else if (r < 6) { // 4..5
            ui.animType = 4; // 左眼一次性
            ui.animStep = 0;
        }
        break;
    }
    default: // type 1/2 序列推进与 type 3/4 随机/锁存/绘制均在 drawAnim
        break;
    }
    // [RE 0x48C34C] 嘴部动画（仅在消息显示期间活动；随机张嘴帧 12/13 持续 1..15 tick，
    //   倒计时归零的 tick 画闭嘴帧 11；停机后保留最后口型 = 原版表面保留）
    if (ui.msg.active() || ui.mouthTimer != 0) {
        if (ui.mouthTimer != 0) {
            --ui.mouthTimer;
            if (ui.mouthTimer == 0) {
                ui.mouthFrame = 11; // 闭嘴帧
            }
        } else if ((std::rand() >> 11) < 4) {
            ui.mouthFrame = 12 + (std::rand() & 1); // 张嘴帧 12/13
            ui.mouthTimer = std::rand() & 0xF;
            if (ui.mouthTimer == 0) {
                ui.mouthTimer = 1;
            }
        }
    }
}

// [RE 0x43010C] 人物动画绘制（在状态画面之后叠加；状态 4..8 跳过且倒计时冻结）
//   原版靠表面保留：眨眼/一次性覆盖帧画一次后**持续存在**（直到人物被状态重绘覆盖）；
//   重写全量重绘 → 用 eyeFrameRight/Left 持久记录并每 tick 补画（同 mouthFrame 模式）
void drawAnim(DrawCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const UiImage& sheet = *ui.sheet;
    if (ui.state >= kDrawWinner && ui.state <= kDrawMsgEnd1) {
        return;
    }
    if (ui.animType == 1 || ui.animType == 2) {
        // 眨眼 4 帧序列（右 8,7,8,10 @512,102 / 左 16,17,16,15 @52,89）；
        //   启动 tick 不更新覆盖帧（保留旧画面，对齐原版 case0 选型→case1 下一 tick 绘制）
        if (ui.animStep >= 0) {
            const int f = ui.animStep & 3;
            if (ui.animType == 1) {
                ui.eyeFrameRight = kAnimSeqRight[f];
            } else {
                ui.eyeFrameLeft = kAnimSeqLeft[f];
            }
        }
        if (++ui.animStep >= 4) {
            if (ui.animType == 1) {
                ui.eyeLatchRight = true; // [RE 0x430E96] BYTE1 |= 0x10（只置不清）
            } else {
                ui.eyeLatchLeft = true; // [RE 0x430CED] BYTE1 |= 0x20
            }
            ui.animType = 0;
            ui.animStep = -1;
        }
    } else if (ui.animType == 3) {
        // 右眼一次性（原版 case3）：启动 tick 不处理；下一 tick 随机帧，与锁存位相同则不重绘
        if (ui.animStep == 0) {
            ui.animStep = 1;
        } else {
            const int pick = std::rand() & 1;
            if (pick != (ui.eyeLatchRight ? 1 : 0)) {
                ui.eyeFrameRight = 9 + pick;
            }
            ui.animType = 0;
            ui.animStep = -1;
        }
    } else if (ui.animType == 4) {
        // 左眼一次性（原版 case4）：同上（帧 14/15 @52,89）
        if (ui.animStep == 0) {
            ui.animStep = 1;
        } else {
            const int pick = std::rand() & 1;
            if (pick != (ui.eyeLatchLeft ? 1 : 0)) {
                ui.eyeFrameLeft = 14 + pick;
            }
            ui.animType = 0;
            ui.animStep = -1;
        }
    }
    // 持久覆盖帧补画（原版表面保留）
    if (ui.eyeFrameRight != 0) {
        blitElement(dst, sheet.frame(ui.eyeFrameRight), 512, 102, false);
    }
    if (ui.eyeFrameLeft != 0) {
        blitElement(dst, sheet.frame(ui.eyeFrameLeft), 52, 89, false);
    }
    if (ui.mouthFrame != 0) {
        blitElement(dst, sheet.frame(ui.mouthFrame), 512, 119, false);
    }
}

// [RE 0x42F6C3 / 0x43010C case 4/6] 「累積獎金」+ $金额（win 布局 = (91,19)/(91,56)）
void drawJackpotText(DrawCtx& ui, bool winLayout) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const int x = winLayout ? 91 : 77;
    const int y = winLayout ? 19 : 193;
    app.text().setFont(20, kJackpotColor, 0, kTextStyleBold, 0);
    app.text().drawText(dst, "累積獎金", x, y, 2);
    app.text().setFont(20, kTextRed, 0, kTextStyleBold, 0);
    char buf[32];
    char money[28];
    formatMoney(money, sizeof(money), app.gameState().publicFund);
    std::snprintf(buf, sizeof(buf), "$%s", money);
    app.text().drawText(dst, buf, x, y + (winLayout ? 37 : 35), 2);
}

// [RE 0x43010C] 大数字（panel[15] 帧 37..46 = '0'..'9'）@(286,405)/(358,405)
void drawBigNumber(DrawCtx& ui) {
    if (!ui.numberDrawn) {
        return;
    }
    const UiImage& sheet = *ui.sheet;
    blitElement(ui.app->surface(), sheet.frame(37 + ui.number / 10), 286, 405, false);
    blitElement(ui.app->surface(), sheet.frame(37 + ui.number % 10), 358, 405, false);
}

// [RE 0x43010C] 提示框小数字（g_tipFrame 帧 8..17）@(300,220)/(340,220)
void drawSmallNumber(DrawCtx& ui) {
    if (!ui.numberDrawn || ui.tip == nullptr || ui.tip->frameCount() < 18) {
        return;
    }
    Surface& dst = ui.app->surface();
    blitElement(dst, ui.tip->frame(8 + ui.number / 10), 300, 220, false);
    blitElement(dst, ui.tip->frame(8 + ui.number % 10), 340, 220, false);
}

// [RE 0x42F417] 玩家号码列表：4 区 (16,340)/(16,410)/(328,340)/(328,410) 296×60；
//   区域 -16 变暗 → 头像帧 25+charIndex @(x+20,y+30) → 号码数字（每 40px，>6 个换行）
void drawPlayerNumbers(DrawCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    static const int kPos[4][2] = {{16, 340}, {16, 410}, {328, 340}, {328, 410}}; // [RE 0x42F30C]
    const UiImage& digits = *ui.digits;
    int slot = 0;
    for (int p = 0; p < st.playerCount && p < 4; ++p) {
        const Player& pl = st.players[p];
        if (pl.alive == 0) {
            continue;
        }
        const int x = kPos[slot][0];
        const int y = kPos[slot][1];
        scaleSurfaceChannels(dst, x, y, 296, 60, kChannelHalf); // [RE 0x4552E7 -16]
        const int avatarFrame = 25 + pl.charIndex;
        if (avatarFrame < ui.sheet->frameCount()) {
            blitElement(dst, ui.sheet->frame(avatarFrame), x + 20, y + 30, false);
        }
        char list[80] = {}; // 原版 v12[76]；36 号全买 = 72 字符
        for (int i = 0; i < 36; ++i) {
            if (st.lotteryNumbers[i] == p + 1) {
                char num[4];
                std::snprintf(num, sizeof(num), "%02d", i + 1);
                std::strncat(list, num, sizeof(list) - std::strlen(list) - 1);
            }
        }
        const int len = static_cast<int>(std::strlen(list));
        if (len > 12) {
            const int n = std::min(len, 24) & ~1;
            int px = x + 54;
            int py = y + 15;
            for (int i = 0; i < n; i += 2) {
                blitElement(dst, digits.frame(list[i] - '0'), px, py, false);
                blitElement(dst, digits.frame(list[i + 1] - '0'), px + 16, py, false);
                if (i == 10) {
                    px = x + 14;
                    py += 30;
                }
                px += 40;
            }
        } else if (len > 0) {
            int px = x + 54;
            for (int i = 0; i < len; i += 2) {
                blitElement(dst, digits.frame(list[i] - '0'), px, y + 30, false);
                blitElement(dst, digits.frame(list[i + 1] - '0'), px + 16, y + 30, false);
                px += 40;
            }
        }
        ++slot;
    }
}

// [RE 0x43010C] 开奖画面（按状态全量重绘；原版依赖表面保留的累积绘制在此复原）
void drawDraw(DrawCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const UiImage& sheet = *ui.sheet;
    const int s = ui.state;
    blitElementOpaque(dst, sheet.frame(0), 0, 0);

    const bool baseLike = (s <= kDrawRolling) || (s == kDrawWinner);
    if (baseLike) {
        // 基础：帧 0 + 帧 3 左板 @(7,66) + 帧 1/2 右板 + 标题/金额 + 摇奖 FLC → 号码 → 玩家列表
        blitElement(dst, sheet.frame(3), 7, 66, false);
        if (s >= kDrawMsg2) {
            blitElement(dst, sheet.frame(2), 418, 66, false);
        } else {
            blitElement(dst, sheet.frame(1), 472, 66, false);
        }
        drawJackpotText(ui, false);
        if (ui.rollStarted && ui.rollOk && ui.rollFlc.frameIndex() > 0) {
            blitFlcFrame(dst, ui.rollFlc, 183, 75, false); // 不透明模式（末帧保留至摇号后）
        }
        if (s >= kDrawMsg2) {
            drawSmallNumber(ui);
            drawBigNumber(ui);
        }
        drawPlayerNumbers(ui);
    } else if (s == kDrawWinFlc) {
        // 中奖演出：帧 6 @(0,0) + 帧 5 @(505,66) + 中奖框帧 24 @(320,200) + 文字/角色名 + FLC
        blitElement(dst, sheet.frame(6), 0, 0, false);
        blitElement(dst, sheet.frame(5), 505, 66, false);
        blitElement(dst, sheet.frame(24), 320, 200, false);
        drawJackpotText(ui, true);
        if (ui.winner >= 1 && ui.winner <= 4) {
            const Player& w = app.gameState().players[ui.winner - 1];
            app.text().setFont(28, kTextRed, 0x400000, kTextStyleShadow | kTextStyleBold, 4);
            app.text().drawText(dst, w.name != nullptr ? w.name : "", 320, 180, 2);
        }
        drawBigNumber(ui);
        drawPlayerNumbers(ui);
        if (ui.winOk && ui.winFlc.frameIndex() > 0) {
            blitFlcFrame(dst, ui.winFlc, 205, 0, true); // 透明模式
        }
    } else if (s == kDrawWinDone) {
        // [RE 0x43010C 状态 5 播完] 重铺 (16,340)+(150,0,330,360) → 帧 6/5 底部重画；
        //   帧 24/角色名被擦除，文字 (91,19) 保留
        blitElement(dst, sheet.frame(6), 0, 0, false);
        blitElement(dst, sheet.frame(5), 505, 66, false);
        blitElementRegion(dst, sheet.frame(6), 0, 340, 0, 340, 162, 120, false);
        blitElementRegion(dst, sheet.frame(5), 505, 340, 0, 274, 124, 130, false);
        drawJackpotText(ui, true);
        drawBigNumber(ui);
        drawPlayerNumbers(ui);
    } else if (s == kDrawNoWin) {
        // 无人中奖：帧 3 完整（原版 state3 主体保留 + 底部重铺后重画腿部，全量重绘取完整）
        //   + 摇奖 FLC 末帧保留 + 大数字 + 帧 4 @(489,116)（右人物哭泣）+ 帧 21 @(52,89)
        //   （左人物尴尬表情覆盖）+ 玩家列表（「累積獎金」文字保留自 INIT；板 23 由 FloatMessage 绘制）
        blitElement(dst, sheet.frame(3), 7, 66, false);
        if (ui.rollStarted && ui.rollOk && ui.rollFlc.frameIndex() > 0) {
            blitFlcFrame(dst, ui.rollFlc, 183, 75, false); // 原版重铺不覆盖 FLC 区
        }
        drawJackpotText(ui, false);
        drawSmallNumber(ui);
        drawBigNumber(ui);
        blitElement(dst, sheet.frame(4), 489, 116, false);
        blitElement(dst, sheet.frame(21), 52, 89, false);
        drawPlayerNumbers(ui);
    } else { // kDrawMsgEnd1 / kDrawMsgEnd2 / kDrawSettle
        if (ui.winner != 0) {
            // 中奖路径：帧 3 完整保留（FLC 已被 state5 末尾重铺擦除）；
            //   「累積獎金」文字由 case6 重画 (77,193)（人物手持板），case8 重铺区不含该处 → 保留
            blitElement(dst, sheet.frame(3), 7, 66, false);
            blitElement(dst, sheet.frame(1), 472, 66, false);
            drawJackpotText(ui, false);
            drawBigNumber(ui);
            drawPlayerNumbers(ui);
        } else {
            // 无人路径：帧 3 完整（主体保留）+ 摇奖 FLC 末帧 + 文字（保留自 INIT）+ 小数字 +
            //   帧 1 @(472,66)（右人物恢复常态；case8 重铺擦掉帧 4 后重画）
            blitElement(dst, sheet.frame(3), 7, 66, false);
            if (ui.rollStarted && ui.rollOk && ui.rollFlc.frameIndex() > 0) {
                blitFlcFrame(dst, ui.rollFlc, 183, 75, false);
            }
            drawJackpotText(ui, false);
            drawSmallNumber(ui);
            blitElement(dst, sheet.frame(1), 472, 66, false);
            drawBigNumber(ui);
            drawPlayerNumbers(ui);
        }
    }
    drawAnim(ui);
}

// [RE 0x43010C 状态 3] 摇号：统计每玩家注数；全 ≤10 → 随机 1..36，否则从已售号中随机
void drawNumber(DrawCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    int counts[9] = {};
    int sold[36];
    int soldCount = 0;
    for (int i = 0; i < 36; ++i) {
        const int owner = st.lotteryNumbers[i];
        if (owner != 0) {
            ++counts[owner];
            sold[soldCount++] = i + 1;
        }
    }
    bool allLow = true;
    for (int k = 1; k <= 8; ++k) {
        if (counts[k] > 10) {
            allLow = false;
        }
    }
    int num;
    if (allLow || soldCount == 0) {
        num = dbg::roll(dbg::SlotLottery, 36) + 1;
    } else {
        num = sold[dbg::roll(dbg::SlotLottery, soldCount)];
    }
    ui.number = num;
    ui.numberDrawn = true;
    const int owner = st.lotteryNumbers[num - 1];
    RICH4_LOGI("lottery draw: No.%d owner=%d sold=%d low=%d (RE 0x43010C)", num, owner, soldCount,
               allLow ? 1 : 0);
    if (owner != 0) {
        ui.winner = owner;
        ui.state = kDrawWinner;
        ui.msg.show(app, kDrawText3); // #0019
    } else {
        ui.winner = 0;
        ui.pendingNoWin = true;
        ui.waitTicks = 10; // [RE 0x45285E(500)]
    }
}

// [RE 0x43010C case 10] 结算：中奖者得全部奖金（入现金）→ 清池清号；无人中奖仅退出（奖金累积）
void settleDraw(DrawCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (ui.winner != 0) {
        addMoney(app, ui.winner - 1, st.publicFund, true); // [RE 0x430AE5]
        st.publicFund = 0;
        std::memset(st.lotteryNumbers, 0, sizeof(st.lotteryNumbers));
    }
    RICH4_LOGI("lottery settle: winner=%d pool left=%d (RE 0x43010C)", ui.winner, st.publicFund);
}

// [RE 0x43010C] 开奖界面窗口过程
bool drawHandler(const SDL_Event* event, void* user) {
    DrawCtx& ui = *static_cast<DrawCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (!event) { // [RE 0x42F6C3 + 0x43010C 0x401] 初始化
        ui.timer = 0;
        ui.msg.setup(*ui.sheet, 22, 300, 47, -10, 0, kTextBlack, 0); // 帧 22 板 @(300,47)
        if (st.settings[1] != 0) {
            ui.state = kDrawMsg1;
            ui.msg.show(app, kDrawText1);
        } else {
            ui.state = kDrawMsg2;
        }
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: { // [RE 0x43010C 0x113] 50ms tick
        const bool done = ui.msg.advance(app);
        if (ui.waitTicks > 0) {
            --ui.waitTicks;
            if (ui.waitTicks == 0 && ui.pendingNoWin) {
                ui.pendingNoWin = false;
                ui.state = kDrawNoWin;
                // state7 重铺右侧 + 帧 4/帧 21 重画人物 → 原版表面上的眼睛覆盖消失
                ui.eyeFrameRight = 0;
                ui.eyeFrameLeft = 0;
                ui.msg.setup(*ui.sheet, 23, 320, 200, 0, 0, kTextBlack, 0); // 帧 23 板 @(320,200)
                ui.msg.show(app, kDrawText5); // #0033SORRY
            }
        } else if (done) {
            switch (ui.state) {
            case kDrawMsg1:
                ui.state = kDrawMsg2;
                ui.msg.show(app, kDrawText2);
                break;
            case kDrawMsg2:
                ui.state = kDrawRolling;
                ui.timer = 0;
                ui.rollStarted = true;
                ui.eyeFrameRight = 0; // case2 帧 2@(418,66) 重画右人物 → 右眼覆盖消失
                if (ui.rollOk) {
                    ui.rollFlc.rewind();
                }
                app.audio().playEffect(kSfxRoll); // [RE 0x430473] audioPlayEffect(dword_47567B, 0)
                break;
            case kDrawWinner:
                ui.state = kDrawWinFlc;
                ui.timer = 0;
                ui.eyeFrameRight = 0; // 中奖演出换帧 6/5 → 覆盖消失
                ui.eyeFrameLeft = 0;
                if (ui.winOk) {
                    ui.winFlc.rewind();
                }
                app.audio().playEffect(kSfxWin); // [RE 0x4308xx] audioPlayEffect(dword_475683, 0)
                break;
            case kDrawWinDone:
                ui.state = kDrawMsgEnd1;
                ui.eyeFrameRight = 0; // state8 重画帧 1/3 → 覆盖消失
                ui.eyeFrameLeft = 0;
                break;
            case kDrawNoWin:
                ui.state = kDrawMsgEnd1;
                ui.eyeFrameRight = 0;
                ui.eyeFrameLeft = 0;
                ui.msg.show(app, kDrawText6); // #0034
                break;
            case kDrawMsgEnd1:
                ui.state = kDrawMsgEnd2;
                ui.msg.setup(*ui.sheet, 22, 300, 47, -10, 0, kTextBlack, 0);
                ui.msg.show(app, kDrawText7); // #0035
                break;
            case kDrawMsgEnd2:
                ui.state = kDrawSettle;
                ui.msg.show(app, kDrawText8); // #0036
                break;
            case kDrawSettle:
                settleDraw(ui);
                app.events().requestExit(0);
                return true;
            default:
                break;
            }
        }
        // 状态 3：等 20 tick（1s）后每 tick 解码摇奖 FLC，播完摇号
        if (ui.state == kDrawRolling && !ui.numberDrawn) {
            ++ui.timer;
            if (ui.timer >= 20) {
                if (decodeFlcOnce(ui.rollFlc)) {
                    drawNumber(ui);
                }
            }
        }
        // 状态 5：31 tick 后每 tick 解码中奖 FLC，播完 → #0032
        if (ui.state == kDrawWinFlc) {
            ++ui.timer;
            if (ui.timer > 30 && decodeFlcOnce(ui.winFlc)) {
                ui.state = kDrawWinDone;
                ui.msg.show(app, kDrawText4); // #0032
            }
        }
        updateAnim(ui);
        drawDraw(ui);
        if (ui.msg.active()) {
            ui.msg.redraw(app);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: // 原版开奖界面无点击交互；左键跳过消息（现代化便利）
    case SDL_EVENT_KEY_DOWN: {
        const bool isClick = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                             event->button.button == SDL_BUTTON_LEFT;
        const bool isSkip = event->type == SDL_EVENT_KEY_DOWN &&
                            (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_RETURN ||
                             event->key.key == SDLK_SPACE);
        if ((isClick || isSkip) && ui.msg.active() && ui.waitTicks == 0) {
            ui.msg.finish(app); // [RE 0x4301xx] floatMsgAdvance(1) 点击跳过
            return true;
        }
        return isClick || isSkip;
    }
    default:
        break;
    }
    return false;
}

} // namespace

// ==================== 入口 ====================

// [RE 0x4315CC] 乐透格落地
void lotteryVisit(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    if (pl.alive == 1) {
        // 人类：投注界面
        auto blob12 = st.panel.read(12);
        auto blob13 = st.panel.read(13);
        auto blob14 = st.panel.read(14);
        if (!blob12 || !blob13 || !blob14) {
            RICH4_LOGW("lottery: panel.mkf[12/13/14] unavailable (RE 0x4315CC)");
            return;
        }
        BuyCtx ui;
        ui.app = &app;
        UiImage sheet;
        UiImage digits;
        if (!sheet.load(std::move(*blob12)) || sheet.frameCount() < 10) {
            RICH4_LOGW("lottery: panel.mkf[12] invalid (RE 0x4315CC)");
            return;
        }
        if (!digits.load(std::move(*blob13)) || digits.frameCount() < 12) {
            RICH4_LOGW("lottery: panel.mkf[13] invalid (RE 0x4315CC)");
            return;
        }
        ui.sheet = &sheet;
        ui.digits = &digits;
        ui.flcOk = ui.flc.open(std::move(*blob14));
        if (!ui.flcOk) {
            RICH4_LOGW("lottery: panel.mkf[14] FLC invalid (RE 0x4315CC)");
        }
        app.audio().pushSceneMusic(6); // [RE 0x431644] musicPlayScene(6)
        runModal(app, &buyHandler, &ui, 100); // [RE 0x42F7FC] SetTimer 100ms
        app.audio().resumeSceneMusic(); // [RE 0x43165B] musicStackPopRestore
        return;
    }
    // AI/托管：现金 > 1000（原版严格大于；人类面为 >= 1000）→ 随机未售号自动投注
    if (pl.cash > kBetAmount) {
        int freeCell[36];
        int n = 0;
        for (int i = 0; i < 36; ++i) {
            if (st.lotteryNumbers[i] == 0) {
                freeCell[n++] = i;
            }
        }
        if (n > 0) {
            const int cell = freeCell[dbg::roll(dbg::SlotLottery, n)];
            st.lotteryNumbers[cell] = static_cast<uint8_t>(p + 1);
            pl.cash -= kBetAmount;
            st.publicFund += kBetAmount;
            RICH4_LOGI("lottery AI: player %d auto-bought No.%d cash=%d (RE 0x4315CC)", p, cell + 1,
                       pl.cash);
        }
    }
}

// [RE 0x431712] 15 号开奖（紧接分红之后调用）
void lotteryDrawMeeting(Application& app) {
    GameState& st = app.gameState();
    bool any = false;
    for (int i = 0; i < 36; ++i) {
        if (st.lotteryNumbers[i] != 0) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }
    auto blob15 = st.panel.read(15);
    auto blob13 = st.panel.read(13);
    if (!blob15 || !blob13) {
        RICH4_LOGW("lottery draw: panel.mkf[15/13] unavailable (RE 0x431712)");
        return;
    }
    DrawCtx ui;
    ui.app = &app;
    UiImage sheet;
    UiImage digits;
    if (!sheet.load(std::move(*blob15)) || sheet.frameCount() < 47) {
        RICH4_LOGW("lottery draw: panel.mkf[15] invalid (RE 0x431712)");
        return;
    }
    if (!digits.load(std::move(*blob13)) || digits.frameCount() < 12) {
        RICH4_LOGW("lottery draw: panel.mkf[13] invalid (RE 0x431712)");
        return;
    }
    ui.sheet = &sheet;
    ui.digits = &digits;
    const UiImage& tip = st.estateTiles; // [RE 0x48BAD8] g_tipFrame = data.mkf[517]
    if (tip.frameCount() < 18) {
        RICH4_LOGW("lottery draw: tip frames %d < 18 (RE 0x48BAD8)", tip.frameCount());
    } else {
        ui.tip = &tip;
    }
    auto blob16 = st.panel.read(16);
    auto blob17 = st.panel.read(17);
    if (blob16) {
        ui.rollOk = ui.rollFlc.open(std::move(*blob16));
    }
    if (blob17) {
        ui.winOk = ui.winFlc.open(std::move(*blob17));
    }
    if (!ui.rollOk || !ui.winOk) {
        RICH4_LOGW("lottery draw: FLC panel.mkf[16/17] invalid (RE 0x431712)");
    }
    app.audio().pushSceneMusic(8); // [RE 0x43175A] musicPlayScene(8)
    runModal(app, &drawHandler, &ui, 50); // [RE 0x43010C] SetTimer 50ms
    app.audio().resumeSceneMusic(); // [RE 0x431763] musicStackPopRestore
}

} // namespace rich4
