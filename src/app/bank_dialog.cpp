#include "game/app/bank_dialog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/message_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/cursor.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// =====================================================================
// 路过银行柜员机（0x436EF8 WndProc / 0x436D3A 数字滚动 / 0x436B0A 週轉结算）
// =====================================================================

constexpr int kVisitX = 60; // 面板基准偏移（0x436FCE blit(帧0, 60, 71)）
constexpr int kVisitY = 71;

// [RE 0x475888/47588A/47588C/47588E] 18 个按钮命中矩形（相对 +60/+71）
struct VisitRect {
    int x0, y0, x1, y1;
};
constexpr VisitRect kVisitRects[18] = {
    {0x39, 0x31, 0x89, 0x5A},    // 0 銀行帳戶（取款模式）
    {0x8B, 0x31, 0xDB, 0x5A},    // 1 身上現金（存款模式）
    {0xDD, 0x31, 0x108, 0x5A},   // 2 離開
    {0x35, 0x89, 0x10C, 0xA6},   // 3 金額滑條
    {0x3A, 0xD3, 0x5B, 0xE4},    // 4 '7'
    {0x61, 0xD3, 0x82, 0xE4},    // 5 '8'
    {0x88, 0xD3, 0xA9, 0xE4},    // 6 '9'
    {0x3A, 0xE6, 0x5B, 0xF7},    // 7 '4'
    {0x61, 0xE6, 0x82, 0xF7},    // 8 '5'
    {0x88, 0xE6, 0xA9, 0xF7},    // 9 '6'
    {0x3A, 0xF9, 0x5B, 0x10A},   // 10 '1'
    {0x61, 0xF9, 0x82, 0x10A},   // 11 '2'
    {0x88, 0xF9, 0xA9, 0x10A},   // 12 '3'
    {0x3A, 0x10C, 0x5B, 0x11D},  // 13 'C'（清除）
    {0x61, 0x10C, 0x82, 0x11D},  // 14 '0'
    {0x88, 0x10C, 0xA9, 0x11D},  // 15 Backspace
    {0xB7, 0xE9, 0xE8, 0x102},   // 16 確認
    {0xAF, 0x104, 0xE8, 0x11D},  // 17 結束/取消
};
// [RE 0x475914] 按钮 4..15 → 数字字符（键盘布局 789/456/123/C/0/←）
constexpr char kVisitDigits[12] = {'7', '8', '9', '4', '5', '6', '1', '2', '3', 'C', '0', 8};
// 数字显示帧 = ASCII − 29（'0'=48 → 帧 19，'9'=57 → 帧 28；面板仅 30 帧，越界会读垃圾导致卡死）
constexpr int kDigitFrameBase = 19;
// 音效（映射同股票数字输入框 number_input_dialog）：
// [RE 0x48234A] 键盘/EXIT/MAX/確定 按下 = g_effectSlots[0]（Effect.mkf[7]）
// [RE 0x482352] 滑条 = Effect.mkf[9]；[RE 0x482322] 顶部存/取 = g_uiSoundClick（Effect.mkf[1]）
constexpr int kVisitSfxKey = 7;
constexpr int kVisitSfxSlider = 9;
constexpr int kVisitSfxClick = 1;
// [RE 0x464BD0] flt_464BD0 = 34.0f（金额/上限 → 进度条宽度系数；满额 6×34=204 = 条宽）
constexpr double kVisitBarScale = 34.0;
constexpr int kVisitBarMax = 204;

struct VisitCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr; // panel.mkf[24]
    int mode = 0;             // dword_48C3F0：0=銀行帳戶（取款）1=身上現金（存款）
    char amount[16] = "0";    // byte_48C3F8
    int limit = 0;            // dword_48C3EC 当前模式余额
    int pressed = -1;         // 仅按下高亮（原版无 hover 效果）
    bool dragging = false;
    std::vector<uint16_t> bg;
    int bgX = 0;
    int bgY = 0;
    int bgW = 0;
    int bgH = 0;
};

int atoiVisit(const char* s) {
    return std::atoi(s);
}

void appendVisitDigit(VisitCtx& ui, char ch) {
    if (ch == 8) { // Backspace（按钮 12 的 ← 与键盘 Backspace 共用）
        const size_t len = std::strlen(ui.amount);
        if (len <= 1) {
            ui.amount[0] = '0';
            ui.amount[1] = 0;
        } else {
            ui.amount[len - 1] = 0;
        }
        return;
    }
    if (ch == 'C') {
        std::snprintf(ui.amount, sizeof(ui.amount), "0");
        return;
    }
    const size_t len = std::strlen(ui.amount);
    if (len >= 10) {
        return;
    }
    if (len == 1 && ui.amount[0] == '0') {
        ui.amount[0] = ch;
        ui.amount[1] = 0;
    } else {
        ui.amount[len] = ch;
        ui.amount[len + 1] = 0;
    }
    if (atoiVisit(ui.amount) > ui.limit) {
        std::snprintf(ui.amount, sizeof(ui.amount), "%d", ui.limit); // 超限重置为上限
    }
}

// [RE 0x436D3A] 金额数字逐位 + 进度条（数字帧 19..28；左端空白清理；滑条动态裁剪）
// 帧布局（smp 实测 panel[24] 30 帧）：帧 0 主面板 320×338 / 帧 1·2·3 顶三按钮高亮
//   80×41·80×41·43×41 / 帧 4 进度条填充 204×26 / 帧 5..16 数字键高亮 33×17 /
//   帧 17·18 确认·結束高亮 49×25·57×25 / 帧 19..28 数字 0..9（18×32）/ 帧 29 融资标志 29×29
void drawVisitAmount(VisitCtx& ui) {
    Surface& dst = ui.app->surface();
    const UiImage& sheet = *ui.sheet;
    const int len = static_cast<int>(std::strlen(ui.amount));
    int x = 304;
    int count = 0;
    for (int i = len - 1; i >= 0 && count < 10; --i, ++count) {
        const int digit = ui.amount[i] - '0';
        const int frame = kDigitFrameBase + digit;
        if (digit >= 0 && digit <= 9 && frame < sheet.frameCount()) {
            blitElement(dst, sheet.frame(frame), x, 172, false);
        }
        x -= 20;
    }
    if (count < 10 && x - 102 > 0) {
        // 清理数字左侧空白：源坐标 = 目标 − 面板原点（即帧 0 对应背景块）
        blitElementRegionOpaque(dst, sheet.frame(0), 122, 172, 62, 101, x - 102, 32, false);
    }
    const int width = ui.limit > 0
                          ? 6 * static_cast<int>(atoiVisit(ui.amount) * kVisitBarScale / ui.limit)
                          : 0;
    const int barW = std::min(std::max(width, 0), kVisitBarMax);
    if (barW > 0) {
        blitElementRegionOpaque(dst, sheet.frame(4), 118, 210, 0, 0, barW, 26, false);
    }
    if (barW < kVisitBarMax) {
        blitElementRegionOpaque(dst, sheet.frame(0), barW + 118, 210, barW + 58, 139,
                                kVisitBarMax - barW, 26, false);
    }
}

void redrawVisit(VisitCtx& ui) {
    Surface& dst = ui.app->surface();
    const UiImage& sheet = *ui.sheet;
    blitElementOpaque(dst, sheet.frame(0), kVisitX, kVisitY);
    // 当前模式按钮常亮：模式 0 → 帧 1 @按钮0；模式 1 → 帧 2 @按钮1
    // （原版初始化 0x437020/0x437055 即高亮当前模式按钮；切换时用帧 0 覆盖旧按钮）
    const int modeBtn = ui.mode == 0 ? 0 : 1;
    blitElement(dst, sheet.frame(modeBtn + 1), kVisitX + kVisitRects[modeBtn].x0,
                kVisitY + kVisitRects[modeBtn].y0, false);
    // 按下高亮：仅鼠标按下时显示（原版 MOUSE_DOWN 画帧 v10+1，松开恢复；无 hover 效果）
    // [RE 0x4371D4] 原版条件 `v10 != 3`：滑条不高亮（否则帧 4 刻度叠加错位一格）；
    //   当前模式按钮不重复高亮；融资中按钮 0 视作模式按钮（不高亮）
    const int lit = ui.pressed;
    const GameState& st = ui.app->gameState();
    const bool financeBlock0 = lit == 0 && st.players[st.currentPlayer].bankFinanceFlags != 0;
    if (lit >= 0 && lit != 3 && lit != modeBtn && !financeBlock0 &&
        sheet.frameCount() > static_cast<size_t>(lit + 1)) {
        blitElement(dst, sheet.frame(lit + 1), kVisitX + kVisitRects[lit].x0,
                    kVisitY + kVisitRects[lit].y0, false);
    }
    // [RE 0x437002] 暫停放款中：帧 29 @(157,141)
    if (st.players[st.currentPlayer].bankFinanceFlags != 0 && sheet.frameCount() > 29) {
        blitElement(dst, sheet.frame(29), 157, 141, false);
    }
    drawVisitAmount(ui);
}

int hitVisitButton(int mx, int my) {
    const int relX = mx - kVisitX;
    const int relY = my - kVisitY;
    for (int i = 0; i < 18; ++i) {
        const VisitRect& r = kVisitRects[i];
        if (relX >= r.x0 && relX <= r.x1 && relY >= r.y0 && relY <= r.y1) {
            return i;
        }
    }
    return -1;
}

void confirmVisitTrade(VisitCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Player& pl = st.players[st.currentPlayer];
    const int v = atoiVisit(ui.amount);
    // 原版确认路径（0x4377E6）无额外音效：按下时已播按键音
    if (v <= 0) {
        app.events().requestExit(0);
        return;
    }
    if (ui.mode != 0) {
        // 现金模式：存款（现金 → 银行）
        pl.bank += v;
        pl.cash -= v;
        ui.limit = pl.cash;
    } else {
        // 银行模式：取款（银行 → 现金）；週轉金结算（0x43784F sub_436B0A(1)）
        pl.bank -= v;
        pl.cash += v;
        ui.limit = pl.bank;
        bankAdvanceSettle(app, 1);
    }
    RICH4_LOGI("bank visit: player %d %s %d -> bank %d cash %d (RE 0x436EF8)",
               st.currentPlayer, ui.mode != 0 ? "deposit" : "withdraw", v, pl.bank, pl.cash);
    app.events().requestExit(0);
}

bool visitHandler(const SDL_Event* event, void* user) {
    auto& ui = *static_cast<VisitCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Player& pl = st.players[st.currentPlayer];
    if (!event) {
        ui.pressed = -1;
        redrawVisit(ui);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            if (!ui.dragging) {
                return true; // 原版无 hover 效果（仅拖动滑块时处理移动，0x436F9D）
            }
            // [RE 0x437422] 滑条：v = (上限/34+1) * (相对x/6+1)，clamp 到上限
            const int mx = static_cast<int>(event->motion.x);
            const int rel = mx - 118;
            int v = 0;
            if (rel <= 0) {
                v = 0;
            } else if (rel >= kVisitBarMax) {
                v = ui.limit;
            } else {
                v = (ui.limit / 34 + 1) * (rel / 6 + 1);
            }
            if (v > ui.limit) {
                v = ui.limit;
            }
            std::snprintf(ui.amount, sizeof(ui.amount), "%d", v);
            redrawVisit(ui);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            const int hit = hitVisitButton(static_cast<int>(event->button.x),
                                           static_cast<int>(event->button.y));
            if (hit < 0) {
                return true;
            }
            if (hit == 3) {
                app.audio().playEffect(kVisitSfxSlider); // [RE 0x43741A] 滑条音效
                ui.dragging = true;
                return true;
            }
            ui.pressed = hit;
            // [RE 0x4373B3/0x4373EB/0x43748B] 音效在**按下**时播：
            //   顶部存/取 = g_uiSoundClick；EXIT/数字键/MAX/確定 = g_effectSlots[0]
            app.audio().playEffect((hit == 0 || hit == 1) ? kVisitSfxClick : kVisitSfxKey);
            // [RE 0x4373D7] 模式切换在按下时生效（0x4373B3/0x4373EB）：
            //   dword_48C3EC = 新余额 + sub_436EDB(1) → **金额重置为 "0"** 并重绘
            if (hit == 0) {
                if (pl.bankFinanceFlags == 0) { // [RE 0x4371E5] 暫停放款中禁用"銀行帳戶"
                    ui.mode = 0;
                    ui.limit = pl.bank;
                }
                std::snprintf(ui.amount, sizeof(ui.amount), "0");
            } else if (hit == 1) {
                ui.mode = 1;
                ui.limit = pl.cash;
                std::snprintf(ui.amount, sizeof(ui.amount), "0");
            }
            redrawVisit(ui);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button == SDL_BUTTON_RIGHT) {
                app.events().requestExit(0); // [RE 0x437920 0x205] 右键退出（原版无音效）
                return true;
            }
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            if (ui.dragging) {
                ui.dragging = false;
                ui.pressed = -1;
                redrawVisit(ui);
                return true;
            }
            const int hit = hitVisitButton(static_cast<int>(event->button.x),
                                           static_cast<int>(event->button.y));
            ui.pressed = -1;
            if (hit < 0) {
                redrawVisit(ui);
                return true;
            }
            if (hit == 2) {
                // [RE 0x437774] 離開（EXIT）：松开退出
                app.events().requestExit(0);
                return true;
            }
            if (hit >= 4 && hit <= 15) {
                appendVisitDigit(ui, kVisitDigits[hit - 4]);
            } else if (hit == 16) {
                // [RE 0x4378DD LABEL_131] MAX：金额直接设为上限（dword_48C3EC）
                std::snprintf(ui.amount, sizeof(ui.amount), "%d", ui.limit);
            } else if (hit == 17) {
                // [RE 0x4377E6] 確定（↲）：确认交易（原版 MOUSE_UP 时 byte_48C40B-1 → 17）
                confirmVisitTrade(ui);
                return true;
            }
            redrawVisit(ui);
            return true;
        }
        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = event->key.key;
            if (k == SDLK_ESCAPE) {
                app.audio().playEffect(4); // 重写增强：ESC 退出（原版仅右键 0x205）
                app.events().requestExit(0);
                return true;
            }
            // [RE 0x43756F LABEL_87] 键盘输入按下音效（g_effectSlots[0]）
            app.audio().playEffect(kVisitSfxKey);
            if (k >= SDLK_0 && k <= SDLK_9) {
                appendVisitDigit(ui, static_cast<char>('0' + (k - SDLK_0)));
                redrawVisit(ui);
                return true;
            }
            if (k >= SDLK_KP_0 && k <= SDLK_KP_9) {
                appendVisitDigit(ui, static_cast<char>('0' + (k - SDLK_KP_0)));
                redrawVisit(ui);
                return true;
            }
            if (k == SDLK_BACKSPACE) {
                appendVisitDigit(ui, 8);
                redrawVisit(ui);
                return true;
            }
            if (k == SDLK_C) {
                appendVisitDigit(ui, 'C');
                redrawVisit(ui);
                return true;
            }
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_M) {
                confirmVisitTrade(ui); // [RE 0x4377E6] 'M'/Enter 确认
                return true;
            }
            return true;
        }
        default:
            break;
    }
    return false;
}

} // namespace

// [RE 0x436B0A] 週轉金结算（详见 bank_dialog.h 注释）
void bankAdvanceSettle(Application& app, int one) {
    GameState& st = app.gameState();
    int owner = -1;
    for (size_t i = 1; i < st.specPts.size(); ++i) {
        if (st.specPts[i].costType == 7 && st.specPts[i].owner != 0) {
            owner = st.specPts[i].owner - 1; // 银行经营者（董事长）
        }
    }
    if (one != 0) {
        if (owner < 0 || owner == st.currentPlayer || st.players[owner].bankAdvance == 0) {
            return;
        }
        const int32_t need = st.players[owner].bankAdvance;
        int32_t othersBank = 0;
        for (int i = 0; i < st.playerCount && i < 4; ++i) {
            if (i != owner && st.players[i].alive) {
                othersBank += st.players[i].bank;
            }
        }
        if (othersBank < need) {
            char text[64];
            std::snprintf(text, sizeof(text), "銀行資金準備\n\n不足%d元\n\n由經營者%s墊", need - othersBank,
                          st.players[owner].name ? st.players[owner].name : ""); // [RE 0x464B75]
            showMessage(app, text, 2500);
            payFromBank(app, owner, need - othersBank);
            st.players[owner].bankAdvance -= (need - othersBank);
            if (st.players[owner].bankAdvance < 0) {
                st.players[owner].bankAdvance = 0;
            }
        }
        return;
    }
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        if (i == owner || !st.players[i].alive || st.players[i].bankAdvance == 0) {
            continue;
        }
        showMessage(app, "銀行經營權易主！", 1500); // [RE 0x464B9E]
        char text[64];
        std::snprintf(text, sizeof(text), "%s\n\n週轉欠款%d元", st.players[i].name ? st.players[i].name : "",
                      st.players[i].bankAdvance);
        showMessage(app, text, 1500);
        payFromBank(app, i, st.players[i].bankAdvance);
        st.players[i].bankAdvance = 0;
    }
}

// [RE 0x4379C9] 路过银行
void bankVisitDialog(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    // [RE 0x4379DA] 拒绝往来中：显示剩余天数（0x4379DA (b&0x7F)+1）
    if ((pl.bankRefuseDays & 0x7F) != 0) {
        char text[64];
        std::snprintf(text, sizeof(text), "銀行坦絕往來\n\n還剩%d天！",
                      (pl.bankRefuseDays & 0x7F) + 1); // [RE 0x464BED]
        showMessage(app, text, 1000);
        return;
    }
    if (pl.alive == 1) {
        auto blob = st.panel.read(24); // [RE 0x437A27] panel.mkf[24]
        if (!blob) {
            RICH4_LOGW("bankVisit: panel.mkf[24] unavailable (RE 0x437A27)");
            return;
        }
        UiImage sheet;
        sheet.load(std::move(*blob));
        if (sheet.frameCount() < 30) {
            RICH4_LOGW("bankVisit: panel.mkf[24] frames %zu < 30", sheet.frameCount());
            return;
        }
        VisitCtx ui;
        ui.app = &app;
        ui.sheet = &sheet;
        // [RE 0x436FDD] 暫停放款中（bankFinanceFlags 非 0）→ 现金模式 + 消息
        ui.mode = pl.bankFinanceFlags != 0 ? 1 : 0;
        ui.limit = ui.mode != 0 ? pl.cash : pl.bank;
        std::snprintf(ui.amount, sizeof(ui.amount), "0");
        ui.bgW = sheet.frame(0).width;
        ui.bgH = sheet.frame(0).height;
        ui.bgX = kVisitX;
        ui.bgY = kVisitY;
        saveRegion(ui.bg, app.surface(), ui.bgX, ui.bgY, ui.bgW, ui.bgH);
        redrawVisit(ui);
        if (pl.bankFinanceFlags != 0) {
            char text[64];
            std::snprintf(text, sizeof(text), "銀行暫停放款\n\n還剩%d天！",
                          (pl.bankFinanceFlags & 0x7F) + 1); // [RE 0x464BD4]
            showMessage(app, text, 1500);
        }
        RICH4_LOGI("bank visit: player %d mode %d limit %d (RE 0x4379C9)", p, ui.mode, ui.limit);
        // [RE 0x4370BA/0x43714D] cursorSelect(27,1,0) 面板交互光标；退出恢复默认箭头 41
        app.cursor().select(27, 1, 0);
        runModal(app, &visitHandler, &ui);
        app.cursor().select(41, 1, 0);
        restoreRegion(app.surface(), ui.bg, ui.bgX, ui.bgY, ui.bgW, ui.bgH);
        return;
    }
    // AI：按 aiCashPct 调整现金/存款比例（0x437ACD..0x437C1C）
    // 目标 = aiCashPct%（月初 1-7 日 ×1.5 / 月末 26-31 日 ×0.5，clamp 0.1..0.9），
    // 与当前现金比例差 ≥0.25（或现金为 0）时调整
    const int total = pl.bank + pl.cash;
    if (total <= 0) {
        return;
    }
    const double curRatio = static_cast<double>(pl.cash) / total;
    double target = pl.aiCashPct / 100.0;
    const int day = static_cast<int>(st.gameDate & 0xFF);
    if (day <= 7) {
        target *= 1.5; // [RE 0x464C10]
    }
    if (day >= 26) {
        target *= 0.5; // [RE 0x464C18]
    }
    if (target >= 1.0) {
        target = 0.9;
    } else if (target <= 0.0) {
        target = 0.1;
    }
    const double diff = curRatio - target;
    if (diff >= 0.25 || diff <= -0.25 || pl.cash == 0) { // [RE 0x464C20/464C28]
        const int cash = static_cast<int>(total * target);
        pl.cash = cash;
        pl.bank = total - cash;
        RICH4_LOGI("bank visit AI: player %d cash %d bank %d (RE 0x437ACD)", p, pl.cash, pl.bank);
    }
}

} // namespace rich4
