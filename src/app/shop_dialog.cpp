#include "game/app/shop_dialog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/card_bag_dialog.h"
#include "game/app/event_stack.h"
#include "game/app/float_message.h"
#include "game/app/item_bag_dialog.h"
#include "game/app/item_lines.h"
#include "game/app/map_objects.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {
namespace {

// ===== 资源帧/坐标（panel.mkf[10]，38 帧；卡片店 0..15 / 道具店 16..31 / 共享 32..37）=====
// [RE 0x42D299] 底图 帧 0/16 @(0,0)；柜台 帧 2/18 @(320,240)
// [RE 0x42D78F] 列表抽屉 帧 1/17 @(drawerX,10)，drawerX -222→5（缓动 +40/-3）
// [RE 0x42D7B7] 卡包/道具包 panel.mkf[11] @(bagX,293)，bagX 640→227（缓动 -80/+7）
// [RE 0x42DEF8] 切页按钮 帧 13/29（常态）/ 14/30（按下）@(542,13)
// [RE 0x42DFB3] 离开按钮 帧 35（常态）/ 36（按下）@(556,246)
// [RE 0x42D4EB] 点券框 帧 37 @(230,246) + 数字 @(310,257) align1
// [RE 0x42D366] 浮动消息板 帧 15 @(120,10)（FloatMessage setup）
constexpr int kDrawerStartX = -222;
constexpr int kDrawerEndX = 5;
constexpr int kBagStartX = 640;
constexpr int kBagEndX = 227;
constexpr int kDrawerY = 10;
constexpr int kBagY = 293;
constexpr int kSwapX = 542;
constexpr int kSwapY = 13;
constexpr int kLeaveX = 556;
constexpr int kLeaveY = 246;
constexpr int kPointsBoxX = 230;
constexpr int kPointsBoxY = 246;
constexpr int kPointsNumX = 310;
constexpr int kPointsNumY = 257;

// 命中区（屏幕坐标）[RE 0x42DE60..0x42E423]
constexpr int kSwapHitL = 542;
constexpr int kSwapHitT = 13;
constexpr int kSwapHitR = 627;
constexpr int kSwapHitB = 98;
constexpr int kLeaveHitL = 556;
constexpr int kLeaveHitT = 246;
constexpr int kLeaveHitR = 636;
constexpr int kLeaveHitB = 286;
constexpr int kBagHitL = 232;
constexpr int kBagHitT = 298;
constexpr int kBagHitR = 632;
constexpr int kBagHitB = 466;
constexpr int kBagSlotW = 80;
constexpr int kBagSlotH = 56;
constexpr int kCardRowL = 14;
constexpr int kCardRowT = 81;
constexpr int kCardRowR = 215;
constexpr int kCardRowB = 465;
constexpr int kCardRowH = 24;
constexpr int kItemRowL = 12;
constexpr int kItemRowT = 80;
constexpr int kItemRowR = 213;
constexpr int kItemRowB = 464;
constexpr int kItemRowH = 48;

// 商品列表绘制内坐标（相对抽屉帧 1/17 像素）[RE 0x42EBA8/0x42EBDD/0x42EC6E/0x42ECAD]
constexpr int kListNameX = 90;
constexpr int kListPriceX = 194;

// 卖出按下高亮 Rect（原版 0x42E05A：left=233+80c / right=+311 / top=299+56r / bottom=+353）
constexpr int kSellHiL = 233;
constexpr int kSellHiT = 299;
constexpr int kSellHiW = 78;
constexpr int kSellHiH = 54;

// 店员眼睛/嘴部动画 [RE 0x42D8A4..0x42DBB2 / 0x42DC62..0x42DDF4]
//   眼睛 A：帧表 page0 {7,8,7,5} / page1 {21,22,21,23}（case1/case2 逐帧）
//   眼睛 B：page0 帧 4..6（v12+3）/ page1 帧 23..24（v13+22）（case3/case4 随机帧）
//   ★ 原版眼睛 A 组(case1/2)实参传的是重绘 Rect 的 right/bottom（(455,93)/(487,94)，笔误，
//     同医院复位帧 8/13 先例）→ 正确落点为 Rect 左上角 (405,60)/(417,50)，与 B 组一致
//     （两套都是同一处眼睛的眨眼覆盖帧；原版实机错位，重写按左上角修正）
//   嘴部口型：page0 帧 9/10/11、page1 帧 25/26/27（闭/半开/张开），位置 (405,91)/(417,89)
//     ——用户实机反馈：原版常驻随机重复触发太吵，重写改为**语音开始时播一次**（2026-09-25）
constexpr uint8_t kClerkFrames[8] = {7, 8, 7, 5, 21, 22, 21, 23}; // [RE 0x4755B8]
constexpr int kClerkPos[2][2] = {{405, 60}, {417, 50}};
constexpr int kMouthPos[2][2] = {{405, 91}, {417, 89}};

// [RE 0x4755C0] off_4755C0 商店消息（每页 6 项：进入/提示/点券不足/栏位满/离开/未用）
const char* const kShopMsgs[2][6] = {
    {"#0000有什麼我能\n為你服務的嗎？", "#0001請挑選你想要\n兌換的卡片。", "#0002抱歉！\n你的點數不足！",
     "#0003對不起！\n您的卡片欄已滿！", "#0004歡迎下次再來！", ""},
    {"#0005歡迎光臨\n道具店！", "#0006您要兌換\n什麼道具？", "#0007對不起，\n您的點券不夠！",
     "#0008很抱歉！您的\n道具欄已滿！", "#0010謝謝惠顧！", "#0009這個道具會員\n才能兌換！"},
};

// [RE 0x4755F0] byte_4755F0 AI 购买礼物道具顺序（0-based 道具槽）
constexpr uint8_t kAiItemOrder[6] = {7, 1, 6, 0, 3, 2};

// [RE 0x464378] 自家百货公司中奖消息模板
constexpr const char* kGiftMsg = "歡迎董事長光臨\n\n送您%s！";

enum class ShopPhase {
    Welcome,  // 进入页欢迎消息中（lParam 0）
    Slide,    // 抽屉缓动滑入（lParam 1）
    Prompt,   // 到位提示消息中（lParam 2）
    Ready,    // 可交互（lParam 3）
    Farewell, // 离开消息中（lParam 4）
};

struct ShopState {
    Application* app = nullptr;
    UiImage sheet;                              // panel.mkf[10] 商店 UI
    UiImage bagSheet;                           // panel.mkf[11] 卡包/道具包
    int cur = 0;
    int page = 0;                               // dword_48C310：0=卡片店 1=道具店
    ShopPhase phase = ShopPhase::Welcome;
    FloatMessage msg;
    bool visited[2] = {};                       // byte_48C349：该页进入过（引导消息只显示一次）
    uint8_t cardsForSale[15] = {};              // byte_48C31C 在售卡（1-based id）
    uint8_t itemsForSale[8] = {};               // byte_48C2F8 在售道具（1-based id）
    bool cardsSold[15] = {};                    // 已购买：原版把行文字置灰留在表面（非清空消失）
    bool itemsSold[8] = {};                     // 已购买（同上）
    uint8_t itemVisibleMap[15] = {};            // 道具包「可见槽 → 道具 id」（卖出区命中）
    int revenue = 0;                            // dword_48C343（买 10×价 / 卖 1×价；退出返回）
    // 抽屉动画
    int drawerX = kDrawerStartX;
    int bagX = kBagStartX;
    int velX = 40;                              // dword_48C33B
    int velY = 80;                              // dword_48C33F
    // 店员眼睛/嘴部动画
    int animStage = 0;                          // dword_48C32F（低4位状态 / bit4-7 A帧 / bit8-11 B帧）
    int mouthTimer = 0;                         // dword_48C314（嘴部口型倒计时 1..7）
    int mouthFrame = 0;                         // 当前嘴部帧（9/10/11 或 25/26/27；0=无）
    int clerkEyeFrame = 0;                      // 当前眼睛帧（两套动画共用；0=无）
    bool halfTick = false;                      // byte_48C348 隔帧（动画 100ms 节奏）
    int pressedBtn = 0;                         // byte_48C347：1 切页 / 2 离开 / 3 卖出
    int pressedSlot = -1;                       // 卖出按住格（highlightRect 下沉）
};

// ===== 买卖（UI 与 AI 共用；数值照抄 0x42D1B2/0x42D145/0x42D237/0x42D272）=====

// [RE 0x42D237] 买卡：giveCardToBag（满 15 丢最低价）+ 扣点券；返回营业额 10×价
// 依据: 0x42D237 反编译（尾共享 0x42D25C：points -= price; return 10*price）
int shopBuyCard(GameState& st, int p, int card) {
    giveCardToBag(st, p, card);
    const int price = kCardPrices[card];
    Player& pl = st.players[p];
    pl.points = static_cast<uint16_t>(pl.points - price);
    return 10 * price;
}

// [RE 0x42D272→0x42D25C] 买道具：givePlayerItem + 扣点券；返回营业额 10×价
// 依据: 0x42D272 反编译（givePlayerCard 0x445A4D + 共享扣点券尾段）
int shopBuyItem(GameState& st, int p, int item) {
    givePlayerItem(st, p, item);
    const int price = kItemPrice[item - 1];
    Player& pl = st.players[p];
    pl.points = static_cast<uint16_t>(pl.points - price);
    return 10 * price;
}

// [RE 0x42D145] 卖卡：cardBagRemove（库存+1）+ 点券 += (int)(价×0.9)；返回营业额 价
//   原版 x87 fild/fmul 0.9/fadd/fistp → 就近舍入（默认 round-to-nearest-even）
// 依据: 0x42D145 反编译; sub_441343 移除 + dbl_464364=0.9
int shopSellCard(GameState& st, int p, int card) {
    cardBagRemove(st, p, card);
    const int price = kCardPrices[card];
    Player& pl = st.players[p];
    pl.points = static_cast<uint16_t>(static_cast<int>(std::nearbyint(price * 0.9 + pl.points)));
    return price;
}

// [RE 0x42D1B2] 卖道具 n 个：点券 += (int)(n×价×0.9)；扣 n；道具 id≤8 归还礼物池；返回营业额 n×价
// 依据: 0x42D1B2 反编译; g_playerCards[15p+id] -= n（基址 0x49915B = itemStock-1）;
//   g_cardPool[id] += n（= misc8A[id-1]，id≤8）
int shopSellItem(GameState& st, int p, int item, int n) {
    if (item < 1 || item > 13 || n <= 0) {
        return 0;
    }
    const int gain = n * kItemPrice[item - 1];
    Player& pl = st.players[p];
    pl.points = static_cast<uint16_t>(static_cast<int>(std::nearbyint(gain * 0.9 + pl.points)));
    st.itemStock[15 * p + item - 1] =
        static_cast<uint8_t>(st.itemStock[15 * p + item - 1] - n);
    if (item <= 8) { // [RE 0x42D229] g_cardPool[item] += n（= misc8A[item-1]）
        st.misc8A[item - 1] = static_cast<uint8_t>(st.misc8A[item - 1] + n);
    }
    return gain;
}

// ===== 绘制 =====

void shopRender(ShopState& s) {
    Application& app = *s.app;
    Surface& dst = app.surface();
    GameState& st = app.gameState();
    const UiImage& sh = s.sheet;
    const int page = s.page;

    // 底图 + 柜台 [RE 0x42D299/0x42D2CE/0x42D308]
    blitElementOpaque(dst, sh.frame(16 * page), 0, 0);
    blitElement(dst, sh.frame(16 * page + 2), 320, 240, false);

    // 列表抽屉（不透明）[RE 0x42D78F blitElementFullscreen]
    blitElementOpaque(dst, sh.frame(16 * page + 1), s.drawerX, kDrawerY);

    // 商品列表（画在抽屉上，随动画移动）；已购买行**置灰保留**——原版 0x42E214/0x42E490
    //   把该行灰色（0xA0A0A0）名字/价格画入资源表面后仅清 byte_48C31C/2F8，文字仍留在画面上
    TextRenderer& t = app.text();
    char buf[24];
    if (page == 0) {
        for (int pass = 0; pass < 2; ++pass) {
            const bool sold = (pass == 1);
            t.setFont(20, sold ? 0xA0A0A0 : 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
            for (int i = 0; i < 15; ++i) {
                const int card = s.cardsForSale[i];
                if (card == 0 || s.cardsSold[i] != sold) {
                    continue;
                }
                t.drawText(dst, kCardNames[card], s.drawerX + kListNameX, kDrawerY + 24 * i + 83, 2);
                std::snprintf(buf, sizeof buf, "$%d", kCardPrices[card]); // [RE 0x464374]
                t.drawText(dst, buf, s.drawerX + kListPriceX, kDrawerY + 24 * i + 75, 1);
            }
        }
    } else {
        for (int pass = 0; pass < 2; ++pass) {
            const bool sold = (pass == 1);
            t.setFont(20, sold ? 0xA0A0A0 : 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
            for (int j = 0; j < 8; ++j) {
                const int item = s.itemsForSale[j];
                if (item == 0 || s.itemsSold[j] != sold) {
                    continue;
                }
                t.drawText(dst, kItemBagNames[item], s.drawerX + kListNameX, kDrawerY + 48 * j + 92, 2);
                std::snprintf(buf, sizeof buf, "$%d", kItemPrice[item - 1]);
                t.drawText(dst, buf, s.drawerX + kListPriceX, kDrawerY + 48 * j + 84, 1);
            }
        }
    }

    // 店员眼睛动画（A/B 两套都是同一处眨眼覆盖帧，共用 clerkEyeFrame）[RE 0x42D8A4 case1..4]
    // 落点 = Rect 左上角（原版 A 组实参误传 Rect.right/bottom，见常量注释）
    if (s.clerkEyeFrame != 0) {
        blitElementOpaque(dst, sh.frame(s.clerkEyeFrame), kClerkPos[page][0], kClerkPos[page][1]);
    }
    // 嘴部口型（帧 9/25 闭合、10/11 或 26/27 张开）[RE 0x42DC8F/0x42DD1D]
    if (s.mouthFrame != 0) {
        blitElementOpaque(dst, sh.frame(s.mouthFrame), kMouthPos[page][0], kMouthPos[page][1]);
    }

    // 切页/离开按钮 + 点券框：**抽屉到位后**才出现
    //   [RE 0x42D7F2 0x40C 到位画帧 13/29 + 35；0x40E 才画点券框 37 + 数字]
    const bool arrived = (s.phase != ShopPhase::Welcome && s.phase != ShopPhase::Slide);
    if (arrived) {
        const int swapFrame = (s.pressedBtn == 1) ? 16 * page + 14 : 16 * page + 13;
        blitElement(dst, sh.frame(swapFrame), kSwapX, kSwapY, false);
        blitElement(dst, sh.frame(s.pressedBtn == 2 ? 36 : 35), kLeaveX, kLeaveY, false);
        blitElement(dst, sh.frame(37), kPointsBoxX, kPointsBoxY, false);
        t.setFont(20, 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
        std::snprintf(buf, sizeof buf, "%u", static_cast<unsigned>(st.players[s.cur].points));
        t.drawText(dst, buf, kPointsNumX, kPointsNumY, 1);
    }

    // 卡包 / 道具包 [RE 0x42D7B7 sub_456418(dword_48C300[page], bagX, 293)]
    if (s.page == 0) {
        drawCardBagAt(app, s.bagSheet, s.cur, s.bagX, kBagY);
    } else {
        drawItemBagAt(app, s.bagSheet, s.cur, s.bagX, kBagY, s.itemVisibleMap);
    }

    // 卖出按住格（highlightRect 下沉）[RE 0x42E05A]
    if (s.pressedSlot >= 0) {
        const int l = kSellHiL + kBagSlotW * (s.pressedSlot % 5);
        const int top = kSellHiT + kBagSlotH * (s.pressedSlot / 5);
        pressDown(dst, l, top, kSellHiW, kSellHiH, 1, kChannelHalf);
    }

    // 浮动消息板（每帧全量重绘后补画，避免被覆盖）
    s.msg.redraw(app);
}

// ===== 状态机 =====

// 消息结束（自然播完或点击跳过）后的阶段推进
void shopOnMsgDone(ShopState& s) {
    switch (s.phase) {
    case ShopPhase::Welcome:
        s.phase = ShopPhase::Slide; // 原版：消息完 → PostMessage(0x40C) 开始滑入
        break;
    case ShopPhase::Prompt:
        s.visited[s.page] = true;  // [RE 0x42DBFF] byte_48C349[page]=1
        s.phase = ShopPhase::Ready;
        break;
    case ShopPhase::Farewell:
        s.app->events().requestExit(s.revenue); // [RE 0x42DC2E] postModalExit(dword_48C343)
        break;
    default:
        break;
    }
}

// 抽屉到位 [RE 0x42D7F2]：画按钮/点券 → 首次进入显示提示（0x40D），否则直接可交互
void shopFinishSlide(ShopState& s) {
    if (!s.visited[s.page]) {
        s.phase = ShopPhase::Prompt;
        s.msg.show(*s.app, kShopMsgs[s.page][1]);
    } else {
        s.visited[s.page] = true;
        s.phase = ShopPhase::Ready;
    }
}

// 进入页（首次：欢迎消息 + 动画；已进入过：直接到位）[RE 0x42D56F 0x405]
void shopEnterPage(ShopState& s) {
    s.msg.setup(s.sheet, 15, 120, 10, -20, 0, 0x101010, 0); // [RE 0x42D366]
    s.pressedBtn = 0;
    s.pressedSlot = -1;
    s.animStage = s.page + 3; // [RE 0x42D5E0] dword_48C32F = page + 3
    s.mouthTimer = 0;
    s.mouthFrame = 0;
    s.clerkEyeFrame = 0;
    if (s.visited[s.page]) {
        s.drawerX = kDrawerEndX;
        s.bagX = kBagEndX;
        s.phase = ShopPhase::Slide; // 位置已到位 → 下一 tick 立即完成
    } else {
        s.drawerX = kDrawerStartX;
        s.bagX = kBagStartX;
        s.velX = 40;
        s.velY = 80;
        s.phase = ShopPhase::Welcome;
        s.msg.show(*s.app, kShopMsgs[s.page][0]); // [RE 0x42D5B0]
    }
}

// 店员眼睛眨眼动画（100ms 节奏）[RE 0x42D8A4 switch]
//   ★ 用户实机反馈（2026-09-25）：常驻随机但频率降低——case0 采样 0..63（合计 1/32；
//     原版 0..31 为 1/16）
void shopAdvanceClerk(ShopState& s) {
    const int page = s.page;
    switch (s.animStage & 0xF) {
    case 0: {
        const int r = std::rand() % 64; // 原版 rand()>>10（0..31）；降频（用户反馈）
        if (r != 0 || (s.animStage & 0xF00) == 0) {
            if (r == 1) {
                s.animStage = page + 3; // 进入 B 动画（3/4）
            }
        } else {
            s.animStage |= page + 1; // 进入 A 动画（1/2）
        }
        break;
    }
    case 1:
    case 2: {
        const int f = (s.animStage & 0xF0) >> 4;
        if (f != 4) {
            s.clerkEyeFrame = kClerkFrames[4 * page + f]; // [RE 0x42D8A4] 7/8/7/5 或 21/22/21/23
            s.animStage += 16; // A 帧推进（渲染用 bit4-7）
        } else {
            s.animStage = 512; // 结束：回状态 0 + bit9（画面保留最后眼睛帧）
        }
        break;
    }
    case 3:
    case 4: {
        const int r = (page == 0) ? (((3 * std::rand()) >> 15) + 1) : ((std::rand() & 1) + 1);
        if (r != ((s.animStage & 0xF00) >> 8)) {
            s.animStage = (s.animStage & 0xF0F0) | (r << 8); // 低 4 位清零（同原版）
            s.clerkEyeFrame = (page == 0) ? r + 3 : r + 22;  // [RE 0x42D8A4] B 帧 4..6 / 23..24
        }
        break;
    }
    default:
        break;
    }
}

// 店员嘴部口型（100ms 节奏，消息显示期间持续）[RE 0x42DC46..0x42DDF4]
//   原版条件：`floatMsgActive() || dword_48C314` 才进入本段（0x42DC46 提前 return）——
//   消息显示期间每 100ms 1/4 概率随机张嘴（帧 10/11 或 26/27）+ 倒计时 1..7 后闭嘴
//   （帧 9/25），循环到消息结束；无消息且倒计时归零则静止（浏览期间不说话）
void shopAdvanceMouth(ShopState& s, bool msgActive) {
    const int page = s.page;
    if (!msgActive && s.mouthTimer == 0) {
        return; // [RE 0x42DC46] if (!floatMsgActive() && !dword_48C314) return 0
    }
    const int old = s.mouthTimer;
    if (s.mouthTimer != 0) {
        --s.mouthTimer;
        if (old == 1) {
            s.mouthFrame = (page == 0) ? 9 : 25; // [RE 0x42DC8F/0x42DCF6] 倒计时到 1 → 闭嘴
        }
    } else if ((std::rand() % 16) < 4) { // [RE 0x42DD10] rand()>>11 < 4（1/4）→ 张嘴
        s.mouthFrame = (std::rand() & 1) + ((page == 0) ? 10 : 26); // [RE 0x42DD64/0x42DDB3]
        s.mouthTimer |= std::rand() & 7; // [RE 0x42DDD3] dword_48C314 |= rand & 7
        if (s.mouthTimer == 0) {
            s.mouthTimer = 1;
        }
    }
}

// 50ms tick；返回是否应重绘
bool shopTick(ShopState& s) {
    Application& app = *s.app;
    bool redraw = false;
    // 抽屉缓动 [RE 0x42D60B..0x42D7F2]
    if (s.phase == ShopPhase::Slide) {
        if (s.drawerX < kDrawerEndX) {
            s.drawerX += s.velX;
            if (s.drawerX > kDrawerEndX) {
                s.drawerX = kDrawerEndX;
            }
        }
        s.velX -= 3;
        if (s.bagX > kBagEndX) {
            s.bagX -= s.velY;
            if (s.bagX < kBagEndX) {
                s.bagX = kBagEndX;
            }
        }
        s.velY -= 7;
        if (s.drawerX == kDrawerEndX && s.bagX == kBagEndX) {
            shopFinishSlide(s);
        }
        redraw = true;
    }
    // 隔帧动画（100ms）[RE 0x42D884]
    s.halfTick = !s.halfTick;
    if (s.halfTick) {
        shopAdvanceClerk(s); // 眼睛：常驻随机眨眼（降频）
        redraw = true;
    }
    // 浮动消息推进 [RE 0x42DBAE]（先于嘴部动画，同原版 WM_TIMER 顺序）
    if (s.msg.active()) {
        if (s.msg.advance(app)) {
            shopOnMsgDone(s);
        }
        redraw = true;
    }
    // 嘴部口型 [RE 0x42DC46..]：消息显示期间持续；消息结束后把剩余倒计时播完即停
    if (s.halfTick) {
        shopAdvanceMouth(s, s.msg.active());
        redraw = true;
    }
    return redraw;
}

// ===== 点击 =====

void shopClickCardRow(ShopState& s, int mx, int my) {
    if (mx < kCardRowL || mx >= kCardRowR || my < kCardRowT || my >= kCardRowB) {
        return;
    }
    const int i = (my - kCardRowT) / kCardRowH;
    if (i < 0 || i >= 15) { // 原版无上界（v25=16 越界读入道具表），重写防越界
        return;
    }
    const int card = s.cardsForSale[i];
    if (card == 0 || s.cardsSold[i]) {
        return;
    }
    GameState& st = s.app->gameState();
    Player& pl = st.players[s.cur];
    if (pl.points < kCardPrices[card]) { // [RE 0x42E1C5] → floatMsgShow(0x4755C8[页])
        s.msg.show(*s.app, kShopMsgs[0][2]);
        return;
    }
    if (cardBagCount(st, s.cur) >= 15) { // [RE 0x42E1F7] → floatMsgShow(0x4755CC[页])
        s.msg.show(*s.app, kShopMsgs[0][3]);
        return;
    }
    s.revenue += shopBuyCard(st, s.cur, card); // [RE 0x42E214]
    s.cardsSold[i] = true;                     // [RE 0x42E379] 售罄（原版清 byte_48C31C 但灰字留表面）
    RICH4_LOGI("shop: buy card id=%d price=%d points=%u revenue=%d (RE 0x42D37F)", card,
               kCardPrices[card], pl.points, s.revenue);
}

void shopClickItemRow(ShopState& s, int mx, int my) {
    if (mx < kItemRowL || mx >= kItemRowR || my < kItemRowT || my >= kItemRowB) {
        return;
    }
    const int j = (my - kItemRowT) / kItemRowH;
    const int item = s.itemsForSale[j];
    if (item == 0 || s.itemsSold[j]) {
        return;
    }
    GameState& st = s.app->gameState();
    Player& pl = st.players[s.cur];
    if (pl.points < kItemPrice[item - 1]) { // [RE 0x42E423] → 0x4755C8[页]
        s.msg.show(*s.app, kShopMsgs[1][2]);
        return;
    }
    if (st.itemStock[15 * s.cur + item - 1] >= 9) { // [RE 0x42E482] 道具上限 9
        s.msg.show(*s.app, kShopMsgs[1][3]);
        return;
    }
    s.revenue += shopBuyItem(st, s.cur, item); // [RE 0x42E490]
    s.itemsSold[j] = true;                     // [RE 0x42E5F6] 售罄（原版清 byte_48C2F8 但灰字留表面）
    RICH4_LOGI("shop: buy item id=%d price=%d points=%u revenue=%d (RE 0x42D37F)", item,
               kItemPrice[item - 1], pl.points, s.revenue);
}

void shopClickSellSlot(ShopState& s, int mx, int my) {
    if (mx < kBagHitL || mx >= kBagHitR || my < kBagHitT || my >= kBagHitB) {
        return;
    }
    const int slot = 5 * ((my - kBagHitT) / kBagSlotH) + (mx - kBagHitL) / kBagSlotW;
    if (slot < 0 || slot >= 15) {
        return;
    }
    GameState& st = s.app->gameState();
    if (s.page == 0) {
        const int card = st.cardState60[15 * s.cur + slot];
        if (card == 0) {
            return;
        }
        s.revenue += shopSellCard(st, s.cur, card); // [RE 0x42E0C5]
        RICH4_LOGI("shop: sell card id=%d price=%d points=%u revenue=%d (RE 0x42D37F)", card,
                   kCardPrices[card], st.players[s.cur].points, s.revenue);
    } else {
        const int item = s.itemVisibleMap[slot];
        if (item == 0) {
            return;
        }
        s.revenue += shopSellItem(st, s.cur, item, 1); // [RE 0x42E126]
        RICH4_LOGI("shop: sell item id=%d price=%d points=%u revenue=%d (RE 0x42D37F)", item,
                   kItemPrice[item - 1], st.players[s.cur].points, s.revenue);
    }
    s.pressedBtn = 3; // [RE 0x42E0F4]
    s.pressedSlot = slot;
}

void shopMouseDown(ShopState& s, int mx, int my) {
    Application& app = *s.app;
    // 非可交互（消息/动画中）：点击 = 跳过消息 / 快进动画 [RE 0x42DE10]
    if (s.phase != ShopPhase::Ready || s.msg.active()) {
        if (s.msg.active()) {
            s.msg.finish(app);
            if (s.phase == ShopPhase::Ready) {
                return; // 可交互阶段弹出的提示消息：仅跳过
            }
        }
        if (s.phase == ShopPhase::Welcome || s.phase == ShopPhase::Slide) {
            s.drawerX = kDrawerEndX; // [RE 0x42DE33] 直接到位
            s.bagX = kBagEndX;
            if (s.phase == ShopPhase::Welcome) {
                s.phase = ShopPhase::Slide;
            }
        } else if (s.phase == ShopPhase::Prompt || s.phase == ShopPhase::Farewell) {
            shopOnMsgDone(s);
        }
        return;
    }
    // 切页按钮 [RE 0x42DE60]
    if (mx >= kSwapHitL && mx < kSwapHitR && my >= kSwapHitT && my < kSwapHitB) {
        s.pressedBtn = 1;
        return;
    }
    // 离开按钮 [RE 0x42DF5B]
    if (mx >= kLeaveHitL && mx < kLeaveHitR && my >= kLeaveHitT && my < kLeaveHitB) {
        s.pressedBtn = 2;
        return;
    }
    // 卖出区 [RE 0x42E010]
    shopClickSellSlot(s, mx, my);
    if (s.pressedBtn == 3) {
        return;
    }
    // 商品列表 [RE 0x42E179]
    if (s.page == 0) {
        shopClickCardRow(s, mx, my);
    } else {
        shopClickItemRow(s, mx, my);
    }
}

void shopMouseUp(ShopState& s) {
    Application& app = *s.app;
    const int btn = s.pressedBtn;
    s.pressedBtn = 0;
    s.pressedSlot = -1;
    if (btn == 1) {
        // 切页 [RE 0x42E646..0x42E67A]
        s.page ^= 1;
        shopEnterPage(s);
        RICH4_LOGI("shop: switch page -> %d (RE 0x42D37F)", s.page);
    } else if (btn == 2) {
        // 离开：告别消息 → 关闭 [RE 0x42E699]
        s.phase = ShopPhase::Farewell;
        s.msg.show(app, kShopMsgs[s.page][4]);
    }
}

// [RE 0x42D37F] shopWndProc：商店模态窗口过程（WndProc → SDL handler + 50ms tick）
// 依据: 0x42D37F 反编译; WM_TIMER 50ms（SetTimer 0x32）；lParam 0..4 消息状态机
//   （0 欢迎 / 1 滑入 / 2 提示 / 3 可交互 / 4 离开）；鼠标命中区与买卖分支
//   见 shopMouseDown/shopMouseUp、绘制见 renderShop
bool shopModalHandler(const SDL_Event* event, void* user) {
    auto& s = *static_cast<ShopState*>(user);
    Application& app = *s.app;
    if (event == nullptr) {
        shopRender(s);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (shopTick(s)) {
            if (!app.events().exitRequested()) {
                shopRender(s);
            }
        }
        return true;
    }
    switch (event->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (event->button.button == SDL_BUTTON_RIGHT || event->button.button == SDL_BUTTON_MIDDLE) {
            app.events().requestExit(s.revenue); // [RE 0x42E88A] 右键关闭（无告别消息）
            return true;
        }
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        int mx = 0;
        int my = 0;
        app.mouseLogicalPos(mx, my);
        shopMouseDown(s, mx, my);
        if (!app.events().exitRequested()) {
            shopRender(s);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        shopMouseUp(s);
        if (!app.events().exitRequested()) {
            shopRender(s);
        }
        return true;
    }
    case SDL_EVENT_KEY_DOWN: {
        if (event->key.key == SDLK_ESCAPE) { // 重写增强（原版仅右键）
            app.events().requestExit(s.revenue);
        }
        return true;
    }
    default:
        return true;
    }
}

// ===== AI 自动买卖 [RE 0x42ED8D..0x42F30C] =====

int shopAiVisit(Application& app, int p) {
    GameState& st = app.gameState();
    Player& pl = st.players[p];
    const uint8_t persona = pl.aiPersonality;
    int revenue = 0;

    // 卖卡：性格差 == 2（卡包紧凑，空槽即停止）
    for (int j = 0; j < 15; ++j) {
        const int card = st.cardState60[15 * p + j];
        if (card == 0) {
            break;
        }
        if (static_cast<int>(kCardAiPersona[card]) - static_cast<int>(persona) == 2) {
            revenue += shopSellCard(st, p, card);
            --j; // [RE 0x42EE11] 卡包前移，重试同槽
        }
    }
    // 卖道具：性格差 == 2（全量卖出）
    for (int j = 0; j < 13; ++j) {
        const int n = st.itemStock[15 * p + j];
        if (n != 0 && static_cast<int>(kItemAiPersona[j + 1]) - static_cast<int>(persona) == 2) {
            revenue += shopSellItem(st, p, j + 1, n);
        }
    }
    // 点券不足 100：卖最低价卡 / 卖多余道具 / 下车 [RE 0x42EEA4]
    if (pl.points < 100) {
        int bestCard = 0;
        int bestPrice = 10000;
        for (int j = 0; j < 15; ++j) {
            const int card = st.cardState60[15 * p + j];
            if (card != 0 && kCardPrices[card] < bestPrice) {
                bestPrice = kCardPrices[card];
                bestCard = card;
            }
        }
        if (bestCard != 0) {
            revenue += shopSellCard(st, p, bestCard);
        }
        for (int j = 0; j < 13; ++j) {
            const int n = st.itemStock[15 * p + j];
            if (n > 1) {
                revenue += shopSellItem(st, p, j + 1, n - 1);
            }
        }
        const int travel = pl.travel & 3;
        if (travel == 1) {
            const int n = st.itemStock[15 * p + 4];
            if (n != 0) {
                revenue += shopSellItem(st, p, 5, n);
            }
        }
        if (travel == 2) {
            const int n = st.itemStock[15 * p + 5];
            if (n != 0) {
                revenue += shopSellItem(st, p, 6, n);
            }
        }
    }
    // 一半点券买卡（库存×价格加权候选 → 价格降序）[RE 0x42F03C]
    const int points = pl.points;
    if (points != 0) {
        int budgetCard = points >> 1;
        int budgetOther = points - budgetCard;
        std::vector<int> cand; // 0-based 卡序号
        for (int j = 0; j < 30; ++j) {
            for (int k = 0; k < st.propStock[j] && budgetCard >= kCardPrices[j + 1] &&
                            cand.size() < 128 &&
                            static_cast<int>(kCardAiPersona[j + 1]) - static_cast<int>(persona) != 2;
                 ++k) {
                cand.push_back(j);
            }
        }
        if (!cand.empty()) {
            // [RE 0x42D0EF] shopCardPriceCmp：价高在前
            std::sort(cand.begin(), cand.end(), [](int a, int b) {
                return kCardPrices[a + 1] > kCardPrices[b + 1];
            });
            for (size_t i = 0; i < cand.size(); ++i) {
                if (cardBagCount(st, p) == 15) { // [RE 0x42F10D] 卡包满 → 停止
                    break;
                }
                const int card = cand[i] + 1;
                if (budgetCard >= kCardPrices[card]) {
                    revenue += shopBuyCard(st, p, card);
                    budgetCard -= kCardPrices[card];
                }
            }
        }
        // 买机车/汽车（未骑该载具 + 无库存 + 预算足 + 礼物池有回收库存）[RE 0x42F16C]
        const int travel = pl.travel & 3;
        if (travel != 1 && st.itemStock[15 * p + 4] == 0 && kItemPrice[4] < budgetOther &&
            st.misc8A[4] != 0) {
            revenue += shopBuyItem(st, p, 5);
            budgetOther -= kItemPrice[4];
        }
        if (travel != 2 && st.itemStock[15 * p + 5] == 0 && kItemPrice[5] < budgetOther &&
            st.misc8A[5] != 0) {
            revenue += shopBuyItem(st, p, 6);
            budgetOther -= kItemPrice[5];
        }
        // 买礼物道具（0x4755F0 顺序，价格 <= 预算）[RE 0x42F234]
        for (int j = 0; j < 6; ++j) {
            const int slot = kAiItemOrder[j];
            if (st.itemStock[15 * p + slot] < 9 &&
                static_cast<int>(kItemAiPersona[slot + 1]) - static_cast<int>(persona) != 2) {
                if (budgetOther >= kItemPrice[slot] && st.misc8A[slot] != 0) {
                    revenue += shopBuyItem(st, p, slot + 1);
                    budgetOther -= kItemPrice[slot];
                }
            }
        }
    }
    RICH4_LOGI("shop AI: p%d personality=%u revenue=%d points=%u (RE 0x42E931 else)", p, persona,
               revenue, pl.points);
    return revenue;
}

// ===== 自家百货中奖 [RE 0x42E93F..0x42EA2B] =====

void shopOwnerGift(Application& app, int p) {
    GameState& st = app.gameState();
    char text[96];
    const char* name = nullptr;
    int price = 0;
    if (dbg::roll(dbg::SlotShop, 2) != 0) {
        const int item = drawGiftCard(app, p); // [RE 0x445ADA] 1-based / 0=池空
        if (item == 0) {
            return; // 原版池空时 0 索引别名到烏龜卡（重写跳过）
        }
        name = kItemBagNames[item];
        price = kItemPrice[item - 1];
    } else {
        const int card = drawFreeCard(st, p); // [RE 0x441E12] 1-based / 0=池空
        if (card == 0) {
            return;
        }
        name = kCardNames[card];
        price = kCardPrices[card];
    }
    std::snprintf(text, sizeof text, kGiftMsg, name);
    showMessage(app, text, 1500); // [RE 0x42EA14]
    playValueLine(app, p, price); // [RE 0x42EA23 sub_44F230]
    RICH4_LOGI("shop: owner gift p%d %s price=%d (RE 0x42E931)", p, name, price);
}

// ===== 人类商店 UI [RE 0x42D37F] =====

void shopOpenUi(Application& app, int specIdx) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    ShopState s;
    s.app = &app;
    s.cur = cur;
    if (auto blob = st.panel.read(10)) { // [RE 0x42EA4C]
        s.sheet.load(std::move(*blob));
    }
    if (auto blob = st.panel.read(11)) { // [RE 0x42EA66]
        s.bagSheet.load(std::move(*blob));
    }
    if (s.sheet.frameCount() < 38 || s.bagSheet.frameCount() < 2) {
        RICH4_LOGW("shopDialog: panel.mkf[10/11] unavailable (RE 0x42E931)");
        return;
    }
    // 卡片商品生成：rand%10+6 件，按 g_propStock 库存加权不重复抽取（副本，不扣真实库存）
    // [RE 0x42EAF7..0x42EBFE]
    uint8_t stock[30];
    std::memcpy(stock, st.propStock, sizeof stock);
    const int n = dbg::roll(dbg::SlotShop, 10) + 6;
    for (int i = 0; i < n; ++i) {
        uint8_t pool[128];
        int cnt = 0;
        for (int j = 0; j < 30 && cnt < 128; ++j) {
            for (int k = 0; k < stock[j] && cnt < 128; ++k) {
                pool[cnt++] = static_cast<uint8_t>(j);
            }
        }
        if (cnt == 0) {
            break; // 原版无守卫（v42 越界风险），重写防越界
        }
        const int pick = pool[dbg::roll(dbg::SlotShop, cnt)] + 1;
        s.cardsForSale[i] = static_cast<uint8_t>(pick);
        --stock[pick - 1];
    }
    // 道具商品：礼物池非 0 的 8 槽紧凑排列 [RE 0x42EC07..0x42ECBB]
    int slot = 0;
    for (int j = 0; j < 8; ++j) {
        if (st.misc8A[j] != 0) {
            s.itemsForSale[slot++] = static_cast<uint8_t>(j + 1);
        }
    }
    // 引导消息标志 [RE 0x42D44E] byte_48C349[0/1] = byte_497159 ^ 1（设置项：动画/提示开关）
    s.visited[0] = st.settings[1] ^ 1;
    s.visited[1] = s.visited[0];
    RICH4_LOGI("shop: open cards=%d items=%d visited=%d (RE 0x42E931)", n, slot,
               s.visited[0] ? 1 : 0);

    app.audio().pushSceneMusic(6); // [RE 0x42ECF6] musicPlayScene(6)
    s.page = 0;
    shopEnterPage(s);
    const int revenue = runModal(app, &shopModalHandler, &s, 50); // [RE 0x42ED00]
    app.audio().resumeSceneMusic();                                // [RE 0x42ED0F]
    st.specPts[specIdx].fund += revenue;     // [RE 0x42ED75] +40 公库
    st.specPts[specIdx].fundPaid += revenue; // [RE 0x42ED7E] +44 累计
    trace::logf("shop revenue=%d", revenue);
    RICH4_LOGI("shop: close revenue=%d fund=%d (RE 0x42E931)", revenue, st.specPts[specIdx].fund);
}

} // namespace

// [RE 0x42E931] shopDialog：百貨公司（特殊地點 case 15）
// 依据: 0x42E931 反编译; 中奖分支（owner==cur+1）+ 人类 runModal(sub_42D37F) / AI else 段;
//   实参 objId = cellEnt+32 special（6000+n），非 cellEnts 数组下标;
//   营业额（买 10×价 / 卖 1×价）入 specPt.fund(+40)/fundPaid(+44)
void shopDialog(Application& app, int objId) {
    trace::logf("dialog open name=shop");
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return;
    }
    if (objId <= 6000 || objId >= 8000) { // [RE 0x42E935]
        return;
    }
    const int idx = objId - 6000;
    if (idx < 0 || idx >= static_cast<int>(st.specPts.size())) {
        return;
    }
    // 自己拥有 → 董事长赠礼 [RE 0x42E93F]
    if (st.specPts[idx].owner == cur + 1) {
        shopOwnerGift(app, cur);
    }
    if (st.players[cur].alive == 1) {
        shopOpenUi(app, idx); // [RE 0x42EA39] 人类
    } else {
        const int revenue = shopAiVisit(app, cur); // [RE 0x42ED8D] AI/托管
        st.specPts[idx].fund += revenue;
        st.specPts[idx].fundPaid += revenue;
    }
}

} // namespace rich4
