#include "game/app/jail_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/confirm_dialog.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/app/message_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x475C04/0x475C08] 保释面板 8 格坐标（(x,y) 交错表拆分为两数组）
constexpr int kBailCellX[8] = {33, 185, 336, 487, 33, 185, 336, 487};
constexpr int kBailCellY[8] = {24, 24, 24, 24, 183, 183, 183, 183};
constexpr int kBailHitW = 121;   // 悬停/点击命中宽（0x43CC3D）
constexpr int kBailHitH = 137;   // 命中高（0x43CC51）
constexpr int kBailPanelW = 100; // 悬停信息板（allocUiElement(100,95) 0x43D382）
constexpr int kBailPanelH = 95;
// [RE 0x475C44] 保释费用（点券：玩家 0-3 = 30，事件槽 NPC 4-7 = 300）
constexpr int kBailCost[8] = {30, 30, 30, 30, 300, 300, 300, 300};

// [RE 0x43C8FB] panel.mkf[63] 帧表（资源+12+12*帧）
enum BailFrame : int {
    kFrameBg = 0,           // +12 全屏底图
    kFrameMsg = 1,          // +24 浮动消息板（点券不足 @(230,300)）
    kFrameInfo = 2,         // +36 悬停信息板 100×95（格 +(20,120)）
    kFrameIncarc = 3,       // +48 在押框（格位）
    kFrameEmpty = 4,        // +60 空格框（格位）
    kFrameAvatarBase = 5,   // 玩家头像 = charIndex + 5（帧 5..16）
    kFrameNpcBase = 17,     // NPC 头像 = i + 13（i=4..7 → 帧 17..20）
    kFramePoints = 21,      // +264 点券板 @(542,432)，数字 @(622,452)
};

struct BailCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    int hover = -1; // dword_48C4C4（-1 无）
    std::vector<uint16_t> hoverBg;
    int hoverBgX = 0;
    int hoverBgY = 0;
    FloatMessage msg; // 0x44EC30/44ECB6/44EE18（点券不足）
};

// ===== 背景保存/恢复（原版 saveBackground word_46CAEC / blitElementFullscreen 恢复）=====

void saveRegion(std::vector<uint16_t>& out, const Surface& dst, int x, int y, int w, int h) {
    out.assign(static_cast<size_t>(w) * h, 0);
    const uint16_t* src = dst.pixels();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            out[static_cast<size_t>(row) * w + col] =
                src[static_cast<size_t>(py) * Surface::kWidth + px];
        }
    }
}

void restoreRegion(Surface& dst, const std::vector<uint16_t>& bg, int x, int y, int w, int h) {
    uint16_t* d = dst.pixels();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            d[static_cast<size_t>(py) * Surface::kWidth + px] =
                bg[static_cast<size_t>(row) * w + col];
        }
    }
}

// ===== 绘制 =====

// [RE 0x43C8FB] 全界面：底图 + 8 格（在押=头像+在押框，否则空格框）+ 点券板/数字
void drawPanel(BailCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0); // [RE 0x4563F5]
    for (int i = 0; i < 8; ++i) {
        const int x = kBailCellX[i];
        const int y = kBailCellY[i];
        if (st.jailFlags[i] != 0) {
            const int avatar =
                i >= 4 ? i + 13 : st.players[i].charIndex + kFrameAvatarBase;
            if (avatar < ui.sheet->frameCount()) {
                blitElement(dst, ui.sheet->frame(avatar), x, y, false);
            }
            blitElement(dst, ui.sheet->frame(kFrameIncarc), x, y, false);
        } else {
            blitElement(dst, ui.sheet->frame(kFrameEmpty), x, y, false);
        }
    }
    blitElement(dst, ui.sheet->frame(kFramePoints), 542, 432, false);
    app.text().setFont(20, 0xFFFFFF, 0x101010, 3, 0); // 20 号白字阴影
    char num[16];
    std::snprintf(num, sizeof(num), "%d", st.players[st.currentPlayer].points); // itoa10
    app.text().drawText(dst, num, 622, 452, 6);
}

void clearHover(BailCtx& ui) {
    if (ui.hover == -1 || ui.hoverBg.empty()) {
        ui.hover = -1;
        return;
    }
    Surface& dst = ui.app->surface();
    restoreRegion(dst, ui.hoverBg, ui.hoverBgX, ui.hoverBgY, kBailPanelW, kBailPanelH);
    ui.hoverBg.clear();
    ui.hover = -1;
}

// [RE 0x43CD04] 悬停信息板：格 +(20,120)，板 100×95；名/「保釋點數」/「%d點」
void drawInfoPanel(BailCtx& ui, int i) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int px = kBailCellX[i] + 20;
    const int py = kBailCellY[i] + 120;
    saveRegion(ui.hoverBg, dst, px, py, kBailPanelW, kBailPanelH);
    ui.hoverBgX = px;
    ui.hoverBgY = py;
    blitElement(dst, ui.sheet->frame(kFrameInfo), px, py, false);
    app.text().setFont(16, 0x101010, 0, 2, 0); // [RE 0x43CD84] 16 号黑粗
    // 名字：玩家 g_players[26*i]；NPC dword_47ED5A[i]（P5 事件槽名表）
    const char* name = i < 4 && st.players[i].name ? st.players[i].name : "";
    app.text().drawText(dst, name, px + 57, py + 26, 2);
    app.text().drawText(dst, "保釋點數", px + 57, py + 48, 2); // [RE 0x465140]
    char cost[24];
    std::snprintf(cost, sizeof(cost), "%d點", kBailCost[i]); // [RE 0x465149 "%d點"]
    app.text().drawText(dst, cost, px + 57, py + 70, 2);
}

// 命中格（0x43CC1C：仅 jailFlags 非 0 的格）
int hitCell(GameState& st, int x, int y) {
    for (int i = 0; i < 8; ++i) {
        if (st.jailFlags[i] == 0) {
            continue;
        }
        if (x >= kBailCellX[i] && x <= kBailCellX[i] + kBailHitW && y >= kBailCellY[i] &&
            y <= kBailCellY[i] + kBailHitH) {
            return i;
        }
    }
    return -1;
}

// ===== 释放执行 =====

// [RE 0x43D1B3] 保释玩家 i（<4）：擦格 + BYTE=0x80 + 清标志 + 扣点券 + 返还记账
// 依据: 0x43D13E blitOpaque 底图帧 0 区域 (格,140×137) + 0x43D189 空格框；
//       0x43CF2C days=BYTE2&0x7F（0 则 1），addPlayerDebt(i,cur,-100×M×days)（欠款矩阵未建模）；
//       0x43CF5F PostMessage(0x205) = 立即退出（原版释放无浮动消息）
void releasePrisoner(BailCtx& ui, int i) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), kBailCellX[i], kBailCellY[i],
                            kBailCellX[i], kBailCellY[i], 140, 137, false);
    blitElement(dst, ui.sheet->frame(kFrameEmpty), kBailCellX[i], kBailCellY[i], false);
    Player& pr = st.players[i];
    const uint32_t sf = pr.stateFlags;
    int days = static_cast<int>((sf >> 16) & 0x7F);
    if ((sf & 0x7F0000u) == 0) {
        days = 1;
    }
    const int pay = 100 * days * st.moneyMul;
    RICH4_LOGI("bail: player %d released by %d, pays back %d to prisoner "
               "(addPlayerDebt matrix TODO, RE 0x43CF4D/0x40DF69)",
               i, st.currentPlayer, pay);
    pr.stateFlags = (sf & 0xFF00FFFFu) | 0x800000u; // BYTE2 = 0x80（到期恢复，走出在 0x40D6BE）
    st.jailFlags[i] = 0;
    st.players[st.currentPlayer].points = static_cast<uint16_t>(st.players[st.currentPlayer].points -
                                                                kBailCost[i]);
}

// [RE 0x43CFB5] 点券不足：浮动消息板 #0002（@(230,300)，文字 y 偏移 -6，16 进制色 0x101010）
void showNotEnough(BailCtx& ui) {
    ui.msg.setup(*ui.sheet, kFrameMsg, 230, 300, 0, -6, 0x101010, 0);
    ui.msg.show(*ui.app, "#0002抱歉！\n你的點數不足！"); // [RE 0x46514E]（#0002 语音自动触发）
}

// ===== 模态处理（0x43CAAB WndProc）=====

bool bailHandler(const SDL_Event* event, void* user) {
    BailCtx& ui = *static_cast<BailCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (!event) {
        ui.hover = -1; // [RE 0x43CB28] WM_USER+1
        ui.msg = FloatMessage();
        drawPanel(ui);
        return true;
    }
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        if (ui.msg.active()) {
            return true; // [RE 0x43CBFC] byte_48C4C8 消息中屏蔽悬停
        }
        int x = 0;
        int y = 0;
        app.mouseLogicalPos(x, y);
        const int hit = hitCell(st, x, y);
        if (hit == ui.hover) {
            return true; // [RE 0x43CC59] 未变化
        }
        clearHover(ui);
        if (hit >= 0) {
            ui.hover = hit;
            drawInfoPanel(ui, hit);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button == SDL_BUTTON_RIGHT) {
            // [RE 0x43D268] 0x205 退出：停消息 + 关绘 + postModalExit
            if (ui.msg.active()) {
                ui.msg.finish(app);
            }
            app.events().requestExit(0);
            return true;
        }
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (ui.msg.active()) {
            ui.msg.finish(app); // [RE 0x43CF01] 点击跳过消息（sub_44EE18(1)）
            return true;
        }
        int x = 0;
        int y = 0;
        app.mouseLogicalPos(x, y);
        const int hit = hitCell(st, x, y);
        if (hit < 0) {
            return true;
        }
        clearHover(ui); // [RE 0x43D01D] 点中格子先恢复悬停板区域
        if (hit >= 4) {
            // TODO(P5 事件槽 NPC): 原版 NPC 格点选 = panel[64] 帧 (i-4) @(365,450) +
            //   浮动消息 dword_475BE2[i]+2 + releaseJailNpc(i) + 扣点券（点数需 >=700）。
            //   当前按占位策略：显示点券不足，不进入释放阶段。
            showNotEnough(ui);
            return true;
        }
        if (st.players[st.currentPlayer].points < kBailCost[hit]) {
            showNotEnough(ui); // [RE 0x43D0DA] 人类检查 >= 费用
            return true;
        }
        if (confirmDialog(app, nullptr, 320, 240)) { // [RE 0x453A32] yesNoDialog(320,240)
            clearHover(ui);
            releasePrisoner(ui, hit);
            app.events().requestExit(0); // [RE 0x43CF5F] PostMessage(0x205)
        }
        return true;
    }
    case kModalTimerEvent: {
        // [RE 0x43CB7A] 100ms tick：消息推进；完成→全重绘；已保释→退出
        if (!ui.msg.active()) {
            return true;
        }
        if (ui.msg.advance(app)) {
            drawPanel(ui); // [RE 0x43CBBA] sub_43C8FB + InvalidateRect
        }
        return true;
    }
    default:
        return true; // 模态：吞其余事件
    }
}

// [RE 0x43D3DF] AI 保释：rand&1 → 按个性收集候选 → 点数检查 → 扣费释放
void bailAi(Application& app) {
    GameState& st = app.gameState();
    if ((std::rand() & 1) == 0) {
        return;
    }
    const int cur = st.currentPlayer;
    Player& pl = st.players[cur];
    int cand[8];
    int n = 0;
    const uint8_t personality = pl.aiPersonality; // 0=乖寶寶(己方) 1=普通人 2=大老奸(敌方)
    if (personality == 2) {
        for (int k = 4; k < 8; ++k) {
            if (st.jailFlags[k]) {
                cand[n++] = k;
            }
        }
    } else {
        for (int j = 0; j < 4; ++j) {
            if (st.jailFlags[j]) {
                cand[n++] = j;
            }
        }
        if (personality == 1 && (std::rand() % 3) == 0) {
            for (int k = 4; k < 8; ++k) {
                if (st.jailFlags[k]) {
                    cand[n++] = k;
                }
            }
        }
    }
    if (n == 0) {
        return;
    }
    const int target = cand[std::rand() % n];
    const int cost = kBailCost[target];
    // [RE 0x43D4FD] 玩家目标 points > 费用（严格）；NPC 目标 points >= 700（费用表 300 另判）
    const int pts = st.players[cur].points;
    const bool ok = target < 4 ? pts > cost : pts >= 700;
    if (!ok) {
        return;
    }
    const char* name =
        target < 4 && st.players[target].name ? st.players[target].name : "NPC"; // dword_47ED5A P5
    char text[64];
    std::snprintf(text, sizeof(text), "保釋%s", name); // [RE 0x465169]
    showMessage(app, text, 1500);
    st.players[cur].points = static_cast<uint16_t>(pts - cost);
    if (target >= 4) {
        RICH4_LOGI("bail AI: NPC %d released (P5 事件槽未建模, RE 0x43D580)", target);
        st.jailFlags[target] = 0; // TODO(P5): releaseJailNpc 事件槽表 16B 记录
        return;
    }
    // [RE 0x43D56E] 玩家释放：BYTE2 = 0x80 + 清标志（下回合走出恢复）
    st.players[target].stateFlags =
        (st.players[target].stateFlags & 0xFF00FFFFu) | 0x800000u;
    st.jailFlags[target] = 0;
    RICH4_LOGI("bail AI: player %d bailed by %d, cost %d (RE 0x43D3DF)", target, cur, cost);
}

} // namespace

// [RE 0x43D304] 監獄格落地（landingEvent case 4）：保释
void jailBailDialog(Application& app) {
    GameState& st = app.gameState();
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (st.jailFlags[i] != 0) {
            any = true;
            break;
        }
    }
    if (!any) {
        return; // [RE 0x43D324] 无在押者
    }
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 9) {
        return;
    }
    if (st.players[cur].alive != 1) {
        bailAi(app);
        return;
    }
    auto blob = st.panel.read(63); // [RE 0x43D353] panel.mkf[63]（dword_48C4B4）
    if (!blob) {
        RICH4_LOGW("jailBail: panel.mkf[63] unavailable (RE 0x43D353)");
        return;
    }
    UiImage sheet;
    if (!sheet.load(std::move(*blob))) {
        RICH4_LOGW("jailBail: panel.mkf[63] load failed (RE 0x43D353)");
        return;
    }
    app.audio().pushSceneMusic(15); // [RE 0x43D38E] musicPlayScene(15)
    BailCtx ui;
    ui.app = &app;
    ui.sheet = &sheet;
    runModal(app, &bailHandler, &ui, 100);
    app.audio().resumeSceneMusic(); // [RE 0x454BCC] sub_454BCC 弹栈恢复
    RICH4_LOGI("jailBail done (RE 0x43D304)");
}

} // namespace rich4
