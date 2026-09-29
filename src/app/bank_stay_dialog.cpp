#include "game/app/bank_stay_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/date_util.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/number_input_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/app/debug/debug.h"
#include <cstdio>
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/raw_bitmap.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// ===== panel.mkf[23]（SMP 24 帧，实测帧表见 docs/reverse/functions/bank-system.md §3）=====
enum StayFrame : int {
    kFrameBg = 0,       // 640x480 银行大堂（含申请/偿还牌位、EXIT 常态图）
    kFrameCurtain = 1,  // 344x240 关闭的百叶帘 @anchor(62,198)（非自家银行覆盖）
    kFrameOwn = 2,       // 640x480 经理室（週轉現金子模态背景）
    kFrameEye3 = 3,      // 80x40 柜员眼部动画（随机动作 3/4/5）
    kFrameEye6 = 6,      // 80x40 柜员眼部循环（7,6,7,5）
    kFrameSmall8 = 8,    // 80x30 柜员下窗静止
    kFrameSmall9 = 9,    // 79x30 柜员下窗动作
    kFrameSmall10 = 10,  // 80x30 柜员下窗动作
    kFrameAdvEye11 = 11, // 70x35 週轉眼部循环（12,11,12）
    kFrameAdvSmall13 = 13, // 66x25 週轉下窗
    kFramePanelTop = 15,  // 200x280 右上玩家信息面板
    kFrameBtnWide = 16,   // 114x40 週轉現金/歸還款項 常态
    kFrameBtnWideDown = 17,
    kFrameBtnExit = 18,  // 80x40 離開（EXIT）常态
    kFrameBtnExitDown = 19,
    kFramePanelLeft = 20, // 137x165 左侧客户存款/融资信息面板
    kFrameMsgBoard = 21,  // 195x142 主 UI 浮动消息板
    kFrameAdvMsgBoard = 22, // 250x110 週轉浮动消息板
    kFrameBan = 23,      // 29x29 禁令标记 @anchor(14,14)（暫停放款中）
};

// 位置/命中区（原版坐标，见 bank-system.md §3.2）
constexpr int kCurtainX = 320, kCurtainY = 240;                  // 帧1 anchor(62,198) → 落点(258,42)
constexpr int kApplyLabelX = 345, kApplyLabelY = 345;            // [RE 0x4341A8]
constexpr int kRepayLabelX = 530, kRepayLabelY = 345;            // [RE 0x4341CA]
constexpr int kBanOnApplyX = 345, kBanOnApplyY = 345;            // [RE 0x43420A]
constexpr int kBanOnTurnX = 67, kBanOnTurnY = 324;               // [RE 0x434333]
constexpr int kNoteX = 492, kNoteY = 245;                        // 灰字「特別融資」[RE 0x4343ED]
constexpr int kPanelX = 440;                                     // 右侧双面板滑入终点
constexpr int kPanelTopH = 280;
constexpr int kHitExit[4] = {548, 431, 628, 471};                // [RE 0x4757F8]
constexpr int kHitApply[4] = {282, 324, 408, 366};
constexpr int kHitRepay[4] = {470, 326, 590, 366};
constexpr int kHitCenter[4] = {268, 51, 591, 273};               // 中央帘子区（自家→週轉現金）
constexpr int kEyeX = 140, kEyeY = 114, kEyeW = 80, kEyeH = 40;  // [RE 0x4358B2]
constexpr int kSmallX = 140, kSmallY = 152, kSmallW = 80, kSmallH = 30;
constexpr int kMsgBoardX = 240, kMsgBoardY = 80;                 // [RE 0x434462 floatMsgSetup]

// 週轉現金子模态（sub_434492）
constexpr int kAdvPanelLeftX = 10, kAdvPanelLeftY = 125;
constexpr int kAdvBtnX = 11, kAdvBtnTurnY = 305, kAdvBtnBackY = 362, kAdvBtnLeaveY = 419;
constexpr int kAdvEyeX = 496, kAdvEyeY = 162, kAdvEyeW = 70, kAdvEyeH = 35;
constexpr int kAdvSmallX = 496, kAdvSmallY = 197, kAdvSmallW = 66, kAdvSmallH = 25;
constexpr int kAdvMsgX = 214, kAdvMsgY = 50;
constexpr int kAdvHitTurn[4] = {11, 305, 125, 345};              // [RE 0x475818]
constexpr int kAdvHitBack[4] = {11, 362, 125, 402};
constexpr int kAdvHitLeave[4] = {11, 419, 91, 459};
constexpr int kAdvNumX = 128;                                    // 数字右对齐
constexpr int kAdvNumY[3] = {163, 211, 259};                     // 额度/已週轉/剩余

// 音效：UI（Effect.mkf 索引）—— g_uiSoundClick=1、Cancel=4
constexpr int kSfxClick = 1;
constexpr int kSfxCancel = 4;

// [RE 0x475880] 柜员眼部循环帧序（(bits&0x30)>>4 档）
constexpr int kClerkCycle[4] = {7, 6, 7, 5};
// [RE 0x475884] 週轉眼部循环帧序
constexpr int kAdvCycle[3] = {12, 11, 12};

// [RE 0x452793] 千分位金额格式化（同 game_panel.cpp 实现）
void formatMoney(char* out, int32_t value) {
    char digits[24];
    std::snprintf(digits, sizeof(digits), "%d", value);
    const int len = static_cast<int>(std::strlen(digits));
    int n = 0;
    for (int i = len; i > 0; --i) {
        if (i % 3 == 0 && n > 0) {
            out[n++] = ',';
        }
        out[n++] = digits[len - i];
    }
    out[n] = '\0';
}

// [RE 0x433C20/0x433D6E] 面板金额 = '$' + 千分位（原版 chText = '$'; sub_452793(&chText+1, v)）
void formatMoneyDollar(char* out, int32_t value) {
    out[0] = '$';
    formatMoney(out + 1, value);
}

// ===== 日历工具（[RE 0x4520A6]/0x4521F0 简化版，同 game_panel.cpp）=====

int daysInMonth(int year, int month) {
    static const int days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4) == 0) {
        return 29;
    }
    return days[month];
}

int firstWeekday(int year, int month) {
    int total = 0;
    for (int y = 1998; y < year; ++y) {
        total += (y % 4) ? 365 : 366;
    }
    for (int m = 1; m < month; ++m) {
        total += daysInMonth(year, m);
    }
    return (total + 4) % 7;
}

// [RE 0x4521F0] 节日查询（type 0 固定 / 1 浮动农历 / 2 第 N 个星期X）
int findHoliday(const GameState& st, uint32_t date) {
    const int base = 4 * st.gameMode + st.mapIndex;
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    const int v2 = (month << 8) | day;
    const int32_t days = daysSince1998(date);
    for (int i = 0; i < 24; ++i) {
        const uint8_t* h = kHoliday[base][i];
        const int type = h[1];
        int a1 = -1;
        int vcmp = v2;
        if (type == 0) {
            a1 = (h[2] << 8) | h[3];
        } else if (type == 1) {
            if (days < 0 || days >= kFloatHolidayLen) {
                continue; // 农历表止 ~2021，越界不命中
            }
            vcmp = kFloatHoliday[days];
            a1 = (h[2] << 8) | h[3];
        } else if (type == 2) {
            if (h[2] != month) {
                continue;
            }
            const int fw = firstWeekday(year, month);
            int wd = h[4];
            if (wd < fw) {
                wd = 7;
            }
            const int d = 7 * (h[3] - 1) + wd - fw + 1;
            if (d > daysInMonth(year, month)) {
                continue;
            }
            a1 = (month << 8) | d;
        } else {
            continue;
        }
        if (vcmp == a1 && h[0] < 0x80) {
            return i;
        }
    }
    return -1;
}

// [RE 0x4523D5] 特殊日期：星期日 或命中节日且 flag 非 0（setLoanDate 跳过用）
bool isSpecialDate(const GameState& st, uint32_t date) {
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    const int wd = (firstWeekday(year, month) + day - 1) % 7;
    if (wd == 0) {
        return true;
    }
    const int idx = findHoliday(st, date);
    if (idx >= 0) {
        return kHoliday[4 * st.gameMode + st.mapIndex][idx][0] != 0;
    }
    return false;
}

// ===== 主 UI 绘制 =====

struct StayCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    bool own = false;      // byte_48C3E0（自家银行）
    int state = 0;         // byte_48C3DD
    int slideX = 640;      // dword_48C3D5
    int slideDir = 0;      // dword_48C3D9
    uint8_t clerkBits = 0; // byte_48C3DF（上窗）
    uint8_t smallTimer = 0; // byte_48C3DE（下窗驻留计数）
    int pressed = 0;       // byte_48C3E1（1..4）
    bool exitAfterSlide = false; // byte_48C3E2
    int32_t totalAssets = 0;     // dword_48C3B0（贷款上限基准）
    // [PORT] 静态层快照（帧 0 + 帘子/禁令/文字）：滑出回填与週轉返回用。
    //   原版回填用帧 0 原始像素，重写若直接贴帧 0 会擦掉帧 1 帘子与 drawText 的牌子文字
    std::vector<uint16_t> staticBg;
    FloatMessage msg;
};

// 捕获当前主画面为静态层快照（在 drawStayBase 之后调用）
void captureStatic(StayCtx& ui) {
    const uint16_t* src = ui.app->surface().pixels();
    ui.staticBg.assign(src, src + static_cast<size_t>(Surface::kWidth) * Surface::kHeight);
}

// 用静态层快照恢复矩形（滑出回填；避免把帧 1/牌子文字擦成帧 0 原样）
void restoreStaticRect(StayCtx& ui, int x, int y, int w, int h) {
    if (ui.staticBg.empty() || w <= 0 || x < 0 || x + w > Surface::kWidth) {
        return;
    }
    uint16_t* d = ui.app->surface().pixels();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        std::memcpy(d + static_cast<size_t>(py) * Surface::kWidth + x,
                    ui.staticBg.data() + static_cast<size_t>(py) * Surface::kWidth + x,
                    sizeof(uint16_t) * static_cast<size_t>(w));
    }
}

const Player& curPlayer(const StayCtx& ui) {
    return ui.app->gameState().players[ui.app->gameState().currentPlayer];
}

const char* playerName(const Player& pl) {
    return pl.name ? pl.name : "";
}

// [RE 0x434186] 主 UI 静态层（帧 0 + 申请/偿还文字 + 禁令 + 帘子/灰字）
void drawStayBase(StayCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    app.text().setFont(26, 0x101010, 0, 2, 1);
    app.text().drawText(dst, "申請貸款", kApplyLabelX, kApplyLabelY, 2);
    app.text().drawText(dst, "償還貸款", kRepayLabelX, kRepayLabelY, 2);
    const bool banned = curPlayer(ui).bankFinanceFlags != 0;
    if (banned) {
        blitElement(dst, ui.sheet->frame(kFrameBan), kBanOnApplyX, kBanOnApplyY, false);
    }
    if (ui.own) {
        // [RE 0x4343ED] 自家銀行：中央帘子打开（不画帧 1）+ 灰字标注（点击进週轉）
        app.text().setFont(14, 0x808080, 0, 2, 1);
        app.text().drawText(dst, "特別融資", kNoteX, kNoteY, 2);
    } else {
        blitElement(dst, ui.sheet->frame(kFrameCurtain), kCurtainX, kCurtainY, false);
    }
}

// [RE 0x433D6E] 右上 200x280 玩家信息面板 @(x,0)
void drawTopPanel(StayCtx& ui, int x) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const Player& pl = st.players[st.currentPlayer];
    blitElementOpaque(dst, ui.sheet->frame(kFramePanelTop), x, 0);
    if (st.currentPlayer >= 0 && st.currentPlayer < 9 &&
        st.pieceSprites[st.currentPlayer].frameCount() > 0) {
        blitElement(dst, st.pieceSprites[st.currentPlayer].frame(0), x + 42, 40, false);
    }
    app.text().setFont(12, 0xFFFFFF, 0x101010, 4, 0);
    app.text().drawText(dst, "現  金", x + 10, 80, 0);
    app.text().drawText(dst, "存  款", x + 10, 145, 0);
    app.text().drawText(dst, "貸  款", x + 10, 208, 0);
    app.text().setFont(22, 0xFFFFFF, 0x101010, 6, 0);
    app.text().drawText(dst, playerName(pl), x + 82, 28, 0);
    char buf[32];
    formatMoneyDollar(buf, pl.cash);
    app.text().drawText(dst, buf, x + 180, 100, 1);
    formatMoneyDollar(buf, pl.bank);
    app.text().drawText(dst, buf, x + 180, 164, 1);
    formatMoneyDollar(buf, pl.loan);
    app.text().drawText(dst, buf, x + 180, 228, 1);
}

// [RE 0x4169BC 大数字模式] 节日插画（data.mkf RAW 200x200）贴到 (x,y)
void drawHolidayAt(Application& app, int holiday, int x, int y) {
    GameState& st = app.gameState();
    const int base = 4 * st.gameMode + st.mapIndex;
    const int resIdx = kCalendarRes[base] + holiday;
    auto blob = st.data.read(static_cast<size_t>(resIdx));
    if (!blob) {
        return;
    }
    const RawBitmap raw = decodeRawBitmap(*blob);
    if (!raw.valid()) {
        return;
    }
    Surface& dst = app.surface();
    uint16_t* d = dst.pixels();
    for (int row = 0; row < raw.height; ++row) {
        const int py = y + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < raw.width; ++col) {
            const int px = x + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            d[static_cast<size_t>(py) * Surface::kWidth + px] = raw.pixels[row * raw.width + col];
        }
    }
}

// [RE 0x433F24] 右下 200x200 日历面板 @(x,280)（大数字模式 + 距還款日）
void drawBottomPanel(StayCtx& ui, int x) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const uint32_t date = st.gameDate;
    const int year = static_cast<int>((date >> 16) & 0xFFFF);
    const int month = static_cast<int>((date >> 8) & 0xFF);
    const int day = static_cast<int>(date & 0xFF);
    if (month < 1 || month > 12) {
        return;
    }
    const int y = 280;
    const int holiday = findHoliday(st, date);
    if (holiday < 0) {
        const int frame = kMonthFrame[month - 1];
        if (st.panelFrame2.frameCount() > frame) {
            blitElementOpaque(dst, st.panelFrame2.frame(frame), x, y);
        }
    } else {
        drawHolidayAt(app, holiday, x, y);
    }
    char buf[32];
    app.text().setFont(60, 0x101010, 0xFFFFFF, 6, 1);
    std::snprintf(buf, sizeof(buf), "%d", day);
    app.text().drawText(dst, buf, x + 60, y + 96, 2);
    app.text().setFont(16, 0x101010, 0xFFFFFF, 6, 1);
    const int wd = (firstWeekday(year, month) + day - 1) % 7;
    app.text().drawText(dst, kWeekdayNames[wd], x + 14, y + 72, 3);
    app.text().setFont(24, 0x101010, 0xFFFFFF, 6, 1);
    std::snprintf(buf, sizeof(buf), "%d", year);
    app.text().drawText(dst, buf, x + 140, y + 8, 0);
    app.text().setFont(28, 0x101010, 0xFFFFFF, 6, 1);
    std::snprintf(buf, sizeof(buf), "%d月", month);
    app.text().drawText(dst, buf, x + 60, y + 48, 2);
    const Player& pl = curPlayer(ui);
    if (pl.loanDate != 0) {
        app.text().setFont(20, 0x101010, 0xFFFFFF, 6, 1);
        std::snprintf(buf, sizeof(buf), "距還款日%d天", dateDiff(st.gameDate, pl.loanDate));
        app.text().drawText(dst, buf, x + 20, y + 176, 5);
    }
}

void drawStayPanels(StayCtx& ui) {
    if (ui.slideX >= 640) {
        return;
    }
    drawTopPanel(ui, ui.slideX);
    drawBottomPanel(ui, ui.slideX);
}

// [RE 0x4358B2] 柜员微动画：上窗 (140,114) 80x40 + 下窗 (140,152) 80x30
void drawClerkEye(StayCtx& ui, int frame) {
    Surface& dst = ui.app->surface();
    blitElement(dst, ui.sheet->frame(frame), kEyeX, kEyeY, false);
}

void drawClerkSmall(StayCtx& ui, int frame) {
    Surface& dst = ui.app->surface();
    blitElement(dst, ui.sheet->frame(frame), kSmallX, kSmallY, false);
}

// [RE 0x435669 WM_TIMER 0x4358B2] 每 tick 推进柜员动画
void tickClerk(StayCtx& ui) {
    // 上窗（眼区 80×40）：bits 低 4 位 = 状态；1=循环眨眼、2=随机眼态、**0=等待随机触发**
    //（原版判定 (byte_48C3DF & 0xF)——高 6 位残留 v<<6/0x80 不影响再触发；
    //  曾误写 clerkBits==0 → 状态 2/循环结束（bits=0x40/0x80）后永久冻结在闭眼帧，
    //  实机"眼睛一直闭着"根因，2026-09-26 修）
    const int phase = ui.clerkBits & 0xF;
    if (phase == 1) {
        const int idx = (ui.clerkBits & 0x30) >> 4;
        drawClerkEye(ui, kClerkCycle[idx]);
        ui.clerkBits = static_cast<uint8_t>((ui.clerkBits + 16) & 0x3F);
        if ((ui.clerkBits & 0x30) == 0) {
            ui.clerkBits = 0x80;
        }
    } else if (phase == 2) {
        const int v = std::rand() % 3;
        if (((ui.clerkBits & 0xC0) >> 6) != v) {
            drawClerkEye(ui, kFrameEye3 + v);
            ui.clerkBits = static_cast<uint8_t>(v << 6);
        }
    } else {
        const int v = std::rand() >> 10;
        if (v == 0) {
            ui.clerkBits |= 1;
        } else if (v == 1) {
            ui.clerkBits |= 2;
        }
    }
    // 下窗：消息中随机动作，smallTimer 倒计时归 0 时回静止帧 8
    if (ui.msg.active() || ui.smallTimer != 0) {
        if (ui.smallTimer != 0) {
            --ui.smallTimer;
            if (ui.smallTimer == 0) {
                drawClerkSmall(ui, kFrameSmall8);
            }
        } else {
            if ((std::rand() >> 11) >= 4) {
                return;
            }
            drawClerkSmall(ui, kFrameSmall9 + (std::rand() & 1));
            ui.smallTimer = static_cast<uint8_t>((std::rand() & 7) + 1);
        }
    }
}

int hitStayButton(int x, int y) {
    const int* rects[4] = {kHitExit, kHitApply, kHitRepay, kHitCenter};
    for (int i = 0; i < 4; ++i) {
        if (x >= rects[i][0] && x <= rects[i][2] && y >= rects[i][1] && y <= rects[i][3]) {
            return i; // 0=離開 1=申請貸款 2=償還貸款 3=中央（週轉）
        }
    }
    return -1;
}

// [RE 0x435228] 申请贷款（消息完成后由状态机调用）
void doLoanInput(StayCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Player& pl = st.players[st.currentPlayer];
    // [PORT] 数字框为嵌套模态（其打开期间外层 tick 停止），先把滑动推进到位再打开
    if (ui.slideDir != 0) {
        ui.slideX = kPanelX;
        ui.slideDir = 0;
    }
    if (pl.loan >= ui.totalAssets) {
        ui.state = 8; // [RE 0x435354] 尚可融资不足 → 收尾滑出
        return;
    }
    const int v = numberInputDialog(app, ui.totalAssets - pl.loan); // [RE 0x435245]
    // [PORT] 数字框无背景恢复（逐帧重绘面板区域）→ 退出后重画主 UI，避免输入器残留
    drawStayBase(ui);
    drawStayPanels(ui);
    if (v <= 0) {
        RICH4_LOGI("bank stay: loan input cancelled (RE 0x435228)");
        ui.state = 8; // 取消/0 → 收尾
        return;
    }
    pl.bank += v;
    pl.loan += v;
    setLoanDate(app, st.currentPlayer); // [RE 0x43526D]
    drawStayPanels(ui);                 // [RE 0x435275/0x43527A] 刷新双面板
    ui.state = 7;
    ui.msg.show(app, "#0079您的貸款手續\n已經完成。"); // [RE 0x475840]
    ui.exitAfterSlide = true;           // [RE 0x435333 byte_48C3E2 = 1]
    RICH4_LOGI("bank stay: loan +%d (loan=%d loanDate=%u) RE 0x435228", v, pl.loan, pl.loanDate);
}

// [RE 0x435360] 偿还贷款
void doRepayInput(StayCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Player& pl = st.players[st.currentPlayer];
    if (ui.slideDir != 0) { // [PORT] 同 doLoanInput：先完成滑动
        ui.slideX = kPanelX;
        ui.slideDir = 0;
    }
    const int v = numberInputDialog(app, pl.loan); // [RE 0x435367]
    // [PORT] 数字框退出后重画主 UI（防输入器残留）
    drawStayBase(ui);
    drawStayPanels(ui);
    if (v <= 0) {
        RICH4_LOGI("bank stay: repay input cancelled (RE 0x435360)");
        ui.state = 8;
        return;
    }
    if (v > pl.bank + pl.cash) {
        ui.state = 9; // 状态 9 消息完 → 再次打开输入
        ui.msg.show(app, "#0083很抱歉！\n您的現金不足。"); // [RE 0x475850]
        return;
    }
    pl.bank -= v;
    if (pl.bank < 0) {
        pl.cash += pl.bank; // 存款优先，不足补现金
        pl.bank = 0;
    }
    pl.loan -= v;
    if (pl.loan == 0) {
        pl.loanDate = 0; // [RE 0x4353F0]
    }
    drawStayPanels(ui);
    ui.state = 8;
    ui.msg.show(app, "#0084您的還款手續\n已完成。"); // [RE 0x475854]
    ui.exitAfterSlide = true;
    RICH4_LOGI("bank stay: repay -%d (loan=%d) RE 0x435360", v, pl.loan);
}

// ===== 週轉現金子模态（sub_434492；背景 = panel[23] 帧 2 经理室 + 週轉 UI）=====

struct AdvCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    int state = 0;       // byte_48C3CC：0/1 开场、2 空闲、3/4 週轉输入、5/6 还款输入、7 退出
    int quota = 0;       // dword_48C3C8 = Σ其他玩家(0..3) bank
    int exitCode = 0;    // byte_48C3D0
    int pressed = 0;     // byte_48C3CF 1..3
    uint8_t advBits = 0; // byte_48C3CE（眼部循环）
    uint8_t smallTimer = 0; // byte_48C3CD（下窗驻留）
    FloatMessage msg;
};

const Player& advCurPlayer(const AdvCtx& ui) {
    return ui.app->gameState().players[ui.app->gameState().currentPlayer];
}

// 週轉数字（[RE 0x433C20]：额度 / 已週轉 / 剩余，16 号右对齐 @(128,163/211/259)）
void drawAdvNumbers(AdvCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    // 清数字区：原版从帧 2（已合成面板/文字的週轉画面）取 (22,161) 113×117；
    // 重写帧 2 是原始经理室 → 必须从帧 20 面板取对应区域 (12,36)，否则会把面板底图擦成背景
    blitElementRegionOpaque(dst, ui.sheet->frame(kFramePanelLeft), 22, 161, 12, 36, 113, 117, false);
    const Player& pl = advCurPlayer(ui);
    char buf[32];
    app.text().setFont(16, 0xF0F0F0, 0, 2, 0);
    formatMoneyDollar(buf, ui.quota);
    app.text().drawText(dst, buf, kAdvNumX, kAdvNumY[0], 1);
    formatMoneyDollar(buf, pl.bankAdvance);
    app.text().drawText(dst, buf, kAdvNumX, kAdvNumY[1], 1);
    formatMoneyDollar(buf, ui.quota - pl.bankAdvance);
    app.text().drawText(dst, buf, kAdvNumX, kAdvNumY[2], 1);
}

// [RE 0x434186 自家分支画在帧 2 的週轉 UI] 週轉画面（帧 2 + 左面板 + 三按钮 + 文字）
void drawAdvScene(AdvCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameOwn), 0, 0);
    blitElementOpaque(dst, ui.sheet->frame(kFramePanelLeft), kAdvPanelLeftX, kAdvPanelLeftY);
    blitElement(dst, ui.sheet->frame(kFrameBtnWide), kAdvBtnX, kAdvBtnTurnY, false);
    blitElement(dst, ui.sheet->frame(kFrameBtnWide), kAdvBtnX, kAdvBtnBackY, false);
    blitElement(dst, ui.sheet->frame(kFrameBtnExit), kAdvBtnX, kAdvBtnLeaveY, false);
    const Player& pl = st.players[st.currentPlayer];
    app.text().setFont(20, 0xF0F0F0, 0, 2, 0);
    app.text().drawText(dst, "週轉現金", 68, 325, 2);
    if (pl.bankFinanceFlags != 0) {
        blitElement(dst, ui.sheet->frame(kFrameBan), kBanOnTurnX, kBanOnTurnY, false);
    }
    app.text().drawText(dst, "歸還款項", 68, 383, 2);
    app.text().setFont(26, 0x101010, 0, 2, 1);
    app.text().drawText(dst, "特別融資", 443, 427, 2);
    drawAdvNumbers(ui);
    // 标签在数字区清空之后绘制（「目前融資金額/尚可融資金額」行位于清区 (22,161)+113x117 内）
    app.text().setFont(16, 0x202020, 0, 2, 0);
    app.text().drawText(dst, "客戶存款總額", 78, 147, 2);
    app.text().drawText(dst, "目前融資金額", 78, 195, 2);
    app.text().drawText(dst, "尚可融資金額", 78, 243, 2);
}

// [RE 0x434492 WM_TIMER] 週轉动画（100ms）
void tickAdvAnim(AdvCtx& ui) {
    Surface& dst = ui.app->surface();
    if ((ui.advBits & 0xF) == 1) {
        const int idx = (ui.advBits & 0x30) >> 4;
        ui.advBits = static_cast<uint8_t>((ui.advBits + 16) & 0x3F);
        if ((ui.advBits & 0x30) == 0x30) {
            ui.advBits = 0; // 循环结束 → 帧 2 回填
            blitElementRegionOpaque(dst, ui.sheet->frame(kFrameOwn), kAdvEyeX, kAdvEyeY,
                                    kAdvEyeX, kAdvEyeY, kAdvEyeW, kAdvEyeH, false);
        } else {
            blitElement(dst, ui.sheet->frame(kAdvCycle[idx]), kAdvEyeX, kAdvEyeY, false);
        }
    } else if ((ui.advBits & 0xF) == 0 && (std::rand() >> 10) == 0) {
        ui.advBits |= 1;
    }
    if (ui.msg.active() || ui.smallTimer != 0) {
        if (ui.smallTimer != 0) {
            --ui.smallTimer;
            if (ui.smallTimer == 0) {
                blitElementRegionOpaque(dst, ui.sheet->frame(kFrameOwn), kAdvSmallX, kAdvSmallY,
                                        kAdvSmallX, kAdvSmallY, kAdvSmallW, kAdvSmallH, false);
            }
        } else {
            if ((std::rand() >> 11) >= 4) {
                return;
            }
            blitElement(dst, ui.sheet->frame(kFrameAdvSmall13 + (std::rand() & 1)),
                        kAdvSmallX, kAdvSmallY, false);
            ui.smallTimer = static_cast<uint8_t>((std::rand() & 7) + 1);
        }
    }
}

int hitAdvButton(int x, int y) {
    const int* rects[3] = {kAdvHitTurn, kAdvHitBack, kAdvHitLeave};
    for (int i = 0; i < 3; ++i) {
        if (x >= rects[i][0] && x <= rects[i][2] && y >= rects[i][1] && y <= rects[i][3]) {
            return i;
        }
    }
    return -1;
}

// 週轉輸入（状态 3 消息完成后）：v ≤ 0（取消/0）→ 回空闲 2
void doAdvTurnInput(AdvCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Player& pl = st.players[st.currentPlayer];
    const int v = numberInputDialog(app, ui.quota - pl.bankAdvance); // [RE 0x4348xx 0x409]
    // [PORT] 数字框退出后重画週轉画面（防输入器残留；含数字刷新）
    drawAdvScene(ui);
    if (v <= 0) {
        ui.state = 2;
    } else {
        pl.bank += v;
        pl.bankAdvance += v;
        drawAdvNumbers(ui);
        ui.exitCode = 1; // [RE 0x4348BC byte_48C3D0 = 1]
        ui.state = 7;
    }
    RICH4_LOGI("bank advance: +%d quota=%d advance=%d RE 0x434492", v, ui.quota, pl.bankAdvance);
}

// 週轉還款輸入（状态 5 消息完成后）
void doAdvBackInput(AdvCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Player& pl = st.players[st.currentPlayer];
    const int v = numberInputDialog(app, pl.bankAdvance); // [RE 0x4349xx 0x40A]
    // [PORT] 数字框退出后重画週轉画面（防输入器残留）
    drawAdvScene(ui);
    if (v <= 0) {
        ui.state = 2;
        return;
    }
    if (v > pl.bank + pl.cash) {
        ui.state = 5; // 现金不足 → 退回提示（原版 off_47586C = #0090）
        ui.msg.show(app, "#0090董事長您別開玩笑了～");
        return;
    }
    pl.bank -= v;
    if (pl.bank < 0) {
        pl.cash += pl.bank;
        pl.bank = 0;
    }
    pl.bankAdvance -= v;
    drawAdvNumbers(ui);
    ui.exitCode = 1;
    ui.state = 7;
    RICH4_LOGI("bank advance repay: -%d advance=%d RE 0x434492", v, pl.bankAdvance);
}

bool advHandler(const SDL_Event* event, void* user) {
    AdvCtx& ui = *static_cast<AdvCtx*>(user);
    Application& app = *ui.app;
    if (!event) {
        ui.msg = FloatMessage();
        ui.msg.setup(*ui.sheet, kFrameAdvMsgBoard, kAdvMsgX, kAdvMsgY, 0, -10, 0x101010, 0);
        ui.state = 0;
        ui.pressed = 0;
        ui.advBits = 0;
        ui.smallTimer = 0;
        ui.exitCode = 0;
        GameState& st = app.gameState();
        ui.quota = 0;
        for (int i = 0; i < 4; ++i) { // [RE 0x434492 0x401] Σ其他玩家 bank
            if (i != st.currentPlayer) {
                ui.quota += st.players[i].bank;
            }
        }
        drawAdvScene(ui);
        ui.state = 1;
        ui.msg.show(app, "#0086董事長親自蒞臨\n不知有何指教？"); // [RE 0x47585C]
        RICH4_LOGI("bank advance: enter quota=%d cur=%d (RE 0x434492)", ui.quota, st.currentPlayer);
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: {
        if (ui.state == 4 || ui.state == 6) {
            return true; // 输入中：动画暂停
        }
        if (ui.msg.advance(app)) {
            switch (ui.state) {
            case 1:
                ui.state = 2; // 开场消息完 → 空闲
                break;
            case 3:
                ui.state = 4;
                doAdvTurnInput(ui);
                break;
            case 5:
                ui.state = 6;
                doAdvBackInput(ui);
                break;
            case 7:
                RICH4_LOGI("bank advance: exit code=%d (RE 0x434492)", ui.exitCode);
                app.events().requestExit(ui.exitCode); // [RE 0x4347xx 状态7]
                return true;
            default:
                break;
            }
        }
        tickAdvAnim(ui);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (ui.state < 2) { // [RE 0x434492 LABEL_65] 开场中点击 → 跳过并置 1
            ui.msg.finish(app);
            ui.state = 1;
            return true;
        }
        if (ui.state > 2) {
            if (ui.msg.active()) {
                ui.msg.finish(app);
            }
            return true;
        }
        int x = 0;
        int y = 0;
        app.mouseLogicalPos(x, y);
        const int hit = hitAdvButton(x, y);
        if (hit < 0) {
            ui.pressed = 0;
            return true;
        }
        app.audio().playEffect(kSfxClick); // [RE 0x434492 g_uiSoundClick]
        ui.pressed = hit + 1;
        Surface& dst = app.surface();
        if (hit == 0) {
            const Player& pl = advCurPlayer(ui);
            if (pl.bankFinanceFlags != 0) { // 暫停放款中：週轉按钮忽略
                ui.pressed = 0;
                return true;
            }
            blitElement(dst, ui.sheet->frame(kFrameBtnWideDown), kAdvBtnX, kAdvBtnTurnY, false);
            app.text().setFont(20, 0xF0F0F0, 0, 2, 0);
            app.text().drawText(dst, "週轉現金", 68, 325, 2);
        } else if (hit == 1) {
            blitElement(dst, ui.sheet->frame(kFrameBtnWideDown), kAdvBtnX, kAdvBtnBackY, false);
            app.text().setFont(20, 0xF0F0F0, 0, 2, 0);
            app.text().drawText(dst, "歸還款項", 68, 383, 2);
        } else {
            blitElement(dst, ui.sheet->frame(kFrameBtnExitDown), kAdvBtnX, kAdvBtnLeaveY, false);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            if (ui.state != 7) { // [RE 0x434C7x 0x205]
                app.audio().playEffect(kSfxCancel);
                if (ui.msg.active()) {
                    ui.msg.finish(app);
                }
                ui.state = 7;
                ui.msg.show(app, "#0091董事長慢走！"); // [RE 0x475870]
            }
            return true;
        }
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (ui.pressed == 0) {
            return true;
        }
        const int p = ui.pressed;
        ui.pressed = 0;
        Surface& dst = app.surface();
        GameState& st = app.gameState();
        Player& pl = st.players[st.currentPlayer];
        if (p == 1) { // [RE 0x434C0x] 週轉現金
            blitElement(dst, ui.sheet->frame(kFrameBtnWide), kAdvBtnX, kAdvBtnTurnY, false);
            app.text().setFont(20, 0xF0F0F0, 0, 2, 0);
            app.text().drawText(dst, "週轉現金", 68, 325, 2);
            if (pl.bankFinanceFlags != 0) {
                blitElement(dst, ui.sheet->frame(kFrameBan), kBanOnTurnX, kBanOnTurnY, false);
            } else if (pl.bankAdvance < ui.quota) {
                ui.state = 3;
                ui.msg.show(app, "#0087請輸入您要\n週轉的金額～"); // [RE 0x475860]
            }
        } else if (p == 2) { // 歸還款項
            blitElement(dst, ui.sheet->frame(kFrameBtnWide), kAdvBtnX, kAdvBtnBackY, false);
            app.text().setFont(20, 0xF0F0F0, 0, 2, 0);
            app.text().drawText(dst, "歸還款項", 68, 383, 2);
            if (pl.bankAdvance != 0) {
                ui.state = 5;
                ui.msg.show(app, "#0089請輸入您要\n還款的金額～"); // [RE 0x475868]
            }
        } else if (p == 3) { // 離開
            blitElement(dst, ui.sheet->frame(kFrameBtnExit), kAdvBtnX, kAdvBtnLeaveY, false);
            ui.state = 7;
            ui.msg.show(app, "#0091董事長慢走！"); // [RE 0x475870]
        }
        return true;
    }
    default:
        return true;
    }
    return true;
}

// ===== 贷款到期催收窗（sub_43695E → sub_436034）=====

struct DueCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    int state = 0;          // byte_48C3E7：1/2/3 消息阶段
    uint8_t clerkBits = 0;  // byte_48C3E9
    uint8_t smallTimer = 0; // byte_48C3E8
    char helloMsg[64] = {0}; // dword_475874 动态槽（"%s您好"）
    FloatMessage msg;
};

// [RE 0x436034] 催收画面：sub_434186(0)（普通银行大堂）+ 双面板 @(440,0)/@(440,280)
void drawDueScene(DueCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    app.text().setFont(26, 0x101010, 0, 2, 1);
    app.text().drawText(dst, "申請貸款", kApplyLabelX, kApplyLabelY, 2);
    app.text().drawText(dst, "償還貸款", kRepayLabelX, kRepayLabelY, 2);
    blitElement(dst, ui.sheet->frame(kFrameCurtain), kCurtainX, kCurtainY, false);
    // 双面板固定滑入位置（sub_436034 0x401 直接 blit @440）
    StayCtx top;
    top.app = ui.app;
    top.sheet = ui.sheet;
    top.slideX = kPanelX;
    drawTopPanel(top, kPanelX);
    drawBottomPanel(top, kPanelX);
}

void dueTickClerk(DueCtx& ui) {
    Surface& dst = ui.app->surface();
    if ((ui.clerkBits & 0xF) == 1) {
        const int idx = (ui.clerkBits & 0x30) >> 4;
        blitElement(dst, ui.sheet->frame(kClerkCycle[idx]), kEyeX, kEyeY, false);
        ui.clerkBits = static_cast<uint8_t>((ui.clerkBits + 16) & 0x3F);
        if ((ui.clerkBits & 0x30) == 0) {
            ui.clerkBits = 0x80;
        }
    } else if ((ui.clerkBits & 0xF) == 2) {
        const int v = std::rand() % 3;
        if (((ui.clerkBits & 0xC0) >> 6) != v) {
            blitElement(dst, ui.sheet->frame(kFrameEye3 + v), kEyeX, kEyeY, false);
            ui.clerkBits = static_cast<uint8_t>(v << 6);
        }
    } else if (ui.clerkBits == 0) {
        const int v = std::rand() >> 10;
        if (v == 0) {
            ui.clerkBits |= 1;
        } else if (v == 1) {
            ui.clerkBits |= 2;
        }
    }
    if (ui.msg.active() || ui.smallTimer != 0) {
        if (ui.smallTimer != 0) {
            --ui.smallTimer;
            if (ui.smallTimer == 0) {
                blitElement(dst, ui.sheet->frame(kFrameSmall8), kSmallX, kSmallY, false);
            }
        } else {
            if ((std::rand() >> 11) >= 4) {
                return;
            }
            blitElement(dst, ui.sheet->frame(kFrameSmall9 + (std::rand() & 1)), kSmallX, kSmallY,
                        false);
            ui.smallTimer = static_cast<uint8_t>((std::rand() & 7) + 1);
        }
    }
}

bool dueHandler(const SDL_Event* event, void* user) {
    DueCtx& ui = *static_cast<DueCtx*>(user);
    Application& app = *ui.app;
    if (!event) {
        ui.msg = FloatMessage();
        ui.msg.setup(*ui.sheet, kFrameMsgBoard, kMsgBoardX, kMsgBoardY, 20, 0, 0x101010, 0);
        drawDueScene(ui);
        GameState& st = app.gameState();
        const int p = st.currentPlayer;
        std::snprintf(ui.helloMsg, sizeof(ui.helloMsg), "%s您好",
                      playerNameNoSpace(st, p).c_str()); // [RE 0x43616F sprintf 前 0x436185 copyNameNoSpaces]
        ui.state = 1;
        ui.msg.show(app, ui.helloMsg); // [RE 0x4361AE]
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: {
        if (ui.msg.advance(app)) {
            if (ui.state == 1) {
                ui.state = 2;
                ui.msg.show(app, "您向銀行借貸的\n貸款即將到期。"); // [RE 0x475878]
            } else if (ui.state == 2) {
                ui.state = 3;
                ui.msg.show(app, "請不要忘記喔！"); // [RE 0x47587C]
            } else if (ui.state == 3) {
                app.events().requestExit(0); // [RE 0x436237]
                return true;
            }
        }
        dueTickClerk(ui);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (event->button.button == SDL_BUTTON_LEFT) {
            app.audio().playEffect(kSfxClick); // [RE 0x436596]
            if (ui.msg.active()) {
                ui.msg.finish(app); // floatMsgAdvance(1)
            }
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button == SDL_BUTTON_RIGHT) { // [RE 0x4365BB 0x205]
            if (ui.state < 3) {
                app.audio().playEffect(kSfxCancel);
                ui.state = 3;
                if (ui.msg.active()) {
                    ui.msg.finish(app); // floatMsgAdvance(1) → 下一 tick 直接退出
                }
            }
        }
        return true;
    }
    default:
        return true;
    }
    return true;
}

// ===== 主 UI 模态处理 =====

bool stayHandler(const SDL_Event* event, void* user) {
    StayCtx& ui = *static_cast<StayCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (!event) { // WM_USER+1
        // [RE 0x435197 → 0x405] 暫停放款提示：原版在 0x401（sub_434186 画银行场景/招牌）后由
        //   0x405 分支叠加 showMessage；实机反馈要求提示先独立显示（此时屏幕上仍是地图场景），
        //   提示消失后再画银行场景与欢迎动画 —— 差异: 与原版叠加显示不同（体验调整 2026-09-26）。
        Player& pl = st.players[st.currentPlayer];
        if (pl.bankFinanceFlags != 0) {
            char text[64];
            std::snprintf(text, sizeof(text), "銀行暫停放款\n\n還剩%d天！",
                          (pl.bankFinanceFlags & 0x7F) + 1); // [RE 0x464AD5]
            // [RE 0x4351EE] 原版 showMessage(text, 0x800005DC) = 1500ms + 文本左移 100px
            //   （0x800005DC & 0x7FFFFFFF = 0x5DC；重写 showMessage 未实现左移，且负值会被
            //   当作超大 uint64 → 永不退出，故传正 1500；曾误传 8000 导致提示框久留）
            showMessage(app, text, 1500);
        }
        ui.msg = FloatMessage();
        ui.msg.setup(*ui.sheet, kFrameMsgBoard, kMsgBoardX, kMsgBoardY, 20, 0, 0x101010, 0);
        ui.state = 0;
        ui.slideX = 640;
        ui.slideDir = 0;
        ui.clerkBits = 0;
        ui.smallTimer = 0;
        ui.pressed = 0;
        ui.exitAfterSlide = false;
        drawStayBase(ui);
        captureStatic(ui); // [PORT] 静态层快照（滑出回填/週轉返回用）
        if (st.settings[1] != 0) { // [RE 0x435200] 动画过程开关
            ui.state = 1;
            ui.msg.show(app, "#0075歡迎光臨\n大富翁銀行！"); // [RE 0x475830]
        } else {
            ui.state = 3; // 直接服务询问（下一 tick 转 4）
        }
        RICH4_LOGI("bank stay: enter own=%d assets=%d (RE 0x435062)", ui.own ? 1 : 0,
                   ui.totalAssets);
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: {
        // [RE 0x4354DF] 右侧面板滑动（±40px/50ms）
        if (ui.slideDir != 0) {
            const int oldX = ui.slideX;
            ui.slideX += ui.slideDir;
            if (ui.slideDir > 0 && ui.slideX > kPanelX) {
                // 滑出：用静态层快照回填旧位置左边缘条（快照含帘子/牌子文字）
                restoreStaticRect(ui, oldX, 0, ui.slideDir, 480);
            }
            drawStayPanels(ui);
            if (ui.slideX == kPanelX) {
                ui.slideDir = 0;
            }
            if (ui.slideX == 640) {
                ui.slideDir = 0;
                if (ui.exitAfterSlide) {
                    ui.state = 11;
                }
            }
        }
        // [RE 0x435669] 浮动消息推进 → 状态机
        //   原版分支分组：state<7 { <3 / <=3 / ==5 } else if (<=7) {} else { <9 滑出 / <=9 / ==11 }
        //   （重写曾把 <9/<=9 写成开放区间，导致 state 4 每 tick 误触发滑出把面板拉回 440）
        if (ui.msg.advance(app)) {
            if (ui.state < 3) {
                if (ui.state == 1) {
                    ui.state = 3;
                    ui.msg.show(app, "#0076需要我為您\n服務嗎？"); // [RE 0x475834]
                }
            } else if (ui.state <= 3) {
                ui.state = 4; // 等待操作
            } else if (ui.state == 5) {
                ui.state = 6;
                doLoanInput(ui); // [RE 0x435228]（原版 PostMessage 0x409 异步）
            } else if (ui.state == 7) {
                ui.state = 8;
                ui.msg.show(app, "#0080請於三個月內\n還清貸款。"); // [RE 0x475844]
            } else if (ui.state == 8) { // 收尾 → 滑出（完成且 exitAfterSlide 时转 11）
                ui.slideDir = 40;
                ui.slideX = kPanelX;
                ui.state = 4;
            } else if (ui.state == 9) {
                ui.state = 10;
                doRepayInput(ui); // [RE 0x435360]（原版 PostMessage 0x40A 异步）
            } else if (ui.state == 11) {
                app.events().requestExit(0); // [RE 0x4356xx]
                return true;
            }
        }
        if (ui.state == 6 || ui.state == 10) {
            return true; // 输入框打开期间不动画（已同步返回，此分支仅防御）
        }
        tickClerk(ui);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (ui.state < 4) { // [RE 0x435Bxx] 开场中点击跳过
            ui.msg.finish(app);
            ui.state = 3;
            return true;
        }
        if (ui.state > 4) {
            if (ui.msg.active()) {
                ui.msg.finish(app);
            }
            return true;
        }
        int x = 0;
        int y = 0;
        app.mouseLogicalPos(x, y);
        const int hit = hitStayButton(x, y);
        if (hit < 0) {
            ui.pressed = 0;
            return true;
        }
        // 非自家银行点击中央帘子区：完全屏蔽（原版仅屏蔽动作仍播音效；按反馈连音效屏蔽）
        if (hit == 3 && !ui.own) {
            ui.pressed = 0;
            return true;
        }
        app.audio().playEffect(kSfxClick); // [RE 0x435C6E g_uiSoundClick]
        ui.pressed = hit + 1;
        RICH4_LOGI("bank stay: click hit=%d state=%d own=%d slideX=%d (RE 0x435C3A)", hit, ui.state,
                   ui.own ? 1 : 0, ui.slideX);
        Player& pl = st.players[st.currentPlayer];
        switch (hit) {
        case 0: // 離開：按下高亮（原版 0x435D3E 传 Rect.right/bottom 笔误，按左上角修正）
            blitElement(app.surface(), ui.sheet->frame(kFrameBtnExitDown), kHitExit[0], kHitExit[1],
                        false);
            break;
        case 1: // 申請貸款
            ui.state = 5;
            ui.slideX = 640;
            ui.slideDir = -40;
            if (pl.loan < ui.totalAssets) {
                ui.msg.show(app, "#0078請輸入您要貸款\n的金額！"); // [RE 0x47583C]
            } else {
                ui.msg.show(app, "#0081很抱歉！\n金額已超過\n許可額度。"); // [RE 0x475848]
            }
            break;
        case 2: // 償還貸款
            if (pl.loan == 0) {
                ui.pressed = 0;
                break;
            }
            ui.state = 9;
            ui.slideX = 640;
            ui.slideDir = -40;
            ui.msg.show(app, "#0082請輸入您預備\n還款的金額！"); // [RE 0x47584C]
            break;
        case 3: // 中央区（仅自家银行）：週轉現金
            if (ui.own) {
                ui.state = 10;
                AdvCtx adv;
                adv.app = &app;
                adv.sheet = ui.sheet;
                const int r = runModal(app, &advHandler, &adv, 100); // [RE 0x4354xx]
                if (r != 0) {
                    ui.state = 11; // 週轉退出码 1 → 直接离开银行
                } else {
                    ui.state = 4;
                    drawStayBase(ui); // 覆盖週轉画面
                    captureStatic(ui);
                    drawStayPanels(ui);
                    ui.msg.setup(*ui.sheet, kFrameMsgBoard, kMsgBoardX, kMsgBoardY, 20, 0,
                                 0x101010, 0);
                }
            }
            break;
        default:
            break;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (ui.pressed == 1 && ui.state != 11) { // [RE 0x435D3E] 離開松开 → 谢谢惠顾
            blitElement(app.surface(), ui.sheet->frame(kFrameBtnExit), kHitExit[0], kHitExit[1],
                        false);
            ui.state = 11;
            ui.msg.show(app, "#0085謝謝您的惠顧！"); // [RE 0x475858]
        }
        ui.pressed = 0;
        return true;
    }
    case SDL_EVENT_KEY_DOWN: {
        if (event->key.key == SDLK_ESCAPE) { // 重写增强（原版仅右键）
            if (ui.state != 11) {
                app.audio().playEffect(kSfxCancel);
                if (ui.msg.active()) {
                    ui.msg.finish(app);
                }
                ui.state = 11;
                ui.msg.show(app, "#0085謝謝您的惠顧！");
            }
        }
        return true;
    }
    default:
        return true;
    }
    return true;
}

} // namespace

// [RE 0x433B7E] setLoanDate：loanDate 为 0 时设为今天+90 天（逐日跳过特殊日期）
//   银行贷款与命运「人頭被盜用冒貸」(fateEvent id2) 共用（原版调用点 0x435228 / 0x44C201）
void setLoanDate(Application& app, int p) {
    GameState& st = app.gameState();
    Player& pl = st.players[p];
    if (pl.loanDate != 0) {
        return;
    }
    uint32_t d = dateAddDays(st.gameDate, 90);
    int guard = 0;
    while (isSpecialDate(st, d) && guard++ < 366) {
        d = dateAddDays(d, 1); // [RE 0x452117] 原版 sub_452117(&date) = 日期+1
    }
    pl.loanDate = d;
}

// [RE 0x436668] 停留银行（landingEvent case 14 → 路过柜员机后）
void bankStayDialog(Application& app, int cellEntId) {
    // [NEW] named region 登记（0x4757F8 命中矩形同源常量；脚本 clickr bank.exit/apply/
    //   repay/center；center=自家融资帘启后中央区）
    {
        static bool s_reg = false;
        if (!s_reg) {
            s_reg = true;
            debug::registerRegion("bank.exit", kHitExit[0], kHitExit[1],
                                  kHitExit[2] - kHitExit[0], kHitExit[3] - kHitExit[1]);
            debug::registerRegion("bank.apply", kHitApply[0], kHitApply[1],
                                  kHitApply[2] - kHitApply[0], kHitApply[3] - kHitApply[1]);
            debug::registerRegion("bank.repay", kHitRepay[0], kHitRepay[1],
                                  kHitRepay[2] - kHitRepay[0], kHitRepay[3] - kHitRepay[1]);
            debug::registerRegion("bank.center", kHitCenter[0], kHitCenter[1],
                                  kHitCenter[2] - kHitCenter[0], kHitCenter[3] - kHitCenter[1]);
        }
    }

    trace::logf("dialog open name=bank_stay");
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    if (pl.bankRefuseDays != 0) { // [RE 0x43667B] 拒絕往來中不处理
        return;
    }
    const int32_t assets = playerTotalAssets(app, p); // [RE 0x436697]
    if (pl.alive == 1) {
        auto blob = st.panel.read(23); // [RE 0x4366C3] panel.mkf[23]
        if (!blob) {
            RICH4_LOGW("bankStay: panel.mkf[23] unavailable (RE 0x4366C3)");
            return;
        }
        UiImage sheet;
        sheet.load(std::move(*blob));
        if (sheet.frameCount() < 24) {
            RICH4_LOGW("bankStay: panel.mkf[23] frames %zu < 24", sheet.frameCount());
            return;
        }
        StayCtx ui;
        ui.app = &app;
        ui.sheet = &sheet;
        ui.totalAssets = assets;
        // [RE 0x436726] 自家银行判定：实参 cellEntId 是 cellEnts 数组下标，
        //   需先取该格的 objId（+32 special，6000+n）再索引 specPts（原版 sub_436668 实参为 objId）
        ui.own = false;
        int specIdx = -1;
        int specOwner = -1;
        int specObjId = 0;
        if (cellEntId >= 0 && cellEntId < static_cast<int>(st.cellEnts.size())) {
            specObjId = st.cellEnts[cellEntId].special;
            if (specObjId > 6000 && specObjId < 8000) {
                specIdx = specObjId - 6000;
                if (specIdx >= 0 && specIdx < static_cast<int>(st.specPts.size())) {
                    specOwner = st.specPts[specIdx].owner;
                    if (specOwner == p + 1) {
                        ui.own = true;
                    }
                }
            }
        }
        RICH4_LOGI("bank stay: enter own=%d cellEnt=%d objId=%d specIdx=%d owner=%d cur=%d "
                   "assets=%d (RE 0x436668)",
                   ui.own ? 1 : 0, cellEntId, specObjId, specIdx, specOwner, p, assets);
        app.audio().pushSceneMusic(4); // [RE 0x43674F] musicPlayScene(4)
        runModal(app, &stayHandler, &ui, 50); // [RE 0x43675D] SetTimer 50ms
        app.audio().resumeSceneMusic(); // [RE 0x436765] musicStackPopRestore
        return;
    }
    // AI 分支（[RE 0x4367AB]）：已有贷款 → 提前还款；否则随机贷款
    char name[64];
    std::snprintf(name, sizeof(name), "%s", playerName(pl));
    char text[128];
    if (pl.loan != 0) {
        bool early = false;
        if (dateDiff(st.gameDate, pl.loanDate) <= 6) { // [RE 0x4367D1]
            early = static_cast<double>(pl.loan) * 1.12 <=
                    static_cast<double>(pl.cash + pl.bank); // [RE 0x464B24] dbl_464B24 = 1.12
        }
        if (2 * pl.loan < pl.bank || early) { // [RE 0x436827]
            const int32_t amount = pl.loan;
            payFromBank(app, p, amount); // [RE 0x43683E]
            std::snprintf(text, sizeof(text), "%s\n\n還款%d元", name, amount); // [RE 0x464AF5]
            showMessage(app, text, 1500);
            pl.loan = 0; // 原版仅清 loan（loanDate 残留，复刻）
            RICH4_LOGI("bank stay AI: player %d early repay %d (RE 0x436668)", p, amount);
        }
        return;
    }
    const int r = dbg::raw(dbg::SlotAi);
    if ((r % 10) == 0 || pl.bank + pl.cash < 30000) { // [RE 0x436893]
        if (pl.bankFinanceFlags == 0 && pl.aiLoanPct != 0) { // [RE 0x4368CE]
            const int32_t v = static_cast<int32_t>(static_cast<int64_t>(assets) * pl.aiLoanPct / 100);
            pl.loan = v;
            if (v != 0) {
                pl.bank += v;
                setLoanDate(app, p); // [RE 0x436912]
                std::snprintf(text, sizeof(text), "%s\n\n向銀行貸款\n\n%d元", name, v); // [RE 0x464B0C]
                showMessage(app, text, 1500);
                RICH4_LOGI("bank stay AI: player %d borrow %d (RE 0x436668)", p, v);
            }
        }
    }
}

// [RE 0x43695E] 贷款到期催收窗（到期前 3 天）
void bankDueDialog(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9 || st.players[p].alive != 1) { // [RE 0x436970]
        return;
    }
    auto blob = st.panel.read(23); // [RE 0x43698B]
    if (!blob) {
        RICH4_LOGW("bankDue: panel.mkf[23] unavailable");
        return;
    }
    UiImage sheet;
    sheet.load(std::move(*blob));
    if (sheet.frameCount() < 24) {
        return;
    }
    DueCtx ui;
    ui.app = &app;
    ui.sheet = &sheet;
    app.audio().pushSceneMusic(4); // [RE 0x4369E2]
    runModal(app, &dueHandler, &ui, 50); // [RE 0x4369F1]
    app.audio().resumeSceneMusic();   // [RE 0x436A03]
    RICH4_LOGI("bankDue: player %d collect window done (RE 0x43695E)", p);
}

} // namespace rich4
