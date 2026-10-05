#include "game/app/month_settle.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/clock.h"
#include "game/core/trace.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/fli.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// ===== panel.mkf[25]（SMP 实测 83 帧，见 docs/reverse/functions/bank-system.md §5）=====
enum SettleFrame : int {
    kFrameBg = 0,          // 640x480 全屏底
    kFrameBoard1 = 1,      // 290x201 浮动消息板（#0092/#0093/#0095/#0096../#0109）
    kFrameInfo2 = 2,       // 278x98 现金/存款/总资产信息板（@440,405）
    kFrameBoard3 = 3,      // 239x193 浮动消息板（#0108）
    kFrameBoard4 = 4,      // 195x142 浮动消息板（#0122）
    kFrameRow11 = 11,      // 160x71 行图（11+i，i = 存活序）
    kFrameLeft19 = 19,     // 186x410 左列板（@24,70）
    kFrameLeft24 = 24,     // 233x410 左列板二（@28,70）
    kFrameHonor32 = 32,    // 173x413 荣誉板（@35,67）
    kFrameDepressed37 = 37, // 313x391 悲情亮相板（@0,89）
    kFramePortraitAfter38 = 38, // 238x415 悲情立绘后板（@17,65）
    kFrameChampion42 = 42, // 195x416 冠军亮相板（@27,64）
    kFrameChampionAfter45 = 45, // 210x420 冠军立绘后板（@6,60）
    kFrameHead47 = 47,     // 头像 47 + 3*charIndex（charIndex 0..7）
};

// [RE 0x475918] 头像 y 表（索引 4*存活数 + i；1 人时原版数据为 12339，绘制越界自动裁剪）
constexpr int16_t kHeadY[24] = {14391, 13369, 13877, 12849, 12339, 12336, 0,    0,
                                120,   360,   0,     0,     100,   240,   380,  0,
                                60,    180,   300,   420,   407,   490,   0,    0};
// [RE 0x475930] 头像落位 x 表
constexpr int16_t kHeadX[24] = {100, 240, 380, 0,   60,  180, 300, 420,
                                407, 490, 0,   0,   324, 407, 490, 0,
                                324, 407, 490, 573, 334, 414, 0,   0};
// [RE 0x475948] 立绘 x 表
constexpr int16_t kPortraitX[20] = {324, 407, 490, 0,   324, 407, 490, 573, 334, 414,
                                    0,   0,   259, 334, 414, 0,   259, 334, 414, 494};
// [RE 0x475960] 立绘帧号表（1 人时原版值 259 越界 → 跳过绘制）
constexpr int16_t kPortraitFrame[20] = {259, 334, 414, 0,   259, 334, 414, 494, 16, 17,
                                        0,   0,   15,  16,  17,  0,   15,  16,  17, 18};

// [RE 0x4759F7] FLC 全身像参数（每角色 6 项：冠军 x/y/flags + 悲情 x/y/flags）
constexpr int kFlcParams[8][6] = {
    {-50, -94, 1539, -77, -148, 3},      {-48, -97, 1027, -48, -90, 1539},
    {-38, -97, 1539, -50, -87, 1539},    {-31, -61, 1539, -64, -109, 771},
    {-41, -87, 1027, -42, -77, 1539},    {-44, -76, 1027, -31, -60, 515},
    {-36, -100, 1539, -36, -103, 1539},  {-50, -89, 1027, -50, -87, 3},
};

// [RE 0x475988/0x4759B8] 按 charIndex 的悲情/冠军语音标签（'#NNNN' 触发语音）
const char* kRoleMsgDepressed[8] = {"#0096約翰喬", "#0097沙隆巴斯", "#0098忍太郎",
                                    "#0099錢夫人", "#0100阿土伯",   "#0101莎拉公主",
                                    "#0102宮本寶藏", "#0103糖糖"};
const char* kRoleMsgChampion[8] = {"#0110約翰喬", "#0111沙隆巴斯", "#0112忍太郎",
                                   "#0113錢夫人", "#0114阿土伯",   "#0115莎拉公主",
                                   "#0116宮本寶藏", "#0117糖糖"};

// 音效（Effect.mkf 索引；原版 tables dword_475B17={27,28,60,-1}/475B27={60,-1}/475B1F={28,60,-1}）
constexpr int kSfxIntro = 27;  // 前奏揭晓
constexpr int kSfxChampion = 28; // 冠军亮相
constexpr int kSfxReveal = 60; // 悲情亮相

// 消息文本（[RE 0x464D60..] 区，BIG5 已转 UTF-8）
constexpr const char* kMsg1 = "#0092各位客戶辛苦了！\n又到了每月銀行\n結算的日子。";
constexpr const char* kMsg2 = "#0093大富翁銀行將\n根據您的存款，\n加發１０％的儲金利息。";
constexpr const char* kMsgDepressed = "#0095本月悲情人物是．．。";
constexpr const char* kMsgCheer = "#0108別灰心，再加油喔！";
constexpr const char* kMsgChampion = "#0109本月冠軍是．．。";
constexpr const char* kMsgFinal = "#0122其他人還要\n更努力喔";

constexpr uint32_t kTextBlack = 0x101010; // [RE 0x101010]
constexpr uint32_t kTextRed = 0xFF0000;   // [RE 0xFF0000] "貸款中"

// [RE 0x437E61] 状态机 byte_48C42A（0/1/2/5..9/15..19/22；3/4 保留不用）
enum SettleState : int {
    kStMsg1 = 1,
    kStMsg2 = 2,
    kStWaitDepressed = 5,
    kStDepressedIntro = 6,
    kStDepressedPortrait = 7,
    kStDepressedReason = 8,
    kStDepressedFlc = 9,
    kStWaitChampion = 15,
    kStChampionIntro = 16,
    kStChampionPortrait = 17,
    kStChampionReason = 18,
    kStChampionFlc = 19,
    kStFinal = 22,
};

struct SettleCtx {
    Application* app = nullptr;
    UiImage* sheet = nullptr;
    int state = 0;
    int counter = 0;      // dword_48C425（等待拍数；22 时递减）
    bool skip = false;    // byte_48C42E（点击跳过后快速推进）
    int alive = 0;        // byte_48C420（存活数）
    int order[4] = {};    // byte_48C418（存活玩家号，按序）
    int depressed = -1;   // byte_48C42F（悲情人物列表序；-1 = 无）
    int rich = 0;         // byte_48C430（资产冠军列表序）
    int landedY[4] = {};  // word_48C40C（头像底部对齐 y = offsetY + 330 − height）
    // 行文字（开场记录：存款 / 利息 = bank*0.1 / 貸款中；发息前的值）
    char rowBank[4][32] = {};
    char rowInterest[4][24] = {};
    bool rowLoan[4] = {};
    FloatMessage msg;
    std::vector<uint16_t> flcBg; // FLC 播放背景快照
};

// [RE 0x452793] 千分位（同 bank_stay_dialog.cpp 实现）
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

void formatMoneyDollar(char* out, int32_t value) {
    out[0] = '$';
    formatMoney(out + 1, value);
}

const Player& listPlayer(const GameState& st, const SettleCtx& ui, int listIdx) {
    return st.players[ui.order[listIdx]];
}

int charIndexOf(const GameState& st, const SettleCtx& ui, int listIdx) {
    return listPlayer(st, ui, listIdx).charIndex;
}

// 头像落点 y：原版 = 帧头 +6(offsetY) + 330 − 帧头 +2(height) → 头像底边对齐 330
int computeLandedY(const UiImage& sheet, int charIndex) {
    const UiFrameView& fr = sheet.frame(kFrameHead47 + 3 * charIndex);
    return fr.offsetY + 330 - fr.height;
}

void blitHead(SettleCtx& ui, int listIdx, int frameBase, int x, int y) {
    const GameState& st = ui.app->gameState();
    const int ci = charIndexOf(st, ui, listIdx);
    blitElement(ui.app->surface(), ui.sheet->frame(frameBase + 3 * ci), x, y, false);
}

void blitPortrait(SettleCtx& ui, int listIdx) {
    const int f = kPortraitFrame[4 * ui.alive + listIdx];
    if (f < 0 || f >= ui.sheet->frameCount()) {
        return; // 1 人时原版表值越界（259）
    }
    blitElement(ui.app->surface(), ui.sheet->frame(f), kPortraitX[4 * ui.alive + listIdx], 0,
                false);
}

// [RE 0x439BFA] 结息列表行文字（原版预画在行图帧内；重写画在行图屏幕位置）
// 位置 = 行图落点 (360, y-36) + 帧内 (4,6)/(154,6)/(4,46)/(154,46)
void drawRowText(SettleCtx& ui, int listIdx, int rowX, int rowY) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    app.text().setFont(18, kTextBlack, 0, 2, 0);
    app.text().drawText(dst, "存款：", rowX + 4, rowY + 6, 0);
    app.text().drawText(dst, ui.rowBank[listIdx], rowX + 154, rowY + 6, 1);
    app.text().drawText(dst, "利息：", rowX + 4, rowY + 46, 0);
    if (ui.rowLoan[listIdx]) {
        app.text().setFont(18, kTextRed, 0, 2, 0); // [RE 0x464E97] 貸款中（红）
        app.text().drawText(dst, "貸款中", rowX + 154, rowY + 46, 1);
        app.text().setFont(18, kTextBlack, 0, 2, 0);
    } else {
        app.text().drawText(dst, ui.rowInterest[listIdx], rowX + 154, rowY + 46, 1);
    }
}

// [RE 0x439BFA] 开场：帧 0 + 帧 19 @(24,70) + 逐存活玩家头像 @(600, kHeadY)
void drawOpening(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    blitElement(dst, ui.sheet->frame(kFrameLeft19), 24, 70, false);
    for (int i = 0; i < ui.alive; ++i) {
        blitHead(ui, i, kFrameHead47, 600, kHeadY[4 * ui.alive + i]);
    }
}

// [RE 0x437E61 状态 1→2] 左列换板（帧 0 补丁 + 帧 24）
void drawAfterMsg1(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), 24, 70, 24, 70, 186, 410, false);
    blitElement(dst, ui.sheet->frame(kFrameLeft24), 28, 70, false);
}

// [RE 0x437E61 状态 2→3] 结算清单：右侧恢复帧 0 条 + 行图/头像 + 行文字
void drawList(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), 540, 0, 540, 0, 100, 480, false);
    for (int i = 0; i < ui.alive; ++i) {
        const int y = kHeadY[4 * ui.alive + i];
        blitHead(ui, i, kFrameHead47 + 2, 600, y); // 头像帧 49 = 47+2（原版 3*charIdx+49）
        blitElement(dst, ui.sheet->frame(kFrameRow11 + i), 360, y - 36, false);
        drawRowText(ui, i, 360, y - 36);
    }
}

// [RE 0x437C25] 等待期重绘：帧 0 + 帧 19 + 头像落位（(bits&0x30) 档位重画）
void drawQuiet(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    blitElement(dst, ui.sheet->frame(kFrameLeft19), 24, 70, false);
    for (int i = 0; i < ui.alive; ++i) {
        blitHead(ui, i, kFrameHead47, kHeadX[4 * ui.alive + i], ui.landedY[i]);
    }
}

// [RE 0x437E61 状态 5→6] 悲情前奏：恢复左列 + 帧 32 @(35,67)
void drawDepressedIntro(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), 24, 70, 24, 70, 186, 410, false);
    blitElement(dst, ui.sheet->frame(kFrameHonor32), 35, 67, false);
}

// [RE 0x437E61 状态 6→7] 悲情亮相：帧 0 + 立绘 + 帧 37 @(0,89) + 全员头像
void drawDepressedPortrait(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    blitPortrait(ui, ui.depressed);
    blitElement(dst, ui.sheet->frame(kFrameDepressed37), 0, 89, false);
    for (int i = 0; i < ui.alive; ++i) {
        blitHead(ui, i, kFrameHead47, kHeadX[4 * ui.alive + i], ui.landedY[i]);
    }
}

// [RE 0x437E61 状态 7→8] 悲情理由页：帧 0 + 帧 32 + 立绘 + 帧 2 + 文本 + 非冠军头像
void drawDepressedReason(SettleCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const Player& pl = listPlayer(app.gameState(), ui, ui.depressed);
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    blitElement(dst, ui.sheet->frame(kFrameHonor32), 35, 67, false);
    blitPortrait(ui, ui.depressed);
    blitElement(dst, ui.sheet->frame(kFrameInfo2), 440, 405, false);
    app.text().setFont(16, kTextBlack, 0, 2, 1);
    app.text().drawText(dst, "獲獎原因：", 320, 370, 0);
    app.text().drawText(dst, "本月意外損失：", 320, 388, 0);
    char amount[32];
    formatMoneyDollar(amount, pl.monthSettleA);
    app.text().drawText(dst, amount, 560, 388, 1);
    app.text().drawText(dst, "本月意外之財：", 320, 406, 0);
    formatMoneyDollar(amount, pl.monthSettleB);
    app.text().drawText(dst, amount, 560, 406, 1);
    app.text().drawText(dst, "本月倒楣天數：", 320, 424, 0);
    std::snprintf(amount, sizeof(amount), "%d天", static_cast<int>(pl.byte66));
    app.text().drawText(dst, amount, 560, 424, 1);
    for (int i = 0; i < ui.alive; ++i) {
        if (i != ui.depressed) {
            blitHead(ui, i, kFrameHead47, kHeadX[4 * ui.alive + i], ui.landedY[i]);
        }
    }
}

// [RE 0x437E61 状态 8→9] FLC 后：帧 0 补丁 (35,67,173,413) + 帧 38 @(17,65)
void drawAfterFlcDepressed(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), 35, 67, 35, 67, 173, 413, false);
    blitElement(dst, ui.sheet->frame(kFramePortraitAfter38), 17, 65, false);
}

// [RE 0x437E61 状态 15→16] 冠军前奏：恢复左列 + 帧 38 @(17,65)
void drawChampionIntro(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), 24, 70, 24, 70, 186, 410, false);
    blitElement(dst, ui.sheet->frame(kFramePortraitAfter38), 17, 65, false);
}

// [RE 0x437E61 状态 16→17] 冠军亮相：帧 0 + 立绘 + 帧 42 @(27,64) + 全员头像
void drawChampionPortrait(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.sheet->frame(kFrameBg), 0, 0);
    blitPortrait(ui, ui.rich);
    blitElement(dst, ui.sheet->frame(kFrameChampion42), 27, 64, false);
    for (int i = 0; i < ui.alive; ++i) {
        blitHead(ui, i, kFrameHead47, kHeadX[4 * ui.alive + i], ui.landedY[i]);
    }
}

// [RE 0x437E61 状态 17→18] 冠军理由页：在上一状态画面（帧 0 + 立绘 + 帧 42 主持人板 + 头像）
//   之上叠加 立绘 + 帧 2 + 现金/存款/总资产 + 非冠军头像（原版无清屏——0x437E61 中
//   `(*(...100))` 为 DDraw Lock 而非清屏；帧 0 背景由状态 16→17 保留）
void drawChampionReason(SettleCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const Player& pl = listPlayer(app.gameState(), ui, ui.rich);
    blitPortrait(ui, ui.rich);
    blitElement(dst, ui.sheet->frame(kFrameInfo2), 440, 405, false);
    app.text().setFont(16, kTextBlack, 0, 2, 1);
    app.text().drawText(dst, "獲獎原因：", 320, 370, 0);
    char amount[32];
    app.text().drawText(dst, "現金：", 320, 388, 0);
    formatMoneyDollar(amount, pl.cash);
    app.text().drawText(dst, amount, 560, 388, 1);
    app.text().drawText(dst, "存款：", 320, 406, 0);
    formatMoneyDollar(amount, pl.bank);
    app.text().drawText(dst, amount, 560, 406, 1);
    app.text().drawText(dst, "總資產：", 320, 424, 0);
    formatMoneyDollar(amount, playerTotalAssets(app, ui.order[ui.rich]));
    app.text().drawText(dst, amount, 560, 424, 1);
    for (int i = 0; i < ui.alive; ++i) {
        if (i != ui.rich) {
            blitHead(ui, i, kFrameHead47, kHeadX[4 * ui.alive + i], ui.landedY[i]);
        }
    }
}

// [RE 0x437E61 状态 18→19] FLC 后：帧 0 补丁 (27,64,195,416) + 帧 45 @(6,60)
void drawAfterFlcChampion(SettleCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementRegionOpaque(dst, ui.sheet->frame(kFrameBg), 27, 64, 27, 64, 195, 416, false);
    blitElement(dst, ui.sheet->frame(kFrameChampionAfter45), 6, 60, false);
}

// [RE 0x437D1A] 悲情人物：分数 = 2500×物價指數×倒楣天數 + 本月意外損失 − 意外之財 + 10×攻击加成；
//   最高分需比第二名高 40% 才返回（否则 -1 = 不演出）
int findDepressed(const GameState& st, const SettleCtx& ui) {
    int64_t score[4] = {};
    for (int i = 0; i < ui.alive; ++i) {
        const Player& pl = listPlayer(st, ui, i);
        score[i] = 2500LL * st.moneyMul * pl.byte66 + pl.monthSettleA - pl.monthSettleB +
                   10 * pl.luckA;
    }
    int64_t best = 0;
    int bestIdx = 0;
    for (int i = 0; i < ui.alive; ++i) {
        if (best < score[i]) {
            best = score[i];
            bestIdx = i;
        }
    }
    int64_t second = 0;
    for (int i = 0; i < ui.alive; ++i) {
        if (score[i] == best) {
            score[i] = 0;
        }
        if (second < score[i]) {
            second = score[i];
        }
    }
    if (best != 0 && second != 0 &&
        static_cast<double>(best - second) / static_cast<double>(best) > 0.4) {
        return bestIdx;
    }
    return -1;
}

// [RE 0x437DFE] 资产冠军：playerTotalAssets 最高
int findRich(Application& app, const SettleCtx& ui) {
    GameState& st = app.gameState();
    int32_t best = 0;
    int bestIdx = 0;
    for (int i = 0; i < ui.alive; ++i) {
        const int32_t assets = playerTotalAssets(app, ui.order[i]);
        if (best < assets) {
            best = assets;
            bestIdx = i;
        }
    }
    (void)st;
    return bestIdx;
}

// [RE 0x45144F] FLC 全身像播放（阻塞；背景 = 当前结息画面快照 + 色键叠加）
//   原版实参 flags = kFlcParams（dword_475A0B/0x4759F7 表，值 3/515/1027/1539 **全部含 bit1**
//   = g_flcInterruptible）→ 悲情/冠军全身像动画可被左键/Esc/Enter/Space 提前打断（2026-09-29 回验）
void playSettleFlc(SettleCtx& ui, int mkfIndex, int x, int y) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    auto blob = st.data.read(static_cast<size_t>(mkfIndex));
    FliDecoder flc;
    if (!blob || !flc.open(std::move(*blob))) {
        RICH4_LOGW("monthSettle: data.mkf[%d] FLC unavailable (RE 0x45144F)", mkfIndex);
        return;
    }
    Surface& dst = app.surface();
    // [RE 0x4528B9] 清进入前残留 + [NEW] 演出段隐藏光标 + 暂停背景音乐（出口成对恢复）
    app.consumeSkipInput();
    app.cursor().setHidden(dst, true);
    app.audio().pushMusicPause();
    ui.flcBg.assign(dst.pixels(), dst.pixels() + static_cast<size_t>(dst.width()) *
                                                       dst.height());
    const int interval = std::max(1, flc.speedMs());
    // [NEW M4-A2] 帧节拍 = 绝对截止时刻（绘制耗时计入帧间隔）
    uint64_t deadline = nowMs();
    while (flc.nextFrame()) {
        deadline += static_cast<uint64_t>(interval);
        std::memcpy(dst.pixels(), ui.flcBg.data(),
                    sizeof(uint16_t) * dst.width() * dst.height());
        // [NEW M4-A2] FLC 当前帧按画布 scale 缩放叠加（色键透明；scale=1 逐像素等价）
        blitScaled(dst, reinterpret_cast<const uint8_t*>(flc.pixels()), flc.width() * 2, nullptr,
                   x, y, 0, 0, flc.width(), flc.height(), false, false, flc.colorKey());
        // [NEW] 全量回卷 + 叠加 = 本帧背景已重建，光标保存块作废（防陈旧 uncompose 写回闪块）
        app.cursor().invalidate();
        app.renderFrame();
        app.pumpEvents();
        // [RE 0x45144F] g_flcInterruptible（flags bit1，本处表值全含）：帧间延时循环检测
        //   打断输入提前结束（停在当前帧，调用方 drawAfterFlc* 照常重铺终态画面）
        bool skipHit = false;
        for (;;) {
            if (app.consumeSkipInput()) {
                skipHit = true;
                break;
            }
            if (!frameWaitStep(deadline)) {
                break;
            }
            app.audio().update();
        }
        if (skipHit) {
            trace::logf("flc skip idx=%d (monthSettle RE 0x45144F)", mkfIndex);
            break;
        }
    }
    app.cursor().setHidden(dst, false);
    app.audio().popMusicPause(); // [NEW] 恢复背景音乐（原位续播）
    RICH4_LOGI("monthSettle: FLC data.mkf[%d] played @(%d,%d) (RE 0x45144F)", mkfIndex, x, y);
}

// [RE 0x437E61 0x275 WM_TIMER] 状态机（100ms tick；floatMsgAdvance 驱动）
bool settleHandler(const SDL_Event* event, void* user) {
    SettleCtx& ui = *static_cast<SettleCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    if (!event) { // WM_USER+1 (0x401) + 0x405
        for (int i = 0; i < ui.alive; ++i) {
            const Player& pl = listPlayer(st, ui, i);
            char digits[24];
            formatMoney(digits, pl.bank);
            std::snprintf(ui.rowBank[i], sizeof(ui.rowBank[i]), "$%s", digits);
            formatMoney(digits, static_cast<int32_t>(pl.bank * 0.1)); // [RE 0x464EA0] 0.1
            std::snprintf(ui.rowInterest[i], sizeof(ui.rowInterest[i]), "$%s", digits);
            ui.rowLoan[i] = pl.loan != 0;
        }
        drawOpening(ui);
        ui.msg.setup(*ui.sheet, kFrameBoard1, 190, 10, 0, -30, kTextBlack, 0);
        ui.state = kStMsg1;
        if (st.settings[1] != 0) {
            ui.msg.show(app, kMsg1);
        }
        RICH4_LOGI("monthSettle: enter alive=%d settings=%d (RE 0x439BFA/0x437E61)",
                   ui.alive, st.settings[1]);
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: {
        const bool done = ui.msg.advance(app);
        // [RE 0x437E61] 等待期（5/15）：每 tick 计数；20 拍重绘（sub_437C25）、30 拍推进
        if (ui.state == kStWaitDepressed || ui.state == kStWaitChampion) {
            if (!done) {
                return true;
            }
            ++ui.counter;
            if (ui.counter == 20) {
                drawQuiet(ui);
            }
            if (ui.counter >= 30 || ui.skip) {
                ui.skip = false;
                if (ui.state == kStWaitDepressed) {
                    drawDepressedIntro(ui);
                    ui.state = kStDepressedIntro;
                    ui.msg.show(app, kMsgDepressed);
                } else {
                    drawChampionIntro(ui);
                    ui.state = kStChampionIntro;
                    ui.msg.show(app, kMsgChampion);
                }
                app.audio().playEffect(kSfxIntro);
            }
            return true;
        }
        if (!done) {
            return true;
        }
        ui.skip = false;
        switch (ui.state) {
        case kStMsg1: {
            drawAfterMsg1(ui);
            ui.state = kStMsg2;
            ui.msg.show(app, kMsg2);
            break;
        }
        case kStMsg2: {
            // [RE 0x437E61 状态 2→3] 发息（贷款者不发；int 截断）+ 排行 + 后续分支
            for (int i = 0; i < ui.alive; ++i) {
                Player& pl = st.players[ui.order[i]];
                if (pl.loan == 0) {
                    pl.bank = static_cast<int32_t>(pl.bank * 1.1); // [RE 0x464E88] dbl_464E88=1.1
                }
            }
            drawList(ui);
            ui.depressed = findDepressed(st, ui);
            ui.rich = findRich(app, ui);
            if (st.settings[1] == 0 || ui.depressed == ui.rich || ui.depressed == -1) {
                ui.state = kStWaitChampion; // 无悲情演出 → 直接冠军链
                ui.counter = 0;
            } else {
                ui.state = kStWaitDepressed;
                ui.counter = 0;
            }
            RICH4_LOGI("monthSettle: settle done depressed=%d rich=%d (RE 0x437D1A/0x437DFE)",
                       ui.depressed, ui.rich);
            break;
        }
        case kStDepressedIntro: { // 6→7
            drawDepressedPortrait(ui);
            ui.state = kStDepressedPortrait;
            const int ci = charIndexOf(st, ui, ui.depressed);
            ui.msg.show(app, kRoleMsgDepressed[ci]);
            app.audio().playEffect(kSfxReveal);
            break;
        }
        case kStDepressedPortrait: { // 7→8
            drawDepressedReason(ui);
            ui.state = kStDepressedReason;
            break;
        }
        case kStDepressedReason: { // 8→9：播放悲情全身像 FLC（417+2*charIndex）
            const int ci = charIndexOf(st, ui, ui.depressed);
            const int x = kFlcParams[ci][3] + kHeadX[4 * ui.alive + ui.depressed];
            const int y = kFlcParams[ci][4] + 330;
            playSettleFlc(ui, 417 + 2 * ci, x, y);
            drawAfterFlcDepressed(ui);
            ui.state = kStDepressedFlc;
            break;
        }
        case kStDepressedFlc: { // 9→15：#0108 → 冠军链
            ui.msg.setup(*ui.sheet, kFrameBoard3, 190, 10, 20, 0, kTextBlack, 0);
            ui.msg.show(app, kMsgCheer);
            ui.state = kStWaitChampion;
            ui.counter = 19;
            break;
        }
        case kStChampionIntro: { // 16→17
            drawChampionPortrait(ui);
            ui.state = kStChampionPortrait;
            const int ci = charIndexOf(st, ui, ui.rich);
            ui.msg.show(app, kRoleMsgChampion[ci]);
            app.audio().playEffect(kSfxChampion);
            break;
        }
        case kStChampionPortrait: { // 17→18
            drawChampionReason(ui);
            ui.state = kStChampionReason;
            break;
        }
        case kStChampionReason: { // 18→19：播放冠军全身像 FLC（416+2*charIndex）
            const int ci = charIndexOf(st, ui, ui.rich);
            const int x = kFlcParams[ci][0] + kHeadX[4 * ui.alive + ui.rich];
            const int y = kFlcParams[ci][1] + 330;
            playSettleFlc(ui, 416 + 2 * ci, x, y);
            drawAfterFlcChampion(ui);
            ui.state = kStChampionFlc;
            break;
        }
        case kStChampionFlc: { // 19→22：#0122
            ui.msg.setup(*ui.sheet, kFrameBoard4, 213, 37, 20, 0, kTextBlack, 0);
            ui.msg.show(app, kMsgFinal);
            ui.state = kStFinal;
            ui.counter = 10;
            break;
        }
        case kStFinal: { // 22：10 拍后退出
            if (--ui.counter <= 0) {
                app.events().requestExit(0);
            }
            break;
        }
        default:
            break;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_KEY_DOWN: {
        // [RE 0x437E61 0x202/0x205] 点击/按键：跳过当前消息 + 快速推进
        if (ui.msg.active()) {
            ui.msg.finish(app);
        }
        ui.skip = true;
        return true;
    }
    default:
        return true;
    }
    return true;
}

} // namespace

// [RE 0x439BFA] 月初结息入口（advanceDay 新月 1 号）
void monthlySettle(Application& app) {
    GameState& st = app.gameState();
    auto blob = st.panel.read(25); // [RE 0x439BFA] panel.mkf[25]
    if (!blob) {
        RICH4_LOGW("monthSettle: panel.mkf[25] unavailable (RE 0x439BFA)");
        return;
    }
    UiImage sheet;
    if (!sheet.load(std::move(*blob)) || sheet.frameCount() < 83) {
        RICH4_LOGW("monthSettle: panel.mkf[25] frames %d < 83", sheet.frameCount());
        return;
    }
    SettleCtx ui;
    ui.app = &app;
    ui.sheet = &sheet;
    for (int i = 0; i < st.playerCount && i < 4; ++i) {
        if (st.players[i].alive != 0) {
            ui.order[ui.alive++] = i;
        }
    }
    if (ui.alive == 0) {
        return; // 无人存活（原版不会到此处）
    }
    for (int i = 0; i < ui.alive; ++i) {
        ui.landedY[i] = computeLandedY(sheet, charIndexOf(st, ui, i));
    }
    app.audio().pushSceneMusic(9); // [RE 0x439BFA] musicPlayScene(9)
    runModal(app, &settleHandler, &ui, 100); // [RE 0x437E61] SetTimer 100ms
    app.audio().resumeSceneMusic();          // [RE 0x439BFA] musicStackPopRestore
    // [RE 0x439BFA 尾] 清零月度累计（byte_496BAA / dword_496BC4 / dword_496BC8）
    for (int i = 0; i < ui.alive; ++i) {
        Player& pl = st.players[ui.order[i]];
        pl.byte66 = 0;
        pl.monthSettleA = 0;
        pl.monthSettleB = 0;
    }
    RICH4_LOGI("monthSettle: exit (RE 0x439BFA/0x437E61)");
}

} // namespace rich4
