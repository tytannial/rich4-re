#include "game/app/jail_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/confirm_dialog.h"
#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
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
// [RE 0x47ED5A] 事件槽 NPC 名表见 map_tables.h `kNpcNames`（索引 4..7）
// [RE 0x475BE2] NPC 答谢消息 4 条（監獄/醫院共用；原版 `&dword_475BE2[i]+2`，i=4..7 → 0..3）
//   顺序 = NPC 4..7（小偷/強盜/流氓/間諜），文本含 #NNNN 语音前缀
constexpr const char* kNpcThanks[4] = {
    "#0124謝謝你！你真是\n我的再生父母!", "#0125我先走了！大恩\n大德來日再報!",
    "#0123太感激了！我一\n定會報答你的!", "#0126受人點水之恩，\n必當湧泉已報!"};

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
    UiImage* npcMark = nullptr; // panel[64]（NPC 出狱标记 4 帧：小偷/強盜/流氓/間諜头像）
    int hover = -1; // dword_48C4C4（-1 无）
    std::vector<uint16_t> hoverBg;
    int hoverBgX = 0;
    int hoverBgY = 0;
    std::vector<uint16_t> npcBg; // 标记区域背景（原版 saveBackground）
    int npcBgX = 0;
    int npcBgY = 0;
    int npcBgW = 0;
    int npcBgH = 0;
    bool npcReleased = false; // [RE 0x48C4C9] 已释放 NPC → 答谢消息播完退出
    FloatMessage msg; // 0x44EC30/44ECB6/44EE18（点券不足/答谢）
};

// ===== 背景保存/恢复 → 公共 blit.h saveRegion/restoreRegion（0x451A97/0x451EDB）=====

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
                // [RE 0x43C8FB] sub_456418(帧, dword_475C04/08)：blit 内部按帧锚点 offset
                //   定位（clipBlit：落点 = 实参 − offset），直接用格坐标即可
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
    // 名字：玩家 g_players[26*i]；NPC dword_47ED5A[i]（小偷/強盜）
    const char* name =
        i < 4 ? (st.players[i].name ? st.players[i].name : "") : kNpcNames[i];
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
    // [RE 0x43CF4D] 保释返还（欠款矩阵）：addPlayerDebt(囚犯 i, 保释人 cur, -100×M×days)
    //   负数 = 冲销保释人代付时的记账（clamp≥0；原债为 0 时 addPlayerDebt 跳过）
    addPlayerDebt(st, i, st.currentPlayer, -pay);
    RICH4_LOGI("bail: player %d released by %d, repay %d (RE 0x43CF4D)", i, st.currentPlayer,
               pay);
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
            // [RE 0x43D0DA..0x43D11B] NPC 保释（人类）：点券 < 300 → #0002；否则 yesNo →
            //   擦格（帧0 同区域 140×137 + 帧4 空格框）→ panel[64] 帧(i-4) @(365,450)
            //   → 答谢消息（板帧1 @(210,150)，文本 dword_475BE2[i]）→ releaseJailNpc(i)
            //   → 扣点券 300 + byte_48C4C9（消息播完退出）
            if (st.players[st.currentPlayer].points < kBailCost[hit]) {
                showNotEnough(ui);
                return true;
            }
            if (confirmDialog(app, nullptr, 320, 240)) { // [RE 0x453A32] yesNoDialog(320,240)
                clearHover(ui);
                Surface& dst = app.surface();
                const int gx = kBailCellX[hit];
                const int gy = kBailCellY[hit];
                blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), gx, gy, gx, gy, 140, 137,
                                        false); // [RE 0x43D0F2] blitOpaqueToBackbuffer(帧0)
                blitElement(dst, ui.sheet->frame(kFrameEmpty), gx, gy, false); // [RE 0x43D117] 帧4
                if (ui.npcMark && ui.npcMark->frameCount() > static_cast<size_t>(hit - 4)) {
                    // [RE 0x43D13E] sub_456418(panel[64] 帧(i-4), 365, 450)：blit 内部按帧 offset
                    //   定位；背景保存用实际落点 (365−offsetX, 450−offsetY)
                    const UiFrameView& nf = ui.npcMark->frame(hit - 4);
                    const int nx = 365 - nf.offsetX;
                    const int ny = 450 - nf.offsetY;
                    saveRegion(ui.npcBg, dst, nx, ny, nf.width, nf.height);
                    ui.npcBgX = nx;
                    ui.npcBgY = ny;
                    ui.npcBgW = nf.width;
                    ui.npcBgH = nf.height;
                    blitElement(dst, nf, 365, 450, false);
                }
                ui.msg.setup(*ui.sheet, kFrameMsg, 210, 150, 0, -6, 0x101010, 0); // [RE 0x43D189]
                ui.msg.show(app, kNpcThanks[hit - 4]); // [RE 0x43D19E] dword_475BE2[i]+2
                st.players[st.currentPlayer].points = static_cast<uint16_t>(
                    st.players[st.currentPlayer].points - kBailCost[hit]);
                // [RE 0x43D7BF releaseJailNpc] 填犯人表 → 上路随机游走（行走/抓回链见 498df0）
                releaseEventNpc(app, hit, true);
                ui.npcReleased = true; // [RE 0x48C4C9]
                RICH4_LOGI("jailBail: NPC %d released by %d, cost %d (RE 0x43CFDB, walk 0x43D7BF)",
                           hit, st.currentPlayer, kBailCost[hit]);
            }
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
        // [RE 0x43CB7A] 100ms tick：消息推进；完成→全重绘；已释放 NPC→退出（byte_48C4C9）
        if (!ui.msg.active()) {
            if (ui.npcReleased) { // 消息已被点击跳过
                drawPanel(ui); // [RE 0x43CBBA] jailBailDraw + InvalidateRect
                app.events().requestExit(0); // [RE 0x43D1A3] PostMessage(0x205)
            }
            return true;
        }
        if (ui.msg.advance(app)) {
            drawPanel(ui); // [RE 0x43CBBA] sub_43C8FB + InvalidateRect
            if (ui.npcReleased) {
                app.events().requestExit(0);
            }
        }
        return true;
    }
    case SDL_EVENT_KEY_DOWN: {
        // [PORT] ESC 退出（原版 WndProc 无 WM_KEYDOWN；对齐重写其他模态的统一操作）
        if (event->key.key == SDLK_ESCAPE) {
            app.events().requestExit(0);
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
    if (dbg::roll(dbg::SlotAi, 2) == 0) {
        RICH4_LOGI("bail AI: rand=0 本回合不保释 (RE 0x43D3DF)");
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
        if (personality == 1 && dbg::roll(dbg::SlotAi, 3) == 0) {
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
    const int target = cand[dbg::roll(dbg::SlotAi, n)];
    const int cost = kBailCost[target];
    // [RE 0x43D4FD] 玩家目标 points > 费用（严格）；NPC 目标 points >= 700（费用表 300 另判）
    const int pts = st.players[cur].points;
    const bool ok = target < 4 ? pts > cost : pts >= 700;
    if (!ok) {
        return;
    }
    const char* name = target < 4 ? (st.players[target].name ? st.players[target].name : "")
                                  : kNpcNames[target]; // dword_47ED5A 小偷/強盜
    char text[64];
    std::snprintf(text, sizeof(text), "保釋%s", name); // [RE 0x465169]
    showMessage(app, text, 1500);
    st.players[cur].points = static_cast<uint16_t>(pts - cost);
    if (target >= 4) {
        // [RE 0x43D580] AI 保释 NPC：releaseJailNpc（事件槽记录 + 上路随机游走）
        releaseEventNpc(app, target, true);
        st.jailFlags[target] = 0;
        RICH4_LOGI("bail AI: NPC %d released by %d, cost %d (RE 0x43D580)", target, cur, cost);
        return;
    }
    // [RE 0x43D56E] 玩家释放：BYTE2 = 0x80 + 清标志（下回合走出恢复）
    st.players[target].stateFlags =
        (st.players[target].stateFlags & 0xFF00FFFFu) | 0x800000u;
    st.jailFlags[target] = 0;
    RICH4_LOGI("bail AI: player %d bailed by %d, cost %d (RE 0x43D3DF)", target, cur, cost);
}

// =====================================================================
// 醫院办理出院（0x43E9A4 hospitalVisitDialog / 0x43DA27 WndProc / 0x43D88F 绘制）
// =====================================================================

// [RE 0x475C64/0x475C68] 医院 8 格坐标（两列 × 四行）与命中/板尺寸
constexpr int kHospCellX[8] = {297, 297, 297, 297, 481, 481, 481, 481};
constexpr int kHospCellY[8] = {1, 121, 241, 361, 1, 121, 241, 361};
constexpr int kHospHitW = 147;   // 命中宽（0x43CFFE）
constexpr int kHospHitH = 102;   // 命中高
constexpr int kHospPanelW = 98;  // 悬停信息板 = panel[65] 帧 3 实际 98×95（原版 allocUiElement
constexpr int kHospPanelH = 95;  //   动态读帧头，此处按实测帧头硬编码）
// [RE 0x475CA4] 出院费用（同保释：玩家 30 / NPC 300 点券）
constexpr int kHospCost[8] = {30, 30, 30, 30, 300, 300, 300, 300};

// [RE 0x43D88F] panel.mkf[65] 帧表（31 帧；帧号 = (资源偏移-12)/12）
//   帧内容（PNG 实测）：4 = 左护士全身（局部源 (35,48) 眼 40×22 / (35,70) 嘴 40×18）；
//   5/6 = 左眼 睁/闭；7/8 = 左嘴 张/闭（帧 8 为驻留复位）；9 = 右护士全身（退场 @(91,112)，
//   局部源 (74,46) 眼 40×25 / (74,71) 嘴 40×15）；10/11 = 右眼；12/13 = 右嘴。
//   所有帧头 x/y = 0（SMP 无锚点偏移，原版 blit 落点 = 实参）
enum HospFrame : int {
    kHFg = 0,        // +12 底图
    kHMsg = 1,       // +24 NPC 答谢消息板 @(200,200)
    kHMsgTop = 2,    // +36 消息板 @(8,8)（开场/确认/告别/不足）
    kHInfo = 3,      // +48 悬停信息板（格 -(80,0)）
    kHTitle = 4,     // +60 左护士全身 @(104,110)（原命名"标题板"误；局部源见上）
    kHWalkL1 = 5,    // 左眼 睁 @(139,158)（原命名"行走"误，实为眨眼帧）
    kHWalkL2 = 6,    // 左眼 闭
    kHIdleLFull = 7, // +96 左嘴 张 @(139,180)
    kHIdleLReset = 8,// +108 左嘴 闭（复位）@(139,180)
    kHPartR = 9,     // +120 右护士全身（退场 @(91,112)；局部源见上）
    kHWalkR1 = 10,   // 右眼 睁 @(165,158)
    kHWalkR2 = 11,   // 右眼 闭
    kHIdleRFull = 12,// +156 右嘴 张 @(165,183)
    kHIdleRReset = 13,// +168 右嘴 闭（复位）@(165,183)
    kHAvatarBase = 14, // 玩家头像 = charIndex + 14；NPC 头像 = i + 22（26..29）
    kHNpcBase = 22,
    kHPoints = 30,   // +372 点券板 @(8,432)，数字 @(88,452)
};

// [RE 0x475BE2 表 4 项] NPC 答谢消息见文件顶部 kNpcThanks（監獄/醫院共用）
constexpr const char* kHospOpen = "#0127您好！請問您要替誰\n辦理出院手續？"; // off_475CC4
constexpr const char* kHospOk = "#0129ＯＫ！您的朋友已經\n可以出院了！";     // off_475CCC
constexpr const char* kHospBye = "#0130要保重身體喔！";                      // off_475CD0
constexpr const char* kHospNotEnough = "#0002抱歉！\n你的點數不足！";         // off_475CD4

// 阶段（byte_48C4F2）
enum HospPhase : int { kHPInit = 0, kHPOpen = 1, kHPSelect = 2, kHPDischarge = 4,
                       kHPNotEnough = 5, kHNpcClean = 6, kHPBye = 7 };

struct HospCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    UiImage* npcMark = nullptr; // panel[64]（NPC 出院标记，P5 触发）
    int hover = -1;             // byte_48C4F0
    std::vector<uint16_t> hoverBg;
    int hoverBgX = 0;
    int hoverBgY = 0;
    FloatMessage msg;
    int phase = kHPInit;        // byte_48C4F2
    int nurseState = 0;         // byte_48C4F3：低4位 0/1 行走会话，高4位步 1..3
    int nurseRight = 0;         // byte_48C4F4（告别后退场方向=右）
    int nurseHold = 0;          // byte_48C4F1 小像驻留倒计时
    int target = -1;            // byte_48C4F5
    bool released = false;      // byte_48C4F6
    std::vector<uint16_t> npcBg; // dword_48C4E8 NPC 出院标记背景
    int npcBgX = 0;
    int npcBgY = 0;
    int npcBgW = 0;
    int npcBgH = 0;
};

// [RE 0x43D88F] 医院全绘：底图 + 标题板 + 在院头像 + 点券
void drawPanelHosp(HospCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    blitElementOpaque(dst, ui.sheet->frame(kHFg), 0, 0);
    blitElement(dst, ui.sheet->frame(kHTitle), 104, 110, false);
    for (int i = 0; i < 8; ++i) {
        if (st.hospitalFlags[i] == 0) {
            continue;
        }
        const int avatar = i >= 4 ? i + kHNpcBase : st.players[i].charIndex + kHAvatarBase;
        if (avatar < ui.sheet->frameCount()) {
            // [RE 0x43D88F] sub_456418(帧, dword_475C64/68)：blit 内部按帧锚点 offset
            //   定位（clipBlit：落点 = 实参 − offset），直接用格坐标即可
            blitElement(dst, ui.sheet->frame(avatar), kHospCellX[i], kHospCellY[i], false);
        }
    }
    blitElement(dst, ui.sheet->frame(kHPoints), 8, 432, false);
    app.text().setFont(20, 0xFFFFFF, 0x101010, 3, 0);
    char num[16];
    std::snprintf(num, sizeof(num), "%d", st.players[st.currentPlayer].points);
    app.text().drawText(dst, num, 88, 452, 6);
}

void clearHoverHosp(HospCtx& ui) {
    if (ui.hover == -1) {
        return;
    }
    if (!ui.hoverBg.empty()) {
        restoreRegion(ui.app->surface(), ui.hoverBg, ui.hoverBgX, ui.hoverBgY, kHospPanelW,
                      kHospPanelH);
        ui.hoverBg.clear();
    }
    ui.hover = -1;
}

// [RE 0x43CFED] 悬停信息板：格 -(80,0)，文字 +41/+26/48/70
void drawInfoPanelHosp(HospCtx& ui, int i) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int px = kHospCellX[i] - 80;
    const int py = kHospCellY[i];
    saveRegion(ui.hoverBg, dst, px, py, kHospPanelW, kHospPanelH);
    ui.hoverBgX = px;
    ui.hoverBgY = py;
    blitElement(dst, ui.sheet->frame(kHInfo), px, py, false);
    app.text().setFont(16, 0x101010, 0, 2, 0);
    const char* name =
        i < 4 ? (st.players[i].name ? st.players[i].name : "") : kNpcNames[i];
    app.text().drawText(dst, name, px + 41, py + 26, 2);
    app.text().drawText(dst, "保釋點數", px + 41, py + 48, 2); // [RE 0x4651F9]
    char cost[24];
    std::snprintf(cost, sizeof(cost), "%d點", kHospCost[i]);
    app.text().drawText(dst, cost, px + 41, py + 70, 2);
}

int hitCellHosp(GameState& st, int x, int y) {
    for (int i = 0; i < 8; ++i) {
        if (st.hospitalFlags[i] == 0) {
            continue;
        }
        if (x >= kHospCellX[i] && x <= kHospCellX[i] + kHospHitW && y >= kHospCellY[i] &&
            y <= kHospCellY[i] + kHospHitH) {
            return i;
        }
    }
    return -1;
}

// [RE 0x43EB28..0x43ECF3] 上窗行走 + 下窗小像（同一 100ms tick 先后执行，矩形不重叠）
//   行走：F3 低4位=1 会话中；步 = 高4位 0..2 帧 {5,6,5}(左)/{10,11,10}(右) @(139/165,158)；
//         步 3 起帧 4/9 局部擦除收尾，F3 清零；空闲 1/32 概率开始会话
//   小像（LABEL_49）：语音播放中或驻留中才动作（NPC 收尾期跳过）；驻留结束复位帧 8/13
//         @(179/205,198)；否则 1/4 概率 全帧 7/12 @(139/165,158) 或 局部复位（帧 4/9）
void nurseTick(HospCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const int low = ui.nurseState & 0xF;
    if (low == 0) {
        if ((std::rand() >> 10) == 0) {
            ui.nurseState |= 1; // [RE 0x43EB4A] 1/32 开始行走会话
        }
    } else if (low == 1) {
        const int step = (ui.nurseState >> 4) & 0xF;
        const bool right = ui.nurseRight != 0;
        if (step >= 3) { // 收尾：底图局部擦除（0x43EB8F/EC57）
            if (right) {
                blitElementRegionOpaque(dst, ui.sheet->frame(kHPartR), 165, 158, 74, 46, 40, 25,
                                        false);
            } else {
                blitElementRegionOpaque(dst, ui.sheet->frame(kHTitle), 139, 158, 35, 48, 40, 22,
                                        false);
            }
            ui.nurseState = 0;
        } else {
            static const uint8_t kSeqL[3] = {5, 6, 5};     // byte_475CD8[0..2]
            static const uint8_t kSeqR[3] = {10, 11, 10};  // byte_475CDB[0..2]
            // 原版为 blitElementFullscreen（不透明整矩形，帧含背景无透明像素）
            blitElementOpaque(dst, ui.sheet->frame(right ? kSeqR[step] : kSeqL[step]),
                              right ? 165 : 139, 158);
            ui.nurseState += 16;
        }
    }
    // LABEL_49：(!voicePlaying() && !F1) || F2==6 → 跳过小像
    const bool voice = app.audio().voicePlaying();
    if ((!voice && ui.nurseHold == 0) || ui.phase == kHNpcClean) {
        return;
    }
    const bool right = ui.nurseRight != 0;
    if (ui.nurseHold != 0) {
        if (--ui.nurseHold == 0) {
            // [RE 0x43E198/0x43E2D5] 驻留结束复位帧（帧 8 左 / 帧 13 右，静止嘴部）。
            // 差异: 原版实参用 Rect.right/bottom = (179,198)/(205,198)（疑原版笔误——该点
            //   像素误差 300，正确嘴部区域为 (139,180)/(165,183)，误差 19），重写按美术
            //   意图修正为左上角坐标
            blitElementOpaque(dst, ui.sheet->frame(right ? kHIdleRReset : kHIdleLReset),
                              right ? 165 : 139, right ? 183 : 180);
        }
        return;
    }
    if ((std::rand() >> 11) >= 4) {
        return; // [RE 0x43EC2F] 1/4 概率换姿势
    }
    if ((std::rand() & 1) != 0) {
        // [RE 0x43E132/0x43E26E] 小像区域：左 (139,180)、右 (165,183)（行走帧区域下方，勿用 158）
        blitElementOpaque(dst, ui.sheet->frame(right ? kHIdleRFull : kHIdleLFull),
                          right ? 165 : 139, right ? 183 : 180);
    } else if (right) {
        blitElementRegionOpaque(dst, ui.sheet->frame(kHPartR), 165, 183, 74, 71, 40, 15, false);
    } else {
        blitElementRegionOpaque(dst, ui.sheet->frame(kHTitle), 139, 180, 35, 70, 40, 18, false);
    }
    ui.nurseHold = std::rand() & 7;
    if (ui.nurseHold == 0) {
        ui.nurseHold = 1;
    }
}

// [RE 0x43EE14 case4→医院版 0x43F5B0 区] 消息播完执行出院
void doDischarge(HospCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int i = ui.target;
    blitElementRegionOpaque(dst, ui.sheet->frame(kHFg), kHospCellX[i], kHospCellY[i],
                            kHospCellX[i], kHospCellY[i], 147, 102, false);
    if (i < 4) {
        Player& pr = st.players[i];
        const uint32_t sf = pr.stateFlags;
        int days = static_cast<int>((sf >> 24) & 0x7F);
        if ((sf & 0x7F000000u) == 0) {
            days = 1;
        }
        const int pay = 100 * days * st.moneyMul;
        // [RE 0x43DE8D] 出院返还（欠款矩阵）：addPlayerDebt(病人 i, 探病人 cur, -100×M×days)
        addPlayerDebt(st, i, st.currentPlayer, -pay);
        RICH4_LOGI("hospital visit: patient %d released by %d, repay %d (RE 0x43DE8D)", i,
                   st.currentPlayer, pay);
        pr.stateFlags = (sf & 0x00FFFFFFu) | 0x80000000u; // BYTE3 = 0x80
        st.hospitalFlags[i] = 0;
        ui.released = true;
    } else {
        // [RE 0x43DC76..0x43DE4C] NPC 出院：panel[64] 帧 (i-4) @(420,450)（落点 = 实参 − offset）
        //   + 答谢消息（板帧1 @(200,200)，文本 dword_475BE2[i]）+ releaseHospitalNpc(i)
        //   → byte_48C4F6=1（答谢播完退出）
        ui.phase = kHNpcClean;
        const UiFrameView& nf = ui.npcMark->frame(i - 4 < ui.npcMark->frameCount() ? i - 4 : 0);
        ui.npcBgX = 420 - nf.offsetX;
        ui.npcBgY = 450 - nf.offsetY;
        ui.npcBgW = nf.width;
        ui.npcBgH = nf.height;
        saveRegion(ui.npcBg, dst, ui.npcBgX, ui.npcBgY, ui.npcBgW, ui.npcBgH);
        blitElement(dst, nf, 420, 450, false); // blit 内部按帧 offset 定位
        ui.msg.setup(*ui.sheet, kHMsg, 200, 200, 0, -6, 0x101010, 0);
        ui.msg.show(app, kNpcThanks[i - 4]); // [RE 0x43DE3C] dword_475BE2[i]+2
        // [RE 0x43EE6E releaseHospitalNpc] 填犯人表 → 上路随机游走
        releaseEventNpc(app, i, false);
        ui.released = true;      // [RE 0x48C4F6]
        RICH4_LOGI("hospital visit: NPC %d released by %d, cost %d (RE 0x43DE4C, walk 0x43EE6E)",
                   i, st.currentPlayer, kHospCost[i]);
    }
    st.players[st.currentPlayer].points =
        static_cast<uint16_t>(st.players[st.currentPlayer].points - kHospCost[i]);
}

bool hospHandler(const SDL_Event* event, void* user) {
    HospCtx& ui = *static_cast<HospCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (!event) {
        // [RE 0x43D2ED] WM_USER+1（+0x405）：复位 + 消息板 frame2@(8,8) + 开场白
        ui.hover = -1;
        ui.nurseState = 0;
        ui.nurseHold = 0;
        ui.nurseRight = 0;
        ui.released = false;
        ui.target = -1;
        drawPanelHosp(ui);
        ui.phase = kHPOpen;
        ui.msg.setup(*ui.sheet, kHMsgTop, 8, 8, 0, 0, 0x101010, 0);
        ui.msg.show(app, kHospOpen);
        return true;
    }
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        if (ui.msg.active()) {
            return true; // [RE 0x43D0B4] 消息中禁悬停（sub_44EF3B）
        }
        int x = 0;
        int y = 0;
        app.mouseLogicalPos(x, y);
        const int hit = hitCellHosp(st, x, y);
        if (hit == ui.hover) {
            return true;
        }
        clearHoverHosp(ui);
        if (hit >= 0) {
            ui.hover = hit;
            drawInfoPanelHosp(ui, hit);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
            event->button.button == SDL_BUTTON_RIGHT && ui.phase == kHPSelect) {
            // [RE 0x43EA84] 右键告别：清格区标题复位 → 阶段7 告别消息 → 播完退出
            clearHoverHosp(ui);
            Surface& dst = app.surface();
            blitElementRegionOpaque(dst, ui.sheet->frame(kHFg), 104, 110, 104, 110, 134, 357,
                                    false);
            blitElement(dst, ui.sheet->frame(kHPartR), 91, 112, false); // [RE 0x43EB36 退场板]
            ui.phase = kHPBye;
            ui.nurseRight = 1; // 退场方向右（0x43ED0D byte_48C4F4=1）
            ui.msg.show(app, kHospBye);
            return true;
        }
        if (ui.phase != kHPSelect) {
            if (ui.msg.active()) {
                ui.msg.finish(app); // [RE 0x43D295 LABEL_90] 非选择阶段点击 = 跳过消息
            }
            return true;
        }
        if (event->type != SDL_EVENT_MOUSE_BUTTON_UP) {
            return true; // 选择只响应抬起（监狱版同）
        }
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        int x = 0;
        int y = 0;
        app.mouseLogicalPos(x, y);
        const int hit = hitCellHosp(st, x, y);
        if (hit < 0) {
            return true;
        }
        clearHoverHosp(ui);
        // [RE 0x43E686..0x43E7B8] 玩家/NPC 共用：点券 < 费用 → #0002；否则 yesNo →
        //   阶段4 + #0129「您的朋友已經可以出院了」→ 播完 doDischarge（NPC 走标记+答谢分支）
        if (st.players[st.currentPlayer].points < kHospCost[hit]) {
            ui.phase = kHPNotEnough; // [RE 0x43E7B8 医院版 0x43E77A]
            ui.msg.show(app, kHospNotEnough);
            return true;
        }
        if (confirmDialog(app, nullptr, 320, 240)) { // [RE 0x453A32] yesNoDialog(320,240)
            ui.target = hit;      // [RE 0x48C4F5]
            ui.phase = kHPDischarge;
            ui.msg.show(app, kHospOk); // [RE 0x43E7AB off_475CCC] 消息播完 → doDischarge
        }
        return true;
    }
    case kModalTimerEvent: {
        if (ui.phase == kHPInit) {
            return true;
        }
        // [RE 0x43DB7C] 阶段转换触发 = floatMsgAdvance(0) 返回 1。自然播完或点击
        //   floatMsgAdvance(1) 跳过后（消息不再 active）**都必须**执行转换，
        //   否则点击跳过开场白会卡在 kHPOpen（点格/右键全部被 phase != 2 拦截）
        const bool msgDone = ui.msg.active() ? ui.msg.advance(app) : true;
        if (msgDone) {
            switch (ui.phase) {
            case kHPOpen:
            case kHPNotEnough:
                ui.phase = kHPSelect;
                break;
            case kHPDischarge:
                ui.phase = kHPSelect; // 0x43EFB0：先回选择再执行
                doDischarge(ui);
                break;
            case kHNpcClean: // NPC 答谢播完：恢复标记区 + 板复位
                ui.phase = kHPSelect;
                restoreRegion(app.surface(), ui.npcBg, ui.npcBgX, ui.npcBgY, ui.npcBgW,
                              ui.npcBgH);
                ui.npcBg.clear();
                ui.msg.setup(*ui.sheet, kHMsgTop, 8, 8, 0, 0, 0x101010, 0);
                break;
            case kHPBye: // [RE 0x43D268] 告别播完 → 退出
                app.events().requestExit(0);
                return true;
            default:
                break;
            }
            if (ui.released && ui.phase == kHPSelect) {
                // [RE 0x43EFCC] 已出院 → 下一 tick 退出
                app.events().requestExit(0);
                return true;
            }
        }
        nurseTick(ui);
        return true;
    }
    case SDL_EVENT_KEY_DOWN: {
        // [PORT] ESC 退出（原版 WndProc 无 WM_KEYDOWN；对齐重写其他模态的统一操作）
        if (event->key.key == SDLK_ESCAPE) {
            app.events().requestExit(0);
        }
        return true;
    }
    default:
        return true;
    }
}

// [RE 0x43EAA5] AI 自动办理出院（同監獄版 bailAi：候选按个性、玩家 points>费用、NPC >=700）
void visitAi(Application& app) {
    GameState& st = app.gameState();
    if (dbg::roll(dbg::SlotAi, 2) == 0) {
        RICH4_LOGI("hospital visit AI: rand=0 本回合不办理出院 (RE 0x43EAA5)");
        return;
    }
    const int cur = st.currentPlayer;
    const uint8_t personality = st.players[cur].aiPersonality;
    int cand[8];
    int n = 0;
    if (personality == 2) {
        for (int k = 4; k < 8; ++k) {
            if (st.hospitalFlags[k]) {
                cand[n++] = k;
            }
        }
    } else {
        for (int j = 0; j < 4; ++j) {
            if (st.hospitalFlags[j]) {
                cand[n++] = j;
            }
        }
        if (personality == 1 && dbg::roll(dbg::SlotAi, 3) == 0) {
            for (int k = 4; k < 8; ++k) {
                if (st.hospitalFlags[k]) {
                    cand[n++] = k;
                }
            }
        }
    }
    if (n == 0) {
        return;
    }
    const int target = cand[dbg::roll(dbg::SlotAi, n)];
    const int cost = kHospCost[target];
    const int pts = st.players[cur].points;
    const bool ok = target < 4 ? pts > cost : pts >= 700;
    if (!ok) {
        return;
    }
    const char* name = target < 4 ? (st.players[target].name ? st.players[target].name : "")
                                  : kNpcNames[target]; // dword_47ED5A 流氓/間諜
    char text[64];
    std::snprintf(text, sizeof(text), "保釋%s", name); // [RE 0x465207 医院版同文本]
    showMessage(app, text, 1500);
    st.players[cur].points = static_cast<uint16_t>(pts - cost);
    if (target >= 4) {
        // [RE 0x43EAA5] AI 办理 NPC 出院：releaseHospitalNpc（事件槽记录 + 上路随机游走）
        releaseEventNpc(app, target, false);
        st.hospitalFlags[target] = 0;
        RICH4_LOGI("hospital visit AI: NPC %d released by %d, cost %d (RE 0x43EAA5)", target, cur,
                   cost);
        return;
    }
    st.players[target].stateFlags =
        (st.players[target].stateFlags & 0x00FFFFFFu) | 0x80000000u; // BYTE3 = 0x80
    st.hospitalFlags[target] = 0;
    RICH4_LOGI("hospital visit AI: player %d discharged by %d, cost %d (RE 0x43EAA5)", target, cur,
               cost);
}

} // namespace

// [RE 0x43E9A4] 醫院格落地（landingEvent case 5）：办理出院
void hospitalVisitDialog(Application& app) {
    trace::logf("dialog open name=hospital_visit");
    GameState& st = app.gameState();
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (st.hospitalFlags[i] != 0) {
            any = true;
            break;
        }
    }
    if (!any) {
        RICH4_LOGI("hospitalVisit: 无住院者，静默返回 (RE 0x43E9C4)"); // [RE 0x43E9C4]
        return;
    }
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 9) {
        return;
    }
    if (st.players[cur].alive != 1) {
        RICH4_LOGI("hospitalVisit: AI 玩家 %d 自动办理出院 (RE 0x43EAA5)", cur);
        visitAi(app);
        return;
    }
    RICH4_LOGI("hospitalVisit: 人类玩家 %d 打开出院面板 (RE 0x43E9A4)", cur);
    auto blob = st.panel.read(65); // [RE 0x43E9F3] panel.mkf[65]（dword_48C4D4）
    auto blobNpc = st.panel.read(64); // [RE 0x43EA0D] panel.mkf[64]（dword_48C4D0）
    if (!blob) {
        RICH4_LOGW("hospitalVisit: panel.mkf[65] unavailable (RE 0x43E9F3)");
        return;
    }
    UiImage sheet;
    if (!sheet.load(std::move(*blob))) {
        RICH4_LOGW("hospitalVisit: panel.mkf[65] load failed");
        return;
    }
    UiImage npcMark;
    if (blobNpc) {
        npcMark.load(std::move(*blobNpc));
    }
    app.audio().pushSceneMusic(16); // [RE 0x43EA6F] musicPlayScene(16)
    HospCtx ui;
    ui.app = &app;
    ui.sheet = &sheet;
    ui.npcMark = &npcMark;
    runModal(app, &hospHandler, &ui, 100);
    app.audio().resumeSceneMusic(); // [RE 0x454BCC]
    RICH4_LOGI("hospitalVisit done (RE 0x43E9A4)");
}

// [RE 0x43D304] 監獄格落地（landingEvent case 4）：保释
void jailBailDialog(Application& app) {
    trace::logf("dialog open name=jail_bail");
    GameState& st = app.gameState();
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (st.jailFlags[i] != 0) {
            any = true;
            break;
        }
    }
    if (!any) {
        RICH4_LOGI("jailBail: 无在押者，静默返回 (RE 0x43D324)"); // [RE 0x43D324]
        return;
    }
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 9) {
        return;
    }
    if (st.players[cur].alive != 1) {
        RICH4_LOGI("jailBail: AI 玩家 %d 自动保释 (RE 0x43D3DF)", cur);
        bailAi(app);
        return;
    }
    RICH4_LOGI("jailBail: 人类玩家 %d 打开保释面板 (RE 0x43D304)", cur);
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
    // [RE 0x43D35D] panel.mkf[64]（dword_48C4BC）：NPC 出狱标记 4 帧
    UiImage npcMark;
    if (auto blobNpc = st.panel.read(64)) {
        npcMark.load(std::move(*blobNpc));
    } else {
        RICH4_LOGW("jailBail: panel.mkf[64] unavailable (RE 0x43D35D)");
    }
    app.audio().pushSceneMusic(15); // [RE 0x43D38E] musicPlayScene(15)
    BailCtx ui;
    ui.app = &app;
    ui.sheet = &sheet;
    ui.npcMark = &npcMark;
    runModal(app, &bailHandler, &ui, 100);
    app.audio().resumeSceneMusic(); // [RE 0x454BCC] sub_454BCC 弹栈恢复
    RICH4_LOGI("jailBail done (RE 0x43D304)");
}

} // namespace rich4
