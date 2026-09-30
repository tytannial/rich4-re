// 土地公開拍賣（runAuction 0x43BDE5 + auctionWndProc 0x43A2DD + auctionAiMaxBid 0x439F0D）。
// 依据: docs/reverse/functions/43380a-magic-house.md §5。
// 面板 panel.mkf[26]（184 帧）：帧0 背景；帧1(142×113,off 74/59) AI 气泡底 @(480,rowY-15)；
//   帧2(244×100) 浮动消息框 @(410,60)；帧3..16 七按钮（偶=正常 94×46、奇=按下 87×39）@(406,·)；
//   帧17(109×388) 右列罩 @(67,63)；帧21(199×349) 落槌罩 @(124,27)；帧22/23(40×30) 赢家表情；
//   帧24 成交后右列；帧25(154×352) 左列罩 @(123,24)；帧26/27(29×15) 法槌；帧28/29(31×25) 表情闪；
//   帧29.. 缩略图/表情（78+charIndex 表情；缩略图帧公式见 runAuction）。
// 交互：100ms tick；7 按钮 = PASS / +100 / +500 / +1000 / +5000 / +10000 / Give up
//   （wParam=0..6，[RE 0x43BC59] byte_48C4AF 先自减再 Post 0x407）；AI 按与预算差额选档、
//   封顶领先者现金+500、单人竞争强制最低加价（[RE 0x43B220]）；出价=头像翻滚
//   （panel[28+3*char]）+ 气泡（价/ＰＡＳＳ/放棄）；落槌音 {29} + #0135 + 台词 #0136..#0147。
// 成交链：视口对准 → 1s → 产权转移（空地+地权写到期日）→ 小地图重建 → 1s →
//   transferMoney(赢家, seller, 现价, 0)；seller=-1 → 入公库（新闻 idx 7 公有土地）。
// 差异: setPauseDraw 省略（重写模态天然阻塞）；refreshGameUi mode4 附加效果未建模；
//   赢家表情帧原版传参 Rect.right/bottom=(223,95) 为笔误（同医院帧 bug），重写按 (183,65)。

#include "game/app/auction_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/app/game_loop.h"
#include "game/app/map_render.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/app/debug/debug.h"
#include <cstdio>
#include "game/core/debug_hooks.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/core/rng.h"

namespace rich4 {

namespace {

// [RE 0x475B84] 7 按钮 y（x 命中 363..449，中心 ±19）
constexpr int kBtnY[7] = {133, 181, 229, 277, 325, 373, 421};
// [RE 0x475BA2] 加价档（索引 1..5；0=PASS、6=Give up 不用表值）
constexpr int kBidSteps[6] = {0, 100, 500, 1000, 5000, 10000};
// [RE 0x475B34] 出局状态文本（1..7；8=现金不足画现金）
const char* const kStatusTexts[8] = {"",       "住宿中", "消失中", "坐牢中",
                                     "住院中", "冬眠中", "夢遊中", "賣方"};
// [RE 0x475B54] 12 角色成交台词（FloatMessage '#NNNN' 前缀自动播语音；
//   表长 = off_475B54..0x475B84 共 48B = 12 指针，按 charIndex 直接索引）
const char* const kWinLines[12] = {
    "#0136恭喜約翰喬購得此地！", "#0137恭喜沙隆巴斯購得此地！", "#0138恭喜忍太郎購得此地！",
    "#0139恭喜錢夫人購得此地！", "#0140恭喜阿土伯購得此地！", "#0141恭喜莎拉公主購得此地！",
    "#0142恭喜宮本寶藏購得此地！", "#0143恭喜糖糖購得此地！",     "#0144恭喜烏咪購得此地！",
    "#0145恭喜孫小美購得此地！", "#0146恭喜小丹尼購得此地！",     "#0147恭喜金貝貝購得此地！",
};
// [RE 0x465038/0x46507D/0x465098/0x465063/0x465055/0x46505E/0x4650A6]
constexpr char kTextOpen[] = "#0131公開拍賣土地一處。";
constexpr char kTextAsk[] = "#0132底價%d元\n請意者出價。";
constexpr char kTextSold[] = "#0135%d元成交";
constexpr char kTextNoBid[] = "#0148無人出價，宣佈流標。";
constexpr char kTextPass[] = "ＰＡＳＳ";
constexpr char kTextQuit[] = "放棄";
constexpr char kTextBidLabel[] = "標價：";

constexpr int kFrameBg = 0;
constexpr int kFrameBubble = 1;
constexpr int kFrameMsg = 2;
constexpr int kFrameRightMask = 17;
constexpr int kFrameGavel = 21;
constexpr int kFrameWinA = 22;
constexpr int kFrameWinB = 23;
constexpr int kFrameLeftMask = 25;
constexpr int kGavelFrameA = 26;  // [RE 0x475BA0]={26,27,26}
constexpr int kGavelFrameB = 27;
constexpr int kRowY0 = 80;
constexpr int kRowX = 590;

struct AuctionRow {
    int player = -1;       // word_48C434（<=0 = 出列）
    int status = 0;        // word_48C436（1..8 出局原因；PASS=1；0 可竞价）
    int budget = 0;        // dword_48C438（AI 意愿价）
    UiImage sprNormal;     // panel[27+3*char]
    UiImage sprRoll;       // panel[28+3*char] 翻滚
    UiImage sprFace;       // panel[29+3*char] 悲伤/表情（放弃换此 + 赢家翻滚）
    bool human = false;
};

struct AuctionCtx {
    Application* app = nullptr;
    UiImage sheet;
    AuctionRow rows[4];
    int price = 0;
    int seller = -1;
    int objId = 0;
    int state = 0;          // byte_48C4AC
    int curSlot = 0;        // dword_48C4A4 b0
    int bubble = 0;         // (>>4)&3：0=价 1=PASS 2=放弃
    int rollCnt = 0;        // b1
    int winRoll = 0;        // b3（赢家表情翻滚 0..60）
    int leader = -1;        // dword_48C4A8
    int hoverBtn = 0;       // byte_48C4AF（i+1）
    int laneA = 0;          // byte_48C4AE 低 4 位：法槌 @(163,45) 28×15
    int laneB = 0;          // byte_48C4AE 高 4 位：法槌 @(127,90) 16×14
    int exprT = 0;          // byte_48C4AD
    bool winnerMode = false;  // byte_48C4B0
    int bidderCount = 0;    // byte_48C4B1
    FloatMessage msg;
    std::vector<uint16_t> bubbleBg;
    int bubbleBgY = 0;
};

// [RE 0x439F0D] auctionAiMaxBid：AI 意愿价。
// 依据: 0x439F0D 反编译 + disasm FPU（dbl_465018=0.3、dbl_465020=0.5 为 double，
//   IDA float 显示误读已核）；estate 同地段自有数 + level>>1 + 1 加价、
//   供需系数 6.0 - 空地占比×4.0、随机折扣 [0.5,0.8]、上限 (3.0~3.5)×基价×M、封顶现金。
int auctionAiMaxBid(GameState& st, int player1, int objId, int price) {
    const double v20 = static_cast<double>(dbg::raw(dbg::SlotAuction)) / 32767.0 * 0.3 + 0.5;
    int empty = 0;
    int sameName = 0;
    int bid = 0;
    int cap = 0;
    if (objId < 4000) {
        const Estate& target = st.estates[static_cast<size_t>(objId - 2000)];
        for (size_t j = 1; j < st.estates.size(); ++j) {
            const Estate& es = st.estates[j];
            if (es.owner == player1 && std::strcmp(es.name, target.name) == 0) {
                ++sameName;
            }
            if (es.owner == 0) {
                ++empty;
            }
        }
        const double supply = 6.0 - static_cast<double>(empty) /
                                          static_cast<double>(st.estates.size() - 1) * 4.0;
        bid = static_cast<int>(static_cast<double>(st.moneyMul * price *
                                                   (sameName + (target.level >> 1) + 1)) *
                               supply * v20);
        cap = static_cast<int>((static_cast<double>(dbg::raw(dbg::SlotAuction)) * 1.52587890625e-05 + 3.0) *
                               static_cast<double>(st.moneyMul * target.priceAdd));
    } else {
        const Corp& target = st.corps[static_cast<size_t>(objId - 4000)];
        for (size_t j = 1; j < st.corps.size(); ++j) {
            if (st.corps[j].owner == 0) {
                ++empty;
            }
        }
        const double supply = 6.0 - static_cast<double>(empty) /
                                          static_cast<double>(st.corps.size() - 1) * 4.0;
        bid = static_cast<int>(static_cast<double>(st.moneyMul * price * ((target.sub >> 1) + 1)) *
                               supply * v20);
        cap = static_cast<int>((static_cast<double>(dbg::raw(dbg::SlotAuction)) * 1.52587890625e-05 + 3.0) *
                               static_cast<double>(st.moneyMul * target.buildPrice));
    }
    if (bid > cap) {
        bid = cap;
    }
    Player& pl = st.players[player1 - 1];
    return bid > pl.cash ? pl.cash : bid;
}

// [RE 0x43A155] auctionDrawRow：重画某行。a4=0 擦除到背景 patch（表情帧 bbox）；
//   a4≠0 画表情帧(78+char) + spr 帧 0。
void drawRow(AuctionCtx& ui, int slot, const UiImage* spr, bool withFace) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int y = kRowY0 + 120 * slot;
    const int player = ui.rows[slot].player;
    if (player <= 0) {
        return;
    }
    const int ci = st.players[player - 1].charIndex;
    const UiFrameView& face = ui.sheet.frame(78 + ci);
    const int dx = kRowX - face.offsetX;
    const int dy = y - face.offsetY;
    if (withFace) {
        blitElement(dst, face, kRowX, y, false);
    } else {
        blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), dx, dy, dx, dy, face.width,
                                face.height, false);
    }
    if (spr != nullptr && spr->frameCount() > 0) {
        blitSpriteFrame(dst, *spr, 0, kRowX, y);
    }
}

// [RE 0x43A5F0/0x43A4F7] 出价后刷新：擦旧领先者行价 + 本行橙色现价 + 左下"標價：%d"
void redrawPrices(AuctionCtx& ui, int oldLeader) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    char buf[32];
    if (oldLeader >= 0 && oldLeader < 4 && ui.rows[oldLeader].player > 0) {
        const int y = kRowY0 + 120 * oldLeader + 25;
        blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), 520, y, 520, y, 100, 20, false);
    }
    const int y = kRowY0 + 120 * (ui.curSlot & 3) + 25;
    app.text().setFont(18, 0xF0F000, 0x101010, 3, 0);
    std::snprintf(buf, sizeof(buf), "%d", ui.price);
    app.text().drawText(dst, buf, 620, y, 1);
    blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), 180, 260, 180, 260, 100, 20, false);
    app.text().setFont(18, 0xFFFFFF, 0x101010, 3, 0);
    app.text().drawText(dst, buf, 272, 262, 1);
}

// [RE 0x43A403] 0x407 出价处理（wParam=0 PASS / 1..5 加价 / 6 放弃）
void doBid(AuctionCtx& ui, int w) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    app.audio().playEffect(63); // [RE g_auctionBidSound 0x475BC2={63}]
    const int slot = ui.curSlot & 3;
    AuctionRow& row = ui.rows[slot];
    Player& pl = st.players[row.player - 1];
    if (w == 0) {
        row.status = 1;          // 不再竞争（PASS，仍留在轮转名单）
        ui.bubble = 1;
        ui.rollCnt = 17;         // [RE 0x110] 气泡 ~1 tick 即转
        return;
    }
    if (w == 6) {
        ui.bubble = 2;
        ui.rollCnt = 18;         // [RE 0x120]
        drawRow(ui, slot, &row.sprFace, false); // [RE 0x43A44B] 换表情 SPR
        row.player = -1;         // 出列
        return;
    }
    const int step = kBidSteps[w];
    if (step + ui.price > pl.cash) {
        return; // [RE 0x43A4A8] 超现金不执行
    }
    ui.price += step;
    redrawPrices(ui, ui.leader);
    ui.leader = slot;
    for (AuctionRow& r : ui.rows) {
        r.status = 0; // [RE 0x43A6AE] 有效出价 → 清"不再竞争"（重新开放）
    }
    ui.bubble = 0;
    ui.rollCnt = 1; // [RE BYTE1 |= 1] 启动翻滚
}

// [RE 0x43AFA1..0x43B234] 轮到当前 bidder：人类画按钮等待 / AI 即时决策
void beginBidderTurn(AuctionCtx& ui) {
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    const int slot = ui.curSlot & 3;
    AuctionRow& row = ui.rows[slot];
    Player& pl = st.players[row.player - 1];
    if (pl.alive == 1) {
        for (int i = 0; i < 7; ++i) {
            blitElement(dst, ui.sheet.frame(2 * i + 4), 406, kBtnY[i], false);
        }
        if (ui.price <= pl.cash) {
            ui.state = 3;
        } else {
            ui.state = 4;
            doBid(ui, 6); // [RE 0x43B07A Post 0x407 wParam=6]
        }
        return;
    }
    // [RE 0x43B0D6] AI：擦按钮区
    blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), 363, 114, 363, 114, 86, 334, false);
    ui.state = 4;
    int w = 6;
    if (ui.price <= pl.cash) {
        const int v28 = row.budget;
        if (ui.price + 10000 <= v28) {
            w = 5;
        } else if (ui.price + 5000 <= v28) {
            w = 4;
        } else if (ui.price + 1000 <= v28) {
            w = 3;
        } else if (ui.price + 500 <= v28) {
            w = 2;
        } else if (ui.price + 100 <= v28) {
            w = 1;
        } else {
            w = 0;
        }
        if (ui.leader >= 0) {
            const Player& lead = st.players[ui.rows[ui.leader].player - 1];
            const int v29 = lead.cash + 500;
            if (kBidSteps[w] + ui.price > v29) {
                const int v30 = v29 - ui.price;
                if (v30 > 5000 && v30 <= 10000) {
                    w = 4;
                } else if (v30 > 1000 && v30 <= 5000) {
                    w = 3;
                } else if (v30 > 500 && v30 <= 1000) {
                    w = 2;
                } else if (v30 <= 100) {
                    w = 1;
                }
            }
        }
        if (ui.bidderCount == 1 && w != 0) {
            w = 1; // [RE 0x43B220] 单人竞争 → 最低加价
        }
    }
    doBid(ui, w);
}

// [RE 0x43B272..0x43B406] 状态 5：翻滚结束 → 判定轮转
void afterRoll(AuctionCtx& ui) {
    const int slot = ui.curSlot & 3;
    if (ui.rows[slot].player > 0) {
        drawRow(ui, slot, &ui.rows[slot].sprNormal, false);
    }
    int v32 = 0;
    int v33 = 0;
    for (const AuctionRow& r : ui.rows) {
        if (r.player > 0) {
            ++v32;
            if (r.status != 0) {
                ++v33;
            }
        }
    }
    if (v32 == 0 || v32 == v33) {
        ui.msg.show(*ui.app, kTextNoBid);
        ui.state = 11;
        return;
    }
    if ((v32 == 1 || v32 - v33 == 1) && ui.leader >= 0) {
        ui.state = 9; // 落槌
        return;
    }
    do {
        ui.curSlot = (ui.curSlot + 1) & 3;
    } while (ui.rows[ui.curSlot].player <= 0);
    drawRow(ui, ui.curSlot, &ui.rows[ui.curSlot].sprNormal, true);
    ui.state = 2;
}

// [RE 0x43A702] BYTE1 出价翻滚动画（气泡 + 头像帧循环）
void tickRoll(AuctionCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const int slot = ui.curSlot & 3;
    const int y = kRowY0 + 120 * slot;
    const int frameCount = ui.rows[slot].sprRoll.frameCount();
    if (ui.rollCnt == 1 && ui.bubble == 0) {
        drawRow(ui, slot, nullptr, false); // [RE 0x43A75E] 清上一气泡
    }
    if (ui.bubbleBg.empty()) {
        // [NEW M4-A2] 区域快照走公共 API（逻辑区域按画布 scale 设备化）
        saveRegion(ui.bubbleBg, dst, 406, y - 74, 142, 113);
        ui.bubbleBgY = y;
    }
    blitElement(dst, ui.sheet.frame(kFrameBubble), 480, y - 15, false);
    char buf[32];
    app.text().setFont(16, 1052688, 0, 2, 0);
    switch (ui.bubble) {
    case 0:
        std::snprintf(buf, sizeof(buf), "%d", ui.price);
        app.text().drawText(dst, buf, 480, y - 15, 2);
        break;
    case 1:
        app.text().drawText(dst, kTextPass, 480, y - 15, 2);
        break;
    default:
        app.text().drawText(dst, kTextQuit, 480, y - 15, 2);
        break;
    }
    if (ui.rollCnt <= frameCount && ui.bubble == 0) {
        const int fr = (ui.rollCnt == frameCount) ? 0 : ui.rollCnt - 1;
        blitSpriteFrame(dst, ui.rows[slot].sprRoll, fr, kRowX, y);
    }
    if (ui.rollCnt <= frameCount || ui.rollCnt < 10) {
        ++ui.rollCnt;
        return;
    }
    restoreRegion(dst, ui.bubbleBg, 406, ui.bubbleBgY - 74, 142, 113);
    ui.bubbleBg.clear();
    ui.rollCnt = 0;
    ui.bubble = 0;
    ui.state = 5;
    afterRoll(ui);
}

// [RE 0x43AB61] BYTE2 赢家表情 SPR 翻滚（0..60）
void tickWinRoll(AuctionCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    const int v14 = ui.winRoll - 1;
    const int slot = ui.leader & 3;
    const int y = kRowY0 + 120 * slot;
    const int frameCount = ui.rows[slot].sprFace.frameCount();
    if (v14 == 0) {
        drawRow(ui, slot, nullptr, false);
        blitSpriteFrame(dst, ui.rows[slot].sprFace, 0, kRowX, y);
        ++ui.winRoll;
        return;
    }
    if (v14 >= 60) {
        ui.winRoll = 0;
        return;
    }
    int fr = v14 % frameCount;
    if (fr == 0) {
        fr = frameCount;
    }
    drawRow(ui, slot, nullptr, false);
    blitSpriteFrame(dst, ui.rows[slot].sprFace, fr - 1, kRowX, y);
    ++ui.winRoll;
}

// [RE 0x43B5E6..0x43B841] 法槌双车道随机闪烁（装饰）+ 赢家表情帧闪
void tickBlink(AuctionCtx& ui) {
    Application& app = *ui.app;
    Surface& dst = app.surface();
    if (ui.laneA != 0) {
        if (ui.laneA == 1) {
            blitElement(dst, ui.sheet.frame(kGavelFrameA), 163, 45, false);
        } else if (ui.laneA == 2) {
            blitElement(dst, ui.sheet.frame(kGavelFrameB), 163, 45, false);
        } else {
            blitElementRegionOpaque(dst, ui.sheet.frame(kFrameLeftMask), 163, 45, 40, 21, 28, 15, false);
        }
        ++ui.laneA;
        if (ui.laneA > 3) {
            ui.laneA = 0;
        }
    }
    if (ui.laneB != 0) {
        if (ui.laneB == 1) {
            blitElement(dst, ui.sheet.frame(kGavelFrameB), 127, 90, false);
        } else if (ui.laneB == 2) {
            blitElement(dst, ui.sheet.frame(kGavelFrameA), 127, 90, false);
        } else {
            blitElementRegionOpaque(dst, ui.sheet.frame(kFrameRightMask), 127, 90, 61, 29, 16, 14, false);
        }
        ++ui.laneB;
        if (ui.laneB > 3) {
            ui.laneB = 0;
        }
    }
    if (ui.laneA == 0 && ui.laneB == 0) {
        const int r = static_cast<int>(rng::next()) >> 10;
        if (r == 0) {
            ui.laneA = 1;
        } else if (r == 1) {
            ui.laneB = 1;
        }
    }
    if (!ui.winnerMode) {
        return;
    }
    // [RE 0x43B877] 赢家表情 @(183,65) 40×30（原版传参笔误 (223,95)，重写修正）
    constexpr int kExprX = 183;
    constexpr int kExprY = 65;
    if (ui.exprT != 0) {
        if (--ui.exprT == 0) {
            blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), kExprX, kExprY, kExprX, kExprY,
                                    40, 30, false);
            blitElement(dst, ui.sheet.frame(kFrameWinA), kExprX, kExprY, false);
        }
    } else if ((rng::next() >> 11) < 4) {
        blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), kExprX, kExprY, kExprX, kExprY, 40,
                                30, false);
        blitElement(dst, ui.sheet.frame(kFrameWinB), kExprX, kExprY, false);
        ui.exprT = (rng::next() & 7) + 1;
    }
}

bool auctionHandler(const SDL_Event* event, void* user) {
    AuctionCtx& ui = *static_cast<AuctionCtx*>(user);
    Application& app = *ui.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    if (!event) { // [RE 0x401/0x405] 初始：静态画面已绘，开场文本
        ui.msg.setup(ui.sheet, kFrameMsg, 410, 60, 0, 0, 1052688, 0);
        ui.state = 1;
        ui.msg.show(app, kTextOpen);
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: {
        if (ui.rollCnt != 0) {
            tickRoll(ui);
        }
        if (ui.winRoll != 0 && ui.leader >= 0) {
            tickWinRoll(ui);
        }
        if (ui.msg.advance(app)) {
            if (ui.state >= 5) {
                switch (ui.state) {
                case 9: { // [RE 0x43B412] 落槌
                    ui.state = 10;
                    blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), 123, 24, 123, 24, 154,
                                            160, false);
                    blitElement(dst, ui.sheet.frame(kFrameGavel), 124, 27, false);
                    app.audio().playEffect(29); // [RE g_auctionDoneSounds{29}]
                    ui.winnerMode = true;
                    ui.winRoll = 1;
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), kTextSold, ui.price);
                    ui.msg.show(app, buf);
                    break;
                }
                case 10: { // [RE 0x43B4D8] 成交列覆盖 + 赢家台词
                    ui.state = 11;
                    blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), 124, 27, 124, 27, 199,
                                            166, false);
                    blitElement(dst, ui.sheet.frame(kFrameLeftMask), 123, 24, false);
                    blitElement(dst, ui.sheet.frame(kFrameRightMask), 67, 63, false);
                    ui.winnerMode = false;
                    const int leadChar = st.players[ui.rows[ui.leader].player - 1].charIndex;
                    ui.msg.show(app, kWinLines[leadChar >= 0 && leadChar < 12 ? leadChar : 0]);
                    break;
                }
                case 11: // [RE 0x43B5B7] 退出
                    app.events().requestExit(ui.leader >= 0 ? ui.rows[ui.leader].player : -1);
                    break;
                default:
                    break;
                }
            } else if (ui.state == 1) { // [RE 0x43AF13] 首个有效行 + #0132
                bool found = false;
                for (int i = 0; i < 4; ++i) {
                    if (ui.rows[i].player > 0) {
                        ui.curSlot = i;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ui.msg.show(app, kTextNoBid);
                    ui.state = 11;
                } else {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), kTextAsk, ui.price);
                    ui.msg.show(app, buf);
                    ui.state = 2;
                }
            } else if (ui.state == 2) {
                beginBidderTurn(ui);
            }
        }
        tickBlink(ui);
        app.renderFrame();
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: { // [RE 0x43BB27] 按钮 hover（state 3 且非翻滚）
        if (ui.state == 3 && ui.rollCnt == 0) {
            const int mx = static_cast<int>(event->motion.x);
            const int my = static_cast<int>(event->motion.y);
            if (ui.hoverBtn != 0) {
                const int prev = ui.hoverBtn - 1;
                blitElement(dst, ui.sheet.frame(2 * prev + 4), 406, kBtnY[prev], false);
                ui.hoverBtn = 0;
            }
            if (mx >= 363 && mx < 449) {
                for (int i = 0; i < 7; ++i) {
                    if (my >= kBtnY[i] - 19 && my <= kBtnY[i] + 19) {
                        blitElement(dst, ui.sheet.frame(2 * i + 3), 406, kBtnY[i], false);
                        ui.hoverBtn = i + 1;
                        break;
                    }
                }
            }
            app.renderFrame();
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button == SDL_BUTTON_LEFT) { // [RE 0x43BC59] 点击出价（wParam=hoverBtn-1）
            if (ui.state == 3 && ui.hoverBtn != 0) {
                const int w = ui.hoverBtn - 1;
                ui.hoverBtn = 0;
                doBid(ui, w);
                app.renderFrame();
            }
        } else if (event->button.button == SDL_BUTTON_RIGHT) { // [RE 0x43BB27] 右键跳过文本
            ui.msg.finish(app);
        }
        return true;
    }
    default:
        return true;
    }
}

} // namespace

// [RE 0x43BDE5] runAuction
bool runAuction(Application& app, int seller, int objId, bool playMusic) {
    // [NEW] named region：右列 7 按钮（0x475BA2 加价表；auc.btn.0=PASS 1..5=加價 6=放棄，
    //   幾何=hover 命中 kBtnY[i]±19 @406 同源）+ 開框 trace
    {
        static bool s_reg = false;
        if (!s_reg) {
            s_reg = true;
            for (int i = 0; i < 7; ++i) {
                char nm[16];
                std::snprintf(nm, sizeof(nm), "auc.btn.%d", i);
                debug::registerRegion(nm, 406, kBtnY[i] - 19, 110, 38);
            }
        }
    }
    trace::logf("dialog open name=auction");
    GameState& st = app.gameState();
    if (objId <= 2000 || objId >= 6000) {
        return false;
    }
    const bool isCorp = objId >= 4000;

    AuctionCtx ui;
    ui.app = &app;
    ui.seller = seller;
    ui.objId = objId;

    // [RE 0x43BE18] 起拍价 = M × (int)(base × (1 + level×0.5))；缩略图帧公式
    int base = 0;
    int level = 0;
    int thumbFrame = 0;
    if (!isCorp) {
        const Estate& es = st.estates[static_cast<size_t>(objId - 2000)];
        base = es.priceAdd;
        level = es.level;
        if (es.level != 0) {
            if (st.gameMode != 0) { // [RE word_4991B6]
                thumbFrame = es.type != 0 ? 131 + st.mapIndex : 115 + 5 * st.mapIndex + es.level;
            } else {
                thumbFrame = es.type != 0 ? 50 : 29 + 5 * st.mapIndex + es.level;
            }
        } else {
            thumbFrame = es.owner != 0 ? 91 + st.players[es.owner - 1].charIndex : 90;
        }
    } else {
        const Corp& cp = st.corps[static_cast<size_t>(objId - 4000)];
        base = cp.buildPrice;
        level = cp.sub;
        static constexpr int kCorpThumbA[4] = {134, 167, 151, 51}; // [RE 0x475BD2]
        static constexpr int kCorpThumbB[4] = {150, 183, 167, 62}; // [RE 0x475BE2]
        if (cp.sub != 0) {
            if (st.gameMode != 0) {
                switch (cp.type) {
                case 0:
                    thumbFrame = st.mapIndex != 0 ? 51 : 151;
                    break;
                case 1:
                case 2:
                case 4: {
                    const int v10 = cp.type == 4 ? 2 : cp.type - 1;
                    thumbFrame = kCorpThumbA[st.mapIndex & 3] + 5 * v10 + cp.sub;
                    break;
                }
                case 3:
                    thumbFrame = kCorpThumbB[st.mapIndex & 3];
                    break;
                default:
                    thumbFrame = 51;
                    break;
                }
            } else {
                thumbFrame = cp.type == 0 ? 51 : 51 + 5 * (cp.type - 1) + cp.sub;
            }
        } else {
            thumbFrame = cp.owner != 0 ? 104 + st.players[cp.owner - 1].charIndex : 103;
        }
    }
    ui.price = st.moneyMul * static_cast<int>(static_cast<double>(base) * (level * 0.5 + 1.0));

    // 资源
    auto blob = st.panel.read(26);
    if (!blob || !ui.sheet.load(std::move(*blob)) || ui.sheet.frameCount() < 152) {
        RICH4_LOGW("auction: panel.mkf[26] unavailable (RE 0x43BDE5)");
        return false;
    }
    int rows = 0;
    int bidderCount = 0;
    for (int p = 0; p < st.playerCount && rows < 4; ++p) {
        if (st.players[p].alive == 0) {
            continue;
        }
        AuctionRow& row = ui.rows[rows];
        row.player = p + 1;
        row.human = st.players[p].alive == 1;
        const int ci = st.players[p].charIndex;
        for (int w = 0; w < 3; ++w) {
            auto b = st.panel.read(3 * ci + 27 + w);
            UiImage* img = w == 0 ? &row.sprNormal : (w == 1 ? &row.sprRoll : &row.sprFace);
            if (b) {
                img->load(std::move(*b));
            }
        }
        // [RE 0x43C12C] 出局状态收集（后写覆盖：现金不足→状态→卖方）
        int status = 0;
        if (st.players[p].cash <= ui.price) {
            status = 8;
        }
        const uint32_t sf = st.players[p].stateFlags;
        if (sf & 0xFF) {
            status = 1;
        }
        if ((sf >> 8) & 0xFF) {
            status = 2;
        }
        if ((sf >> 16) & 0xFF) {
            status = 3;
        }
        if ((sf >> 24) & 0xFF) {
            status = 4;
        }
        if (st.players[p].byte54 != 0) {
            status = 5;
        }
        if (st.players[p].state37 != 0) {
            status = 6;
        }
        if (p == seller) {
            status = 7;
        }
        row.status = status;
        if (status == 0) {
            ++bidderCount; // [RE v15 → byte_48C4B1]
            if (!row.human) {
                row.budget = auctionAiMaxBid(st, p + 1, objId, ui.price); // [RE 0x43C61E]
            }
        }
        ++rows;
    }
    ui.bidderCount = bidderCount;
    if (rows == 0) {
        return false; // [RE 0x43C315]
    }

    // [RE 0x43C32B] 静态画面
    Surface& dst = app.surface();
    blitElementOpaque(dst, ui.sheet.frame(kFrameBg), 0, 0);
    blitElement(dst, ui.sheet.frame(kFrameLeftMask), 123, 24, false);
    blitElement(dst, ui.sheet.frame(kFrameRightMask), 67, 63, false);
    blitElement(dst, ui.sheet.frame(thumbFrame), 232, 180, false);
    app.text().setFont(18, 0xFFFFFF, 1052688, 3, 0);
    app.text().drawText(dst, kTextBidLabel, 182, 242, 0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", ui.price);
    app.text().drawText(dst, buf, 272, 262, 1);
    bool faceDone = false;
    for (int i = 0; i < rows; ++i) {
        const int y = kRowY0 + 120 * i;
        const int p = ui.rows[i].player;
        if (p <= 0) {
            continue;
        }
        const Player& pl = st.players[p - 1];
        if (ui.rows[i].status != 0) {
            if (ui.rows[i].status == 8) {
                std::snprintf(buf, sizeof(buf), "%d", pl.cash);
                app.text().drawText(dst, buf, 620, y + 14, 1);
            } else {
                app.text().drawText(dst, kStatusTexts[ui.rows[i].status & 7], 590, y + 14, 2);
            }
            blitSpriteFrame(dst, ui.rows[i].status == 7 ? ui.rows[i].sprNormal
                                                        : ui.rows[i].sprRoll,
                            0, kRowX, y);
            ui.rows[i].player = -1; // [RE 0x43C4DE] 画后出列 + 清状态
            ui.rows[i].status = 0;
        } else {
            if (!faceDone) { // [RE 0x43C4F8] 首个正常行画表情帧（原版一次性 quirk）
                faceDone = true;
                blitElement(dst, ui.sheet.frame(78 + pl.charIndex), kRowX, y, false);
            }
            std::snprintf(buf, sizeof(buf), "%d", pl.cash);
            app.text().drawText(dst, buf, 620, y + 14, 1);
            blitSpriteFrame(dst, ui.rows[i].sprNormal, 0, kRowX, y);
        }
    }
    app.renderFrame();

    if (playMusic) {
        app.audio().pushSceneMusic(5); // [RE 0x43C6D4]
    }
    const int winner = runModal(app, &auctionHandler, &ui, 100);
    if (playMusic) {
        app.audio().resumeSceneMusic();
    }
    if (winner <= 0) {
        RICH4_LOGI("auction: no sale obj=%d (RE 0x43BDE5)", objId);
        return false;
    }

    // [RE 0x43C73A] 成交：视口对准 → 1s → 产权转移 → 小地图 → 1s → 转账
    int16_t ox = 0;
    int16_t oy = 0;
    uint8_t* owner = nullptr;
    uint32_t* expire = nullptr;
    if (isCorp) {
        Corp& cp = st.corps[static_cast<size_t>(objId - 4000)];
        ox = cp.x;
        oy = cp.y;
        owner = &cp.owner;
        expire = &cp.expireDate;
    } else {
        Estate& es = st.estates[static_cast<size_t>(objId - 2000)];
        ox = es.x;
        oy = es.y;
        owner = &es.owner;
        expire = &es.expireDate;
    }
    focusView(app, ox, oy); // [RE refreshGameUi(x, y, 4)]（mode4 附加效果未建模）
    eventAudioWait(app, 1000);
    if (*owner != winner) {
        if (st.cfgLandPerm > 0 && st.cfgLandPerm <= 3 && *owner == 0) {
            // [RE 0x4751F0 g_landPermDays + 0x4521CB 日期加法]（同购地逻辑）
            static const int32_t kLandPermDays[4] = {0, 0x00020000, 0x00010000, 0x00000600};
            int32_t d = static_cast<int32_t>(st.gameDate) + kLandPermDays[st.cfgLandPerm];
            if ((kLandPermDays[st.cfgLandPerm] & 0xFF00) != 0 && (d & 0xFF00) > 0xC00) {
                d += 0xF400;
            }
            *expire = static_cast<uint32_t>(d);
        }
        *owner = static_cast<uint8_t>(winner);
        buildMiniMapMarks(app);
        renderGameFrame(app);
        app.renderFrame();
        eventAudioWait(app, 1000);
    }
    transferMoney(app, winner - 1, seller, ui.price, 0); // [RE 0x41D2C6]
    RICH4_LOGI("auction: sold obj=%d price=%d winner=%d seller=%d (RE 0x43BDE5)", objId, ui.price,
               winner, seller);
    return true;
}

} // namespace rich4
