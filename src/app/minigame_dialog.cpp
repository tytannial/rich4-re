// 特殊地点三种小游戏（landingEvent case 6/7/8）交互版 + 共享 else 兜底。
// 依据: sub_415215/0x4154DC/0x4155FC + 共享 else 0x415457 反编译；
//       详见 docs/reverse/functions/41982d-p2-events.md §6。
//
// 交互小游戏公共流程（三游戏同构）：
//   门控（人类 && settings[1] 動畫過程）→ 载 Panel.mkf 资源 → musicPlayScene 压栈 →
//   runModal(WndProc) → musicStackPopRestore → 返回得分（→点券，0x41B152）。
//   开场 0x401：倒计时 + 开场计数 + 初始绘制；开场计数归 0 → 0x405：播 panel[78] 标题
//   FLC（透明全屏）→ 换游戏光标 → 游戏体；结束：大字得分（panel[79] 帧 10+d，
//   x=353-66*len/2，y=150）停留 2s → postModalExit。
//   HUD y=421（panel[79] 小数字帧 0..9 不透明）：倒计时 @(49,69,94)+单位帧0 @(114)；
//   四计数 @(185,205)/(276,296)/(367,387)/(458,478)；总分 @(529|549,569,589)。
//   音乐：挖 12 / 气 11 / 接 10。音效：挖 {11,12,13,14,16,17,18,15}、气 {19,20,21}、
//   接 {22,23,24,15}（投掷 22 / 预留 23 / 引线嘶嘶循环 24 / 爆炸 15）。
//
// case 6 企鵝挖寶（digWndProc 0x414858，100ms×150）：9×9 等距网格（静态坐标表
//   0x474D7C，x=0 无效、高 nibble=1 冰屋邻格）；digInitLayout 0x412014 埋宝 5 类
//   {3,12,3,9,1}（Σ28，v=(64递减)*rand()>>15 随机落位）。点击 panel[81] RAW 掩码
//   （值=row*9+col）→ digSetTarget 0x41211C（主轴 1 格/tick 定点路径）→ 行走
//   panel[82] 8 方向×4 帧（4 tick/格）→ 到达停走音播 12 → 挖掘 panel[83]（phase2
//   画洞帧9、phase3 判定）：type1 火煤球 → 黑化 15 tick 直接结束（音 15）；
//   type2..5 → 计数++ + 宝物音（表 byte_475051={0,0,4,5,5,6}→16/17/17/18）+
//   展示 panel[86+type-1] 7 tick。得分 = 5*Cnt(t2)+12*Cnt(t3)+8*Cnt(t4)+20*Cnt(t5)。
//   时间到：>55 → 好结局 panel[84] 4 轮（音 13 循环）；40..55 → 站立 15 tick；
//   <40 → 坏结局 panel[85]（音 14 循环）。结算大字 2s（点击可跳过）。
//
// case 8 喜從天降（moneyWndProc 0x414FCD，50ms×360=18s）：财神 panel[93]（19 帧）
//   在 y=126 左右巡游（state 机：右行+12/tick 每 rand%5 帧撒袋、边界 110/530、转身 5
//   帧；70% 概率在朝中心方向时触发云 panel[94] y=125 帧 0..11，第 8 帧投**炸弹袋**）。
//   钱袋 4 槽类型 0..3（rand%20：<9→3(1分),9..14→2(3分),15..17→1(5分),18..19→0(10分)）
//   + 4=炸弹；y=100 出生 vy=-16 上抛、+2/tick 重力 cap16、y>=130 匀速
//   {24,18,15,12,15}；水平摆幅=(x-320)*50/210 按 (y-130)/250 进度放大；缩放 blit
//   0.5x→1.0x（32768*(1+进度)）+ 旋转帧 0..7（byte_475010/0x4568C2）。娃娃
//   panel[100+char]（0..4 表情、5..24 行走，half=(帧数-5)>>1）在 y=380 跟随鼠标
//   （10px/tick，死区 ±8，dir 不清零=原版 quirk）；钱袋中心入娃娃帧包围盒即接取。
//   接炸弹 → data[526] 爆炸 FLC（@dollX-55,295）+ 表情 4 + 直接结算。
//   时间到（phase=1）→ 财神转身离场、袋全落 → 评级表情 >=60→3/>=50→2/>=40→0/
//   <40→1 → 大字 2s。

#include "game/app/minigame.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/event_stack.h"
#include "game/app/item_lines.h"
#include "game/app/message_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
#include "game/core/clock.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/cursor.h"
#include "game/render/fli.h"
#include "game/render/surface.h"
#include "game/render/ui_image.h"
#include "game/core/rng.h"

namespace rich4 {

namespace {

// ==================== 通用辅助 ====================

// [RE 0x4568C2] SPR 16.16 定点缩放 blit（65536=1.0x；接钱钱袋 0.5x→1.0x 透视）
// 依据: 0x4568C2 反编译（目标矩形 = (x-offX, y-offY, scale*w>>16, scale*h>>16)，
//   源步长 = 65536²/scale 定点累加，8bit 索引查调色板，0 透明，全裁剪返回 false）
bool blitSprScaled(Surface& dst, const UiImage& spr, int frame, int x, int y, uint32_t scale) {
    if (!spr.isSprite() || frame < 0 || frame >= spr.frameCount()) {
        return false;
    }
    const uint16_t* palette = spr.palette();
    const uint8_t* src = spr.frameBytes(frame);
    const UiFrameView& f = spr.frame(frame);
    const int dx = x - f.offsetX;
    const int dy = y - f.offsetY;
    const int sw = static_cast<int>((scale * f.width) >> 16);
    const int sh = static_cast<int>((scale * f.height) >> 16);
    if (dx >= dst.width() || dy >= dst.height() || sw <= 0 || sh <= 0) {
        return false;
    }
    const uint32_t step = static_cast<uint32_t>(0x100000000ULL / scale);
    for (int row = 0; row < sh; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        const int sy = static_cast<int>((static_cast<uint64_t>(row) * step) >> 16);
        if (sy >= f.height) {
            continue;
        }
        const uint8_t* srow = src + static_cast<size_t>(sy) * f.width;
        uint16_t* d = dst.pixels() + static_cast<size_t>(py) * dst.width();
        uint32_t sxAcc = 0;
        for (int col = 0; col < sw; ++col) {
            const int px = dx + col;
            if (px >= 0 && px < dst.width()) {
                const int sxi = static_cast<int>(sxAcc >> 16);
                if (sxi < f.width) {
                    const uint8_t idx = srow[sxi];
                    if (idx != 0) {
                        d[px] = palette[idx];
                    }
                }
            }
            sxAcc += step;
        }
    }
    return true;
}

// [RE 0x414789] 结算大字：得分 "%d"，每位 = panel[79] 帧 10+d，x=353-66*len/2，y=150
// 依据: 原版 sub_456418 = 0x455C52 blitElement 薄封装（**色键透明**，非不透明）
void drawBigScore(Surface& dst, const UiImage& digits, int score) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", score);
    const int len = static_cast<int>(std::strlen(buf));
    int x = 353 - 66 * len / 2;
    for (int i = 0; i < len; ++i) {
        blitElement(dst, digits.frame(10 + buf[i] - '0'), x, 150, false);
        x += 66;
    }
}

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

// [RE 0x450F04] FLC 当前帧落到 (x,y)（透明=色键 0）
void blitFlcFrame(Surface& dst, const FliDecoder& flc, int x, int y, bool transparent) {
    if (!flc.valid()) {
        return;
    }
    const uint16_t key = flc.colorKey();
    const uint16_t* src = flc.pixels();
    for (int row = 0; row < flc.height(); ++row) {
        const int py = y + row;
        if (py < 0 || py >= dst.height()) {
            continue;
        }
        for (int col = 0; col < flc.width(); ++col) {
            const int px = x + col;
            if (px < 0 || px >= dst.width()) {
                continue;
            }
            const uint16_t c = src[static_cast<size_t>(row) * flc.width() + col];
            if (transparent && c == key) {
                continue;
            }
            dst.pixels()[static_cast<size_t>(py) * dst.width() + px] = c;
        }
    }
}

// HUD 小数字（panel[79] 帧 0..9）：原版 blitElementFullscreen(0x4563F5) = **不透明**
//   像素拷贝——数字黑色填充像素值恰为 0，色键 blit 会把填充当透明吃掉只剩白线框。
//   （结算大字帧 10..19 才用 sub_456418 色键 blitElement，背景需透明）
void drawHudNumber(Surface& dst, const UiImage& digits, const char* buf, const int* xs, int n) {
    for (int i = 0; i < n; ++i) {
        blitElementOpaque(dst, digits.frame(buf[i] - '0'), xs[i], 421);
    }
}

// ==================== 企鵝挖寶（case 6）====================

// [RE 0x474D7C] 9×9 等距格坐标静态表（x=0 无效；flag=1 冰屋邻格，企鹅站上时
//   中央 (320,225) 画冰屋遮挡）
struct DigCell {
    int16_t x;
    int16_t y;
    uint8_t flag;
};
const DigCell kDigCells[9][9] = {
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {80, 153, 0}, {128, 129, 0}, {176, 105, 0}, {224, 81, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {32, 225, 0}, {80, 201, 0}, {128, 177, 0}, {176, 153, 0}, {224, 129, 0}, {272, 105, 0}, {320, 81, 0}, {0, 0, 0}},
    {{0, 0, 0}, {80, 249, 0}, {128, 225, 0}, {176, 201, 0}, {224, 177, 0}, {272, 153, 0}, {320, 129, 0}, {368, 105, 0}, {416, 81, 0}},
    {{80, 297, 0}, {128, 273, 0}, {176, 249, 0}, {224, 225, 1}, {272, 201, 1}, {320, 177, 1}, {368, 153, 0}, {416, 129, 0}, {464, 105, 0}},
    {{128, 321, 0}, {176, 297, 0}, {224, 273, 0}, {272, 249, 0}, {0, 0, 0}, {368, 201, 1}, {416, 177, 0}, {464, 201, 0}, {512, 129, 0}},
    {{176, 345, 0}, {224, 321, 0}, {272, 297, 0}, {320, 273, 0}, {368, 249, 0}, {416, 225, 1}, {464, 201, 0}, {512, 177, 0}, {560, 153, 0}},
    {{224, 369, 0}, {272, 345, 0}, {320, 321, 0}, {368, 297, 0}, {416, 273, 0}, {464, 249, 0}, {512, 225, 0}, {560, 201, 0}, {0, 0, 0}},
    {{0, 0, 0}, {320, 369, 0}, {368, 345, 0}, {416, 321, 0}, {464, 297, 0}, {512, 273, 0}, {560, 249, 0}, {608, 225, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {416, 369, 0}, {464, 345, 0}, {512, 321, 0}, {560, 297, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
};
const int kDigCounts[5] = {3, 12, 3, 9, 1};      // [RE 0x411FC8]
const int kDigSoundTable[8] = {11, 12, 13, 14, 16, 17, 18, 15};  // [RE 0x475057]
const int kDigTreasureSfx[6] = {0, 0, 4, 5, 5, 6};  // [RE 0x475051] 宝物→槽
constexpr int kSfxDigWalk = 11;
constexpr int kSfxDigArrive = 12;
constexpr int kSfxDigGood = 13;
constexpr int kSfxDigBad = 14;
constexpr int kSfxDigFire = 15;
constexpr int kDigCountdown = 150;  // [RE 0x415341]
constexpr int kDigIntro = 10;       // [RE 0x41534B]

struct DigCtx {
    Application* app = nullptr;
    UiImage digits;     // panel[79]
    UiImage board;      // panel[80]：0 背景/1 站立/2 黑化/3 冰屋/4..8 宝物堆/9 洞
    UiImage walk;       // panel[82] 8 方向×4 帧
    UiImage digAnim;    // panel[83]
    UiImage endGood;    // panel[84]
    UiImage endBad;     // panel[85]
    UiImage treasure[5];// panel[86..90] 宝物展示
    std::vector<uint8_t> mask;  // panel[81] RAW 640×480（值=row*9+col）
    FliDecoder title;
    bool titleOk = false;
    uint8_t cellType[9][9] = {};
    bool dug[9][9] = {};
    double posX = 2.0, posY = 6.0;   // 企鹅格坐标（原版 16.16 定点）
    double stepX = 0, stepY = 0;
    double nextX = 2.0, nextY = 6.0;
    int targetCol = 0, targetRow = 0;
    bool retarget = false;           // byte_48BD5B
    int state = 0;                   // dword_48BCCC
    int anim = 0;                    // 低 nibble（case1/4/5/6 帧计数）
    int phase = 0;                   // case2/3 步进 0..3
    int dir = 0;                     // 0..7
    int showType = 0, showAnim = 0;  // 宝物展示
    int showX = 0, showY = 0;
    int cnt[6] = {};                 // 下标 2..5 = g_miniCntA..D
    int countdown = 0;
    int intro = 0;
    int gamePhase = 0;               // g_miniPhase
    int uiState = 0;                 // 0=intro 1=title 2=play 3=settle(并入 gamePhase)
};

// [RE 0x412014] 埋宝：5 类逐批随机落位（v0=64 递减，pick=(v0*rand())>>15 选第 n 个空格）
void digInitLayout(DigCtx& ui) {
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            ui.cellType[r][c] = 0;
            ui.dug[r][c] = false;
        }
    }
    int v0 = 64;
    for (int t = 0; t < 5; ++t) {
        for (int k = 0; k < kDigCounts[t]; ++k) {
            const int pick = static_cast<int>((static_cast<int64_t>(v0) * dbg::raw(dbg::SlotMinigame)) >> 15);
            int n = 0;
            bool placed = false;
            for (int r = 0; r < 9 && !placed; ++r) {
                for (int c = 0; c < 9 && !placed; ++c) {
                    if (kDigCells[r][c].x != 0 && ui.cellType[r][c] == 0) {
                        if (n == pick) {
                            ui.cellType[r][c] = static_cast<uint8_t>(t + 1);
                            --v0;
                            placed = true;
                        }
                        ++n;
                    }
                }
            }
        }
    }
}

// [RE 0x41211C] 设目标：pos 取整+0.5，主轴步进 1 格/tick
bool digSetTarget(DigCtx& ui, int col, int row) {
    if (row < 0 || row >= 9 || col < 0 || col >= 9 || kDigCells[row][col].x == 0) {
        return false;
    }
    const int curCol = static_cast<int>(ui.posX);
    const int curRow = static_cast<int>(ui.posY);
    if (col == curCol && row == curRow) {
        return false;
    }
    ui.posX = curCol + 0.5;
    ui.posY = curRow + 0.5;
    ui.targetCol = col;
    ui.targetRow = row;
    const int dx = col - curCol;
    const int dy = row - curRow;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    if (ady >= adx) {
        ui.stepX = dx / static_cast<double>(ady);
        ui.stepY = dy / static_cast<double>(ady);
    } else {
        ui.stepX = dx / static_cast<double>(adx);
        ui.stepY = dy / static_cast<double>(adx);
    }
    return true;
}

// [RE 0x412287] 走一步；目标角落无效 → 主轴推进、副轴整数卡住时强制 +1 格 + retarget
void digStep(DigCtx& ui) {
    const double nx = ui.posX + ui.stepX;
    const double ny = ui.posY + ui.stepY;
    if (kDigCells[static_cast<int>(ny)][static_cast<int>(nx)].x != 0) {
        ui.nextX = nx;
        ui.nextY = ny;
        return;
    }
    const int sxs = ui.stepX < 0 ? -1 : 1;
    const int sys = ui.stepY < 0 ? -1 : 1;
    if (std::abs(ui.stepX) >= 0.999) {  // 主轴 x
        ui.nextX = nx;
        if (static_cast<int>(ui.nextY) == static_cast<int>(ny)) {
            ui.nextY += sys;
        }
    } else {  // 主轴 y
        ui.nextY = ny;
        if (static_cast<int>(ui.nextX) == static_cast<int>(nx)) {
            ui.nextX += sxs;
        }
    }
    ui.retarget = true;
}

int digScore(const DigCtx& ui) {
    // [RE 0x413DA6] 5*CntA + 20*CntD + 12*CntB + 8*CntC
    return 5 * ui.cnt[2] + 20 * ui.cnt[5] + 12 * ui.cnt[3] + 8 * ui.cnt[4];
}

// [RE 0x4124C8] 挖宝 tick 状态机
void digTick(DigCtx& ui) {
    Application& app = *ui.app;
    bool showJustSet = false;
    switch (ui.state) {
    case 0:
    case 6:
        break;
    case 1:  // 火煤球：站立 3 tick → 黑化，共 15 tick → 结束（计数在下方共享块）
        break;
    case 2: {  // 行走（4 tick/格）
        // 方向公式以 0x412658 反汇编为准（Hex-Rays 条件翻转 quirk）：
        //   col差>0 → 3-row差；col差==0 → row差>0?1:5；col差<0 → (row差+7)&7
        //   帧表 0=下 1=右下 2=右 3=右上 4=上 5=左上 6=左 7=左下（看图确认）
        const int dcx = static_cast<int>(ui.nextX) - static_cast<int>(ui.posX);
        const int dr = static_cast<int>(ui.nextY) - static_cast<int>(ui.posY);
        if (dcx > 0) {
            ui.dir = 3 - dr;
        } else if (dcx == 0) {
            ui.dir = dr > 0 ? 1 : 5;
        } else {
            ui.dir = (dr + 7) & 7;
        }
        ++ui.phase;
        ui.phase &= 3;
        if (ui.phase == 0) {
            ui.posX = ui.nextX;
            ui.posY = ui.nextY;
            if (static_cast<int>(ui.nextX) == ui.targetCol &&
                static_cast<int>(ui.nextY) == ui.targetRow) {
                app.audio().stopEffect(kSfxDigWalk);  // [RE 0x4127E2]
                app.audio().playEffect(kSfxDigArrive);
                ui.state = 3;
                ui.anim = 0;
            } else {
                if (ui.retarget) {
                    digSetTarget(ui, ui.targetCol, ui.targetRow);
                    ui.retarget = false;
                }
                digStep(ui);
            }
        }
        break;
    }
    case 3: {  // 挖掘（4 tick）：phase2 画洞（**无论有无宝**，[RE 0x41287A]）、
              // phase3 判定宝物
        if (ui.phase == 2) {
            ui.dug[static_cast<int>(ui.posY)][static_cast<int>(ui.posX)] = true;
        }
        if (ui.phase == 3) {
            const int c = static_cast<int>(ui.posX);
            const int r = static_cast<int>(ui.posY);
            if (ui.cellType[r][c] != 0) {
                ui.showType = ui.cellType[r][c];
                ui.showX = kDigCells[r][c].x;
                ui.showY = kDigCells[r][c].y;
                ui.cellType[r][c] = 0;
            } else {
                ui.showType = 0;
            }
        }
        ++ui.phase;
        ui.phase &= 3;
        if (ui.phase == 0) {
            if (ui.showType == 1) {  // [RE 0x41299A] 火煤球 → 黑化结束流程
                ui.state = 1;
                ui.anim = 0;
                app.audio().playEffect(kSfxDigFire);
                ui.showType = 0;
            } else if (ui.showType > 1) {
                ui.state = 0;
                app.audio().playEffect(kDigSoundTable[kDigTreasureSfx[ui.showType]]);
                ++ui.cnt[ui.showType];
                ui.showAnim = 1;  // 宝物展示 7 tick（帧 0..6 钳 5）
                showJustSet = true;
            } else {
                ui.state = 0;
            }
        }
        break;
    }
    case 4:  // 好结局动画（[RE 0x412A16] panel[84] 低 nibble 帧、满 4 轮停音 13）
        ++ui.anim;
        if ((ui.anim & 0xF) >= ui.endGood.frameCount()) {
            ui.anim = (ui.anim & 0xF0) + 16;
        }
        if ((ui.anim & 0xF0) >= 0x40) {
            app.audio().stopEffect(kSfxDigGood);
            ui.gamePhase = 1;
        }
        break;
    case 5:
        ++ui.anim;
        if ((ui.anim & 0xF) >= ui.endBad.frameCount()) {
            ui.anim = (ui.anim & 0xF0) + 16;
        }
        if ((ui.anim & 0xF0) >= 0x40) {
            app.audio().stopEffect(kSfxDigBad);
            ui.gamePhase = 1;
        }
        break;
    default:
        break;
    }
    if (ui.state == 1 || ui.state == 6) {  // [RE 0x412B93] 15 tick → 结束
        if (++ui.anim >= 15) {
            ui.gamePhase = 1;
        }
    }
    if (ui.showAnim > 0 && !showJustSet && ++ui.showAnim >= 7) {
        ui.showAnim = 0;
    }
}

// [RE 0x41461B/0x4124C8/0x413A4A] 挖宝全场景即时重绘
void drawDig(DigCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.board.frame(0), 0, 0);
    // 宝物堆只在开场（intro + 标题 FLC）显示——**记忆游戏**：ready go 后堆消失，
    // 玩家凭记忆选格（[RE 0x4148B2 0x405 → digInitDraw(0) 重铺背景不画堆]）
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (ui.cellType[r][c] != 0 && ui.uiState < 2) {
                blitElement(dst, ui.board.frame(ui.cellType[r][c] + 3), kDigCells[r][c].x,
                            kDigCells[r][c].y, false);
            } else if (ui.dug[r][c]) {
                blitElement(dst, ui.board.frame(9), kDigCells[r][c].x, kDigCells[r][c].y, false);
            }
        }
    }
    const int cc = static_cast<int>(ui.posX);
    const int cr = static_cast<int>(ui.posY);
    const int nc = static_cast<int>(ui.nextX);
    const int nr = static_cast<int>(ui.nextY);
    const int cellX = kDigCells[cr][cc].x;
    const int cellY = kDigCells[cr][cc].y;
    switch (ui.state) {
    case 2: {
        const int p = ui.phase;
        const int x = cellX + p * ((kDigCells[nr][nc].x - cellX) >> 2);
        const int y = cellY + p * ((kDigCells[nr][nc].y - cellY) >> 2);
        blitSpriteFrame(dst, ui.walk, 4 * ui.dir + p, x, y);
        break;
    }
    case 3:
        blitSpriteFrame(dst, ui.digAnim, 4 * ui.dir + ui.phase, cellX, cellY);
        break;
    case 1:
        blitElement(dst, ui.board.frame(ui.anim < 3 ? 1 : 2), cellX, cellY, false);
        break;
    case 4: {
        const int f = ui.anim & 0xF;
        blitSpriteFrame(dst, ui.endGood, f < ui.endGood.frameCount() ? f : ui.endGood.frameCount() - 1,
                        cellX, cellY);
        break;
    }
    case 5: {
        const int f = ui.anim & 0xF;
        blitSpriteFrame(dst, ui.endBad, f < ui.endBad.frameCount() ? f : ui.endBad.frameCount() - 1,
                        cellX, cellY);
        break;
    }
    default:
        blitElement(dst, ui.board.frame(1), cellX, cellY, false);
        break;
    }
    if (ui.showAnim > 0 && ui.showType > 0) {
        const UiImage& tr = ui.treasure[ui.showType - 1];
        const int f = ui.showAnim - 1 < tr.frameCount() ? ui.showAnim - 1 : tr.frameCount() - 1;
        blitSpriteFrame(dst, tr, f, ui.showX, ui.showY);
    }
    if (kDigCells[cr][cc].flag != 0) {  // [RE 0x412CC9] 冰屋遮挡
        blitElement(dst, ui.board.frame(3), 320, 225, false);
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", ui.countdown);
    static const int kTimeX[3] = {49, 69, 94};
    drawHudNumber(dst, ui.digits, buf, kTimeX, 3);
    blitElementOpaque(dst, ui.digits.frame(0), 114, 421);
    struct { int v; int x0; int x1; } rows[4] = {
        {ui.cnt[5], 185, 205}, {ui.cnt[3], 276, 296}, {ui.cnt[4], 367, 387}, {ui.cnt[2], 458, 478},
    };
    for (auto& r : rows) {
        std::snprintf(buf, sizeof(buf), "%02d", r.v);
        blitElementOpaque(dst, ui.digits.frame(buf[0] - '0'), r.x0, 421);
        blitElementOpaque(dst, ui.digits.frame(buf[1] - '0'), r.x1, 421);
    }
    const int score = digScore(ui);
    std::snprintf(buf, sizeof(buf), "%03d", score);
    static const int kScoreX[3] = {549, 569, 589};
    drawHudNumber(dst, ui.digits, buf, kScoreX, 3);
    if ((ui.state == 1 || ui.state == 6) && (ui.anim & 0xF) == 4 || ui.state == 4 ||
        ui.state == 5) {
        drawBigScore(dst, ui.digits, score);
    }
    if (ui.uiState == 1) {
        blitFlcFrame(dst, ui.title, 0, 0, true);  // READY GO 标题 FLC（0x405 阻塞播）
    }
}

// [RE 0x414858] digWndProc（100ms）
bool digHandler(const SDL_Event* event, void* user) {
    DigCtx& ui = *static_cast<DigCtx*>(user);
    Application& app = *ui.app;
    if (!event) {  // [RE 0x415341 0x401]
        digInitLayout(ui);
        std::memset(ui.cnt, 0, sizeof(ui.cnt));
        ui.posX = 2.0;
        ui.posY = 6.0;
        ui.nextX = ui.posX;
        ui.nextY = ui.posY;
        ui.stepX = ui.stepY = 0;
        ui.retarget = false;
        ui.state = 0;
        ui.anim = 0;
        ui.phase = 0;
        ui.dir = 0;
        ui.showType = 0;
        ui.showAnim = 0;
        ui.countdown = kDigCountdown;
        ui.intro = kDigIntro;
        ui.gamePhase = 0;
        ui.uiState = 0;
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent:  // [RE 0x414904 0x113]
        if (ui.uiState == 0) {
            if (--ui.intro <= 0) {
                ui.uiState = 1;
            }
        } else if (ui.uiState == 1) {  // 标题 FLC（原版 0x405 阻塞）
            if (!ui.titleOk || decodeFlcOnce(ui.title)) {
                ui.uiState = 2;
                app.cursor().select(42, 1, 0);  // [RE 0x414A95] 铲子光标
            }
        } else if (ui.gamePhase == 2) {  // 结算倒计时 20 tick（点击可跳）
            if (--ui.countdown <= 0) {
                app.events().requestExit(digScore(ui));
                return true;
            }
        } else {
            if (ui.countdown > 0 && --ui.countdown == 0) {
                // [RE 0x414995] 时间到 → 档位动画
                app.audio().stopEffect(kSfxDigWalk);
                const int s = digScore(ui);
                if (s > 55) {
                    ui.state = 4;
                    app.audio().playEffectLooping(kSfxDigGood);
                } else if (s >= 40) {
                    ui.state = 6;
                } else {
                    ui.state = 5;
                    app.audio().playEffectLooping(kSfxDigBad);
                }
                ui.anim = 0;
            }
            digTick(ui);
            if (ui.gamePhase == 1) {  // [RE 0x414A16] 结束动画完成 → 结算
                ui.gamePhase = 2;
                ui.countdown = 20;
                app.cursor().select(41, 1, 0);
            }
        }
        drawDig(ui);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:  // [RE 0x414AB0 0x201/0x203]
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (ui.gamePhase == 2) {
                ui.countdown = 1;  // 结算期点击跳过
            } else if (ui.uiState == 2 && ui.state == 0 && ui.gamePhase == 0) {
                const int mx = static_cast<int>(event->button.x);
                const int my = static_cast<int>(event->button.y);
                if (mx >= 0 && my >= 0 && mx < 640 && my < 480) {
                    const int v = ui.mask[static_cast<size_t>(my) * 640 + mx];
                    if (digSetTarget(ui, v % 9, v / 9)) {
                        digStep(ui);
                        ui.state = 2;
                        ui.phase = 0;
                        app.audio().playEffectLooping(kSfxDigWalk);  // [RE 0x414B55]
                    }
                }
            }
        }
        return true;
    default:
        return true;
    }
}

// [RE 0x415215] miniGameDigRun：企鵝挖寶交互版
// 依据: 0x415215 反编译（资源 panel[78/79/80..90]、音效表 g_digSoundTable、
//   musicPlayScene(12)、runModal digWndProc 0x414858 100ms）；玩法见 §6
int miniGameDigRun(Application& app) {
    GameState& st = app.gameState();
    DigCtx ui;
    ui.app = &app;
    bool ok = true;
    auto blobD = st.panel.read(79);
    if (blobD) {
        ok = ui.digits.load(std::move(*blobD)) && ui.digits.frameCount() >= 20;
    } else {
        ok = false;
    }
    struct { UiImage* img; int idx; } loads[] = {{&ui.board, 80}, {&ui.walk, 82},
                                                 {&ui.digAnim, 83}, {&ui.endGood, 84},
                                                 {&ui.endBad, 85}};
    for (auto& l : loads) {
        auto blob = st.panel.read(l.idx);
        if (!blob || !l.img->load(std::move(*blob))) {
            ok = false;
        }
    }
    for (int i = 0; i < 5; ++i) {
        auto blob = st.panel.read(86 + i);
        if (!blob || !ui.treasure[i].load(std::move(*blob))) {
            ok = false;
        }
    }
    auto blobMask = st.panel.read(81);  // RAW 640×480 8bit 点击掩码
    if (!blobMask || blobMask->size() < 640u * 480u) {
        ok = false;
    }
    if (!ok) {
        RICH4_LOGW("minigame: dig panel resources unavailable (RE 0x415215)");
        return -1;
    }
    ui.mask = std::move(*blobMask);
    auto blob78 = st.panel.read(78);
    if (blob78) {
        ui.titleOk = ui.title.open(std::move(*blob78));
    }
    app.audio().pushSceneMusic(12);  // [RE 0x41539C]
    const int score = runModal(app, &digHandler, &ui, 100);
    app.audio().resumeSceneMusic();
    app.audio().stopEffect(kSfxDigGood);
    app.audio().stopEffect(kSfxDigBad);
    RICH4_LOGI("minigame: dig score=%d (RE 0x415215)", score);
    return score;
}

// ==================== 喜從天降（case 8）====================

const int kMoneyFallSpeed[5] = {24, 18, 15, 12, 15};  // [RE 0x475010]
// [RE 0x475015] 财神帧表 [state][frame]：0 右行/2 右转/3 左转/4 左行（1/5 备用镜像）
const uint8_t kRichFrames[6][6] = {
    {0, 1, 2, 3, 5, 6}, {0, 1, 2, 4, 5, 6}, {7, 8, 9, 10, 11, 0},
    {11, 10, 9, 8, 7, 0}, {12, 13, 14, 15, 17, 18}, {12, 13, 14, 16, 17, 18},
};
constexpr int kMoneyCountdown = 360;  // [RE 0x41500F] 50ms×360 = 18s
// [RE 0x415019/0x4151F1] 开场计数 99，但首次 WM_PAINT 即降为 10（≈0.5s 后出标题 FLC）
constexpr int kMoneyIntro = 10;

struct MoneyBag {
    int16_t x = 0;     // 0=空槽
    int16_t y = 0;
    int16_t flags = 0; // 低 nibble 类型、bit4-6 旋转帧、高字节 摆幅（有符号）
    int16_t vy = 0;
};

struct MoneyCtx {
    Application* app = nullptr;
    UiImage digits;
    UiImage rich;    // panel[93] 财神
    UiImage cloud;   // panel[94] 云
    UiImage bag[5];  // panel[95..99]（4=炸弹）
    UiImage doll;    // panel[100+char]
    std::vector<uint8_t> bgRaw;  // panel[92] RAW 640×480 RGB555
    FliDecoder boom;             // data[526] 爆炸 FLC
    bool boomOk = false;
    FliDecoder title;
    bool titleOk = false;
    MoneyBag bags[16];
    int cnt[4] = {};
    int countdown = 0;
    int intro = 0;
    int gamePhase = 0;
    int uiState = 0;      // 0=intro 1=title 2=play 3=settle
    uint64_t settleMs = 0;
    int richX = 110;
    int richState = 3;
    int richFrame = 4;
    int richWait = 0;
    int cloudFrame = -1;
    int cloudX = 0;
    int dollX = 320;
    int dollDir = 0;
    int dollWalk = 0;
    int dollHalf = 10;
    int dollExpr = 0;
    int bombs = 0;
    bool boomActive = false;
    int boomX = 0;
};

// [RE 0x4123D7] 生成钱袋（云=炸弹 type4；财神=随机 0..3）@y=100、vy=-16
void moneySpawn(MoneyCtx& ui, int x, bool fromCloud) {
    int i = 0;
    for (; i < 16; ++i) {
        if (ui.bags[i].x == 0) {
            break;
        }
    }
    if (i == 16) {
        return;
    }
    int type;
    if (fromCloud) {
        type = 4;
        ++ui.bombs;
    } else {
        const int r = rng::next() % 20;
        type = r < 9 ? 3 : (r < 15 ? 2 : (r < 18 ? 1 : 0));
    }
    const int sway = static_cast<int>(x - 320) * 50 / 210;
    MoneyBag& b = ui.bags[i];
    b.x = static_cast<int16_t>(x);
    b.y = 100;
    b.flags = static_cast<int16_t>(type | ((sway & 0xFF) << 8));
    b.vy = -16;
}

// [RE 0x413248] 接钱 tick（移动+接取+生成+绘制实体；runLogic=false 仅静态绘制）
void moneyTick(MoneyCtx& ui, bool runLogic) {
    Surface& dst = ui.app->surface();
    bool anyAlive = false;
    for (int i = 0; i < 16; ++i) {
        MoneyBag& b = ui.bags[i];
        if (b.x == 0) {
            continue;
        }
        const int v1 = b.y >= 130 ? b.y - 130 : 0;
        const int sway = b.flags >> 8;
        const int bx = b.x + static_cast<int>(static_cast<double>(sway) * v1 / 250.0);
        const int type = b.flags & 0xF;
        if (runLogic && ui.dollDir != 0) {  // [RE 0x413332] 接取：娃娃帧包围盒
            const int f = ui.dollWalk + ui.dollHalf * (ui.dollDir - 1) + 5;
            const UiFrameView& fr = ui.doll.frame(f < ui.doll.frameCount() ? f : 4);
            const int left = ui.dollX - fr.offsetX;
            const int top = 380 - fr.offsetY;
            if (bx > left && bx < left + fr.width && b.y > top && b.y < top + fr.height &&
                !ui.boomActive) {
                if (type == 4) {  // [RE 0x4133D7] 炸弹 → 爆炸 FLC + 结束
                    // 原版：audioStopEffect(0x4750CF)=停 Effect[24] 引线嘶嘶循环、
                    //       audioPlayEffect(0x4750D7,0)=Effect[15] 爆炸音
                    ui.app->audio().stopEffect(24);
                    ui.app->audio().playEffect(15);
                    ui.dollDir = 0;
                    ui.dollExpr = 4;
                    ui.gamePhase = 1;
                    ui.boomActive = true;
                    ui.boomX = ui.dollX - 55;
                    if (ui.boomOk) {
                        ui.boom.rewind();
                        ui.boom.nextFrame();
                    }
                } else {
                    ++ui.cnt[type];
                }
                b.x = 0;
                continue;
            }
        }
        if (b.x == 0) {
            continue;
        }
        const uint32_t scale = static_cast<uint32_t>(32768.0 * (1.0 + v1 / 250.0));
        const int rot = (b.flags >> 4) & 7;
        blitSprScaled(dst, ui.bag[type], rot, bx, b.y, scale);
        if (runLogic) {
            b.flags = static_cast<int16_t>((b.flags & ~0xF0) | ((((b.flags >> 4) + 1) & 7) << 4));
            if (b.y >= 130) {
                b.y = static_cast<int16_t>(b.y + kMoneyFallSpeed[type]);
            } else {
                b.vy = static_cast<int16_t>(b.vy + 2 > 16 ? 16 : b.vy + 2);
                b.y = static_cast<int16_t>(b.y + b.vy);
            }
            if (b.y > 380) {  // 漏接消失（炸弹仅计数，无惩罚）
                if (type == 4 && --ui.bombs <= 0) {
                    ui.bombs = 0;
                    ui.app->audio().stopEffect(24);  // [RE 0x41351B] 末颗炸弹落地停引线音
                }
                b.x = 0;
                continue;
            }
        }
        anyAlive = true;
    }
    if (ui.cloudFrame != -1) {
        blitSpriteFrame(dst, ui.cloud, ui.cloudFrame, ui.cloudX, 125);
    }
    blitSpriteFrame(dst, ui.rich, kRichFrames[ui.richState][ui.richFrame], ui.richX, 126);
    // [RE 0x4135F4] 娃娃跟随鼠标（原版 GetCursorPos；640x480 全屏下光标坐标 = 逻辑坐标）
    if (runLogic && ui.gamePhase != 2 && !ui.boomActive) {
        // [PORT Win32:GetCursorPos] 窗口化 + letterbox 缩放后，窗口坐标 ≠ 逻辑坐标：
        //   原先直接 SDL_GetMouseState 取窗口像素，非 1:1 窗口（默认 1280x960 = 2x）下
        //   娃娃追的目标偏右一倍 → 一路贴右边缘不跟手；且绕过鼠标覆盖，headless/调试
        //   move 命令无法驱动。改走 Application::mouseLogicalPos（窗口→逻辑换算 +
        //   m_mouseOv 合成输入）。
        int mx = 0;
        int my = 0;
        ui.app->mouseLogicalPos(mx, my);
        // [PORT 0x41361C/0x41363C] 原版用 SetCursorPos 把光标夹在 640x480 内（全屏下光标
        //   不可能越界）；窗口化不劫持系统光标，改为夹取采样值——对娃娃判定等价。
        if (mx < 0) {
            mx = 0;
        } else if (mx > 639) {
            mx = 639;
        }
        ui.dollX = moneyDollFollowStep(ui.dollX, mx, ui.dollDir, ui.dollWalk, ui.dollHalf);
    }
    const int df = ui.dollDir != 0 ? ui.dollWalk + ui.dollHalf * (ui.dollDir - 1) + 5
                                   : ui.dollExpr;
    blitSpriteFrame(dst, ui.doll, df < ui.doll.frameCount() ? df : ui.dollExpr, ui.dollX, 380);
    if (!runLogic) {
        return;
    }
    // 云触发/投弹（[RE 0x41374B] 音效表 g_moneySoundTable={22,23,24,15}）
    if (ui.cloudFrame == -1) {
        if ((ui.richState < 2 && ui.richX > 320) || (ui.richState > 3 && ui.richX < 320)) {
            if (rng::next() % 10 < 7 && ui.gamePhase == 0) {
                ui.cloudFrame = 0;
                ui.cloudX = ui.richX <= 320 ? rng::next() % 140 + 360 : rng::next() % 140 + 160;
            }
        }
    } else {
        ++ui.cloudFrame;
        if (ui.cloudFrame == 8 && !ui.boomActive) {
            // 原版：audioPlayEffect(g_moneySoundTable,0)=Effect[22] 投掷音、
            //       audioPlayEffect(0x4750CF,1)=Effect[24] 引线嘶嘶**循环**
            ui.app->audio().playEffect(22);
            ui.app->audio().playEffectLooping(24);
            moneySpawn(ui, ui.cloudX, true);
        }
        if (ui.cloudFrame == 12) {
            ui.cloudFrame = -1;
        }
    }
    // 财神巡游（[RE 0x41387F]）
    if (ui.gamePhase == 1) {
        ui.richState = 2;
        ui.richFrame = 2;
    } else {
        switch (ui.richState) {
        case 0:
            if (ui.richFrame >= 5) {
                if ((ui.richX <= 320 || rng::next() % 4) && ui.richX != 530) {
                    ui.richWait = rng::next() % 5;
                    ui.richX += 12;
                } else {
                    ui.richState = 2;
                }
                ui.richFrame = 0;
            } else {
                if (ui.richFrame == ui.richWait) {
                    moneySpawn(ui, ui.richX, false);
                }
                ++ui.richFrame;
                ui.richX += 12;
            }
            break;
        case 2:
            if (++ui.richFrame == 5) {
                ui.richState = 4;
                ui.richFrame = 0;
            }
            break;
        case 3:
            if (++ui.richFrame == 5) {
                ui.richState = 0;
                ui.richFrame = 0;
            }
            break;
        case 4:
            if (ui.richFrame >= 5) {
                if ((ui.richX >= 320 || rng::next() % 4) && ui.richX != 110) {
                    ui.richWait = rng::next() % 5;
                    ui.richX -= 12;
                } else {
                    ui.richState = 3;
                }
                ui.richFrame = 0;
            } else {
                if (ui.richFrame == ui.richWait) {
                    moneySpawn(ui, ui.richX, false);
                }
                ++ui.richFrame;
                ui.richX -= 12;
            }
            break;
        default:
            break;
        }
    }
    if (!anyAlive && ui.gamePhase == 1) {
        ui.gamePhase = 2;
    }
}

int moneyScore(const MoneyCtx& ui) {
    return 10 * ui.cnt[0] + 5 * ui.cnt[1] + 3 * ui.cnt[2] + ui.cnt[3];  // [RE 0x4144D3]
}

void drawMoney(MoneyCtx& ui, bool runLogic) {
    Surface& dst = ui.app->surface();
    if (ui.bgRaw.size() >= 640u * 480u * 2u) {  // [RE 0x4132A1] RAW 背景
        std::memcpy(dst.pixels(), ui.bgRaw.data(), 640u * 480u * 2u);
    }
    moneyTick(ui, runLogic);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", ui.countdown >> 1);  // 50ms tick → 秒
    static const int kTimeX[3] = {49, 69, 94};
    drawHudNumber(dst, ui.digits, buf, kTimeX, 3);
    blitElementOpaque(dst, ui.digits.frame(0), 114, 421);
    for (int r = 0; r < 4; ++r) {
        std::snprintf(buf, sizeof(buf), "%02d", ui.cnt[r]);
        blitElementOpaque(dst, ui.digits.frame(buf[0] - '0'), 185 + r * 91, 421);
        blitElementOpaque(dst, ui.digits.frame(buf[1] - '0'), 205 + r * 91, 421);
    }
    const int score = moneyScore(ui);
    std::snprintf(buf, sizeof(buf), "%03d", score);
    static const int kScoreX[3] = {549, 569, 589};
    drawHudNumber(dst, ui.digits, buf, kScoreX, 3);
    if (ui.uiState == 1) {
        blitFlcFrame(dst, ui.title, 0, 0, true);
    }
    if (ui.boomActive && ui.boomOk) {  // [RE 0x41340D] 爆炸 FLC @(dollX-55, 295) 透明
        blitFlcFrame(dst, ui.boom, ui.boomX, 295, true);
    }
    if (ui.uiState == 3) {
        drawBigScore(dst, ui.digits, score);
    }
}

// [RE 0x414FCD] moneyWndProc（50ms）
bool moneyHandler(const SDL_Event* event, void* user) {
    MoneyCtx& ui = *static_cast<MoneyCtx*>(user);
    Application& app = *ui.app;
    if (!event) {  // [RE 0x41500F 0x401]
        std::memset(ui.bags, 0, sizeof(ui.bags));
        std::memset(ui.cnt, 0, sizeof(ui.cnt));
        ui.countdown = kMoneyCountdown;
        ui.intro = kMoneyIntro;
        ui.gamePhase = 0;
        ui.uiState = 0;
        ui.richX = 110;
        ui.richState = 3;
        ui.richFrame = 4;
        ui.richWait = 0;
        ui.cloudFrame = -1;
        ui.dollX = 320;
        ui.dollDir = 0;
        ui.dollWalk = 0;
        ui.dollExpr = 0;
        ui.bombs = 0;
        ui.boomActive = false;
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent:  // [RE 0x415068 0x113]
        if (ui.uiState == 0) {
            if (--ui.intro <= 0) {
                ui.uiState = 1;
            }
            drawMoney(ui, false);
        } else if (ui.uiState == 1) {
            if (!ui.titleOk || decodeFlcOnce(ui.title)) {
                ui.uiState = 2;
            }
            drawMoney(ui, false);
        } else if (ui.uiState == 2) {
            if (ui.countdown > 0 && --ui.countdown == 0) {
                ui.gamePhase = 1;  // [RE 0x4150B3]
            }
            if (ui.gamePhase == 2 && !ui.boomActive) {  // [RE 0x4150ED] 评级表情
                if (ui.dollExpr != 4) {
                    ui.dollDir = 0;
                    const int s = moneyScore(ui);
                    ui.dollExpr = s >= 60 ? 3 : (s >= 50 ? 2 : (s >= 40 ? 0 : 1));
                }
                ui.uiState = 3;
                ui.settleMs = nowMs();
            }
            if (ui.boomActive) {  // [RE 0x41515D] 爆炸 FLC 逐帧 → 结算
                if (!ui.boomOk || decodeFlcOnce(ui.boom)) {
                    ui.boomActive = false;
                    ui.gamePhase = 2;
                }
            }
            drawMoney(ui, true);
        } else {
            if (nowMs() - ui.settleMs >= 2000) {  // [RE 0x415144] 2s → 退出
                app.events().requestExit(moneyScore(ui));
                return true;
            }
            drawMoney(ui, false);
        }
        return true;
    default:
        return true;
    }
}

// [RE 0x4155FC] miniGameMoneyRun：喜從天降交互版
// 依据: 0x4155FC 反编译（资源 panel[78/79/92..99/100+char]、data[526]、
//   musicPlayScene(10)、runModal moneyWndProc 0x414FCD 50ms；
//   音效表 g_moneySoundTable(0x4750BF)={22 投掷, 23, 24 引线循环, 15 爆炸}）
int miniGameMoneyRun(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    MoneyCtx ui;
    ui.app = &app;
    bool ok = true;
    auto blobD = st.panel.read(79);
    if (blobD) {
        ok = ui.digits.load(std::move(*blobD)) && ui.digits.frameCount() >= 20;
    } else {
        ok = false;
    }
    struct { UiImage* img; int idx; } loads[] = {{&ui.rich, 93}, {&ui.cloud, 94},
                                                 {&ui.bag[0], 95}, {&ui.bag[1], 96},
                                                 {&ui.bag[2], 97}, {&ui.bag[3], 98},
                                                 {&ui.bag[4], 99},
                                                 {&ui.doll, 100 + st.players[p].charIndex}};
    for (auto& l : loads) {
        auto blob = st.panel.read(l.idx);
        if (!blob || !l.img->load(std::move(*blob))) {
            ok = false;
        }
    }
    auto blob92 = st.panel.read(92);  // RAW 640×480×2
    if (!blob92 || blob92->size() < 640u * 480u * 2u) {
        ok = false;
    }
    if (!ok) {
        RICH4_LOGW("minigame: money panel resources unavailable (RE 0x4155FC)");
        return -1;
    }
    ui.bgRaw = std::move(*blob92);
    ui.dollHalf = (ui.doll.frameCount() - 5) >> 1;  // [RE 0x4157B0]
    auto blob526 = st.data.read(526);
    if (blob526) {
        ui.boomOk = ui.boom.open(std::move(*blob526));
    }
    auto blob78 = st.panel.read(78);
    if (blob78) {
        ui.titleOk = ui.title.open(std::move(*blob78));
    }
    app.audio().pushSceneMusic(10);  // [RE 0x4157CC]
    const int score = runModal(app, &moneyHandler, &ui, 50);
    app.audio().resumeSceneMusic();
    RICH4_LOGI("minigame: money score=%d (RE 0x4155FC)", score);
    return score;
}

// ==================== 七彩氣球（case 7）====================

constexpr int kBalloonCountdown = 150;  // [RE 0x414C1D] 100ms×150 = 15s
// [RE 0x414C27/0x414FB3] 开场计数 99，但首次 WM_PAINT 即降为 5（≈0.5s 后出标题 FLC）
constexpr int kBalloonIntro = 5;
constexpr int kSfxBalloonNew = 19;      // [RE 0x41307B] 新气球
constexpr int kSfxBalloonMiss = 20;     // [RE 0x414F14] 未命中
constexpr int kSfxBalloonHit = 21;      // [RE 0x414E13] 命中

const uint8_t kBalloonSpeed[12] = {15, 15, 15, 15, 18, 18, 18, 24, 24, 24, 24, 18};
const uint8_t kBalloonSpecial[10] = {9, 9, 9, 9, 9, 10, 10, 10, 10, 0x80};

struct Balloon {
    int16_t x = 0;
    int16_t y = 0;
    int16_t type = 0;
};

struct BalloonCtx {
    Application* app = nullptr;
    UiImage scene;   // panel.mkf[91]：0 背景/1..12 气球/13 爆裂
    UiImage digits;  // panel.mkf[79]
    FliDecoder title;
    bool titleOk = false;
    Balloon balloons[16];
    int state = 0;
    int intro = 0;
    int countdown = 0;
    int score = 0;
    int gamePhase = 0;
    int slowdown = 0;
    int wind = 0;
    uint64_t settleMs = 0;
};

// [RE 0x412F6F] 气球移动/生成
void balloonTick(BalloonCtx& ui) {
    for (int i = 0; i < 16; ++i) {
        Balloon& b = ui.balloons[i];
        if (b.x != 0) {
            if ((b.type & 0xF0) != 0) {
                b.type -= 16;
                if ((b.type & 0xF0) == 0) {
                    b.x = 0;
                    continue;
                }
            } else if (ui.slowdown == 0) {
                int sp = kBalloonSpeed[b.type & 0xF];
                if (ui.wind == -1) {
                    sp *= 2;
                } else if (ui.wind == 1) {
                    sp >>= 1;
                }
                b.y = static_cast<int16_t>(b.y - sp);
            }
            continue;
        }
        if (ui.gamePhase != 0) {
            continue;
        }
        const int r = rng::next() % 1000;
        int type;
        if (r < 20) {
            type = r >> 2;
        } else if (r < 28) {
            type = ((27 - r) >> 1) + 5;
        } else if (r < 30) {
            type = kBalloonSpecial[rng::next() % 10];
        } else {
            continue;
        }
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
        b.x = static_cast<int16_t>(cols[rng::next() % nc]);
        b.y = 420;
        b.type = static_cast<int16_t>(type);
    }
}

// [RE 0x414BBC 0x201/0x203] 点击：按下与抬起各射一枪（原版 quirk），一次可命中多个
void balloonClick(BalloonCtx& ui, int mx, int my) {
    if (ui.state != 2 || ui.gamePhase >= 2) {
        return;
    }
    Application& app = *ui.app;
    // 命中/未命中各为表内独立槽（21/20）；一次射击多球同类只落一声（原版同 buffer
    //   重启语义），命中优先保证爆裂音必响（2026-09-25 实机修正：曾被后续 miss 覆盖）
    bool anyHit = false;
    bool anyMiss = false;
    for (int i = 0; i < 16; ++i) {
        Balloon& b = ui.balloons[i];
        if (b.x == 0 || (b.type & 0xF0) != 0) {
            continue;
        }
        const int hw = (b.type >= 6) ? 18 : 22;
        const int hh = (b.type >= 6) ? 26 : 30;
        if (mx < b.x - hw || mx > b.x + hw || my < b.y - hh || my > b.y + hh) {
            anyMiss = true;
            continue;
        }
        anyHit = true;
        switch (b.type) {
        case 9:
            ui.score *= 2;
            break;
        case 10:
            ui.score >>= 1;
            break;
        case 11: {
            switch (rng::next() % 6) {
            case 0: ui.countdown = 1; break;
            case 1: ui.slowdown = 20; break;
            case 2: ui.wind = -1; break;
            case 3: ui.wind = 1; break;
            case 4: ui.score = 0; break;
            default: ui.score *= 2; break;
            }
            break;
        }
        default:
            ui.score += b.type + 1;
            if (ui.score >= 1000) {
                ui.score = 999;
            }
            break;
        }
        b.type = 60;
    }
    if (anyHit) {
        app.audio().playEffect(kSfxBalloonHit);
    } else if (anyMiss) {
        app.audio().playEffect(kSfxBalloonMiss);
    }
}

// [RE 0x4146EE/0x412F6F/0x413F07/0x414789] 全场景即时重绘
void drawBalloon(BalloonCtx& ui) {
    Surface& dst = ui.app->surface();
    blitElementOpaque(dst, ui.scene.frame(0), 0, 0);
    // [RE 0x412FCE] 原版气球画布裁剪 rect y<387（sky 区）——气球从底部面板**后面**
    //   升起进入视野；顶部完全出界清槽（0x41315B blitElementToCanvas 返回非 0）
    bool anyAlive = false;
    for (int i = 0; i < 16; ++i) {
        Balloon& b = ui.balloons[i];
        if (b.x == 0) {
            continue;
        }
        const UiFrameView& fr = ui.scene.frame((b.type & 0xF) + 1);
        const int top = b.y - fr.offsetY;
        if (top >= 387) {
            anyAlive = true;  // 还在面板后面升起，不可见但存活
            continue;
        }
        const int hh = top < 0 ? fr.height + top : (387 - top < fr.height ? 387 - top : fr.height);
        if (hh > 0 &&
            blitElementRegion(dst, fr, b.x, b.y, 0, top < 0 ? -top : 0, fr.width, hh, false)) {
            anyAlive = true;
        } else if (top + fr.height <= 0) {
            b.x = 0;  // 完全飞出顶部
        }
    }
    if (!anyAlive && ui.gamePhase == 1 && ui.state == 2) {
        ui.gamePhase = 2;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", ui.countdown);
    static const int kTimeX[3] = {49, 69, 94};
    drawHudNumber(dst, ui.digits, buf, kTimeX, 3);
    blitElementOpaque(dst, ui.digits.frame(0), 114, 421);
    std::snprintf(buf, sizeof(buf), "%04d", ui.score);
    static const int kScoreX[4] = {529, 549, 569, 589};
    drawHudNumber(dst, ui.digits, buf, kScoreX, 4);
    if (ui.state == 1) {
        blitFlcFrame(dst, ui.title, 0, 0, true);
    }
    if (ui.state == 3) {
        drawBigScore(dst, ui.digits, ui.score);
    }
}

// [RE 0x414BBC] balloonWndProc（100ms tick）
bool balloonHandler(const SDL_Event* event, void* user) {
    BalloonCtx& ui = *static_cast<BalloonCtx*>(user);
    Application& app = *ui.app;
    if (!event) {
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
    case kModalTimerEvent:
        if (ui.state == 0) {
            if (--ui.intro <= 0) {
                ui.state = 1;
            }
        } else if (ui.state == 1) {
            if (!ui.titleOk || decodeFlcOnce(ui.title)) {
                ui.state = 2;
                app.cursor().select(9, 3, 5);  // [RE 0x414D8B] 枪光标
            }
        } else if (ui.state == 2) {
            if (ui.slowdown != 0) {
                --ui.slowdown;
            }
            if (ui.countdown != 0 && --ui.countdown == 0) {
                ui.gamePhase = 1;
            }
            balloonTick(ui);
            if (ui.gamePhase == 2) {
                ui.state = 3;
                app.cursor().select(41, 1, 0);
                ui.settleMs = nowMs();
            }
        } else if (nowMs() - ui.settleMs >= 2000) {
            app.events().requestExit(ui.score);
            return true;
        }
        drawBalloon(ui);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            balloonClick(ui, static_cast<int>(event->button.x),
                         static_cast<int>(event->button.y));
        }
        return true;
    default:
        return true;
    }
}

// [RE 0x4154DC] miniGameBalloonsRun：七彩氣球交互版
// 依据: 0x4154DC 反编译（门控 0x4154FA、资源 panel[78/79/91]、音效表
//   g_balloonSoundTable={19,20,21}、musicPlayScene(11)、runModal balloonWndProc
//   0x414BBC 100ms）；玩法细节见 docs/reverse/functions/41982d-p2-events.md §6
int miniGameBalloonsRun(Application& app) {
    GameState& st = app.gameState();
    auto blob78 = st.panel.read(78);
    auto blob79 = st.panel.read(79);
    auto blob91 = st.panel.read(91);
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
    app.audio().pushSceneMusic(11);  // [RE 0x415597]
    const int score = runModal(app, &balloonHandler, &ui, 100);
    app.audio().resumeSceneMusic();  // [RE 0x4155AD]
    RICH4_LOGI("minigame: balloons score=%d (RE 0x4154DC)", score);
    return score;
}

} // namespace

// [RE 0x4135F4/0x41364D] 喜從天降 娃娃跟随鼠标单步判定（定义见 include/game/app/minigame.h）
// 依据: moneyTick 0x413248 反汇编——
//   0x413654 `sub esi,[Point.x]` / 0x413659 `abs` / 0x413661 `cmp eax,8; jle 0x4136C1`
//     （|diff| <= 8 直接跳到跟随段之后：**dir 保持原值、行走帧不推进**）
//   0x41366A `48BD48=1` + 0x413673 `sub word[48BD4E],0Ah`（diff > 0 → 左行 10px）
//   0x41367F `48BD48=2` + 0x413688 `add word[48BD4E],0Ah`（diff < 0 → 右行 10px）
//   0x413692 `48BD48=0` 分支不可达（仅 diff==0 时进入，而 diff==0 已被死区分支拦下）
//   0x41369B..0x4136BA 行走帧 `48BD50 = (48BD50+1) % 48BD52`（48BD52 = half，仅移动时推进）
int moneyDollFollowStep(int dollX, int cursorLogicalX, int& dir, int& walk, int walkHalf) {
    const int diff = dollX - cursorLogicalX;
    if (diff > 8) {
        dir = 1;
        dollX -= 10;
    } else if (diff < -8) {
        dir = 2;
        dollX += 10;
    } else {
        return dollX;  // 死区：dir 不清零、行走帧不推进（原版 quirk，勿"顺手修"）
    }
    if (walkHalf > 0) {
        walk = (walk + 1) % walkHalf;
    }
    return dollX;
}

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
        // [RE 0x41523A/0x4154FA/0x41561A] 人类 + 动画过程开 → 交互小游戏
        switch (cellType) {
        case 6: score = miniGameDigRun(app); break;
        case 7: score = miniGameBalloonsRun(app); break;
        case 8: score = miniGameMoneyRun(app); break;
        default: break;
        }
    }
    if (score < 0) {
        // [RE 0x415457] 共享 else：score = rand()%20+50（50..69）
        score = dbg::roll(dbg::SlotMinigame, 20) + 50;
                trace::logf("minigame shared: cellType=%d score=%d", cellType, score);
        char text[64];
        std::snprintf(text, sizeof(text), "得點券%d點", score);  // [RE 0x463797 cp950]
        showMessage(app, text, 2000);                            // [RE 0x41548E]
        // [RE 0x4154B4..0x4154CF] 台词 off_48084A[char*27 + (rand&1)] = 列 0/1
        const int ci = pl.charIndex;
        if (ci >= 0 && ci < 12) {
            playLine(app, p, kValueLines[ci][rng::next() & 1]);
        }
    }
    pl.points = static_cast<uint16_t>(pl.points + score);  // [RE 0x41B152] += ax
    RICH4_LOGI("miniGame: cellType %d p%d score %d -> points %u (RE 0x415215/4DC/5FC)", cellType,
               p, score, pl.points);
}

} // namespace rich4
