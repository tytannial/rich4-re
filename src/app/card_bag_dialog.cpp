#include "game/app/card_bag_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

#include "game/app/card_effects.h"
#include "game/app/card_lines.h"
#include "game/app/ai_card.h"
#include "game/app/confirm_dialog.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/event_stack.h"
#include "game/app/facility_dialog.h"
#include "game/app/item_bag_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/app/player_select_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {
namespace {

// 面板 blit 落点（panel.mkf[11] 帧0/帧1，资源内偏移 0,0）[RE 0x441BAA 0x441C5F]
constexpr int kPanelX = 14;
constexpr int kPanelY = 130;
constexpr int kCardFrame = 0;  // 卡片栏背景帧

// 卡名网格（资源内坐标）[RE 0x441B0A: v5=45 步长80 >365换行x=45, v6=33 步长56]
constexpr int kCol0 = 45;
constexpr int kColStep = 80;
constexpr int kColMax = 365;
constexpr int kRow0 = 33;
constexpr int kRowStep = 56;
constexpr int kSlots = 15;

// 命中/高亮（屏幕坐标）[RE 0x4416F0]
constexpr int kHitX0 = 19;
constexpr int kHitY0 = 135;
constexpr int kHitW = 80;
constexpr int kHitH = 56;
constexpr int kHiL0 = 20;   // 下沉区 l = 80*(slot%5)+20
constexpr int kHiT0 = 136;  // 下沉区 t = 56*(slot/5)+136
constexpr int kHiW = 78;
constexpr int kHiH = 54;

struct CardBagState {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    int cur = 0;
    int pressedSlot = -1;  // 按住中的格（下沉浮雕），-1 无
    int selId = 0;         // 按住时记下的卡 id（抬起送出）
};

// [RE 0x441B0A] drawCardBag：帧0 背景 + 5×3 网格卡名（非空槽）
void renderCardBag(CardBagState& s) {
    drawCardBagAt(*s.app, *s.sheet, s.cur, kPanelX, kPanelY);
    Surface& dst = s.app->surface();
    // [RE 0x44184A] 按住格 highlightRect 下沉浮雕（1px/kChannelHalf；原版无白框）
    if (s.pressedSlot >= 0) {
        const int l = kHiL0 + kHitW * (s.pressedSlot % 5);
        const int top = kHiT0 + kHitH * (s.pressedSlot / 5);
        pressDown(dst, l, top, kHiW, kHiH, 1, kChannelHalf);
    }
}

// [RE 0x4416F0] slot 命中（屏幕坐标 x∈[19,419) y∈[135,303)），返回 0..14 或 -1
int hitCardSlot(int px, int py) {
    const int lx = px - kHitX0;
    const int ly = py - kHitY0;
    if (lx < 0 || px >= 419 || ly < 0 || py >= 303) {
        return -1;
    }
    return 5 * (ly / kHitH) + (lx / kHitW);
}

// [RE 0x4416F0] 卡片选择模态：LBUTTONDOWN 选格高亮，LBUTTONUP 确认，右键/中键/Esc 取消
bool cardModalHandler(const SDL_Event* event, void* user) {
    auto& s = *static_cast<CardBagState*>(user);
    GameState& st = s.app->gameState();
    if (!event) {
        renderCardBag(s);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button == SDL_BUTTON_RIGHT ||
                event->button.button == SDL_BUTTON_MIDDLE) {
                s.app->audio().playEffect(4);  // g_uiSoundCancel
                s.app->events().requestExit(0);
                return true;
            }
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            const int slot = hitCardSlot(static_cast<int>(event->button.x),
                                         static_cast<int>(event->button.y));
            if (slot >= 0 && slot < kSlots) {
                const int card = st.cardState60[15 * s.cur + slot];
                if (card != 0) {
                    s.pressedSlot = slot;  // [RE 0x44184A] highlightRect 下沉
                    s.selId = card;
                    s.app->audio().playEffect(1);  // [RE 0x44187C] g_uiSoundClick
                    renderCardBag(s);
                }
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            if (s.selId != 0) {  // [RE 0x441870] g_cardSelId → postModalExit
                s.pressedSlot = -1;
                renderCardBag(s);
                s.app->events().requestExit(s.selId);
            }
            return true;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (event->key.key == SDLK_ESCAPE) {
                s.app->events().requestExit(0);
            }
            return true;
        }
        default:
            return true;
    }
}

// 未实现卡：返回 0（等价原版「不可用 → 重选」，不消耗）
int cardEffectStub(Application&, int id) {
    RICH4_LOGI("card effect id=%d not implemented (P4, RE g_cardEffectFuncs 0x475D5C)", id);
    return 0;
}

int cardEffectInvite(Application& app) {
    return useInviteGodCard(app) ? 1 : 0;  // [RE 0x444E1A] 请神符(卡23)
}
int cardEffectBanish(Application& app) {
    return useBanishGodCard(app) ? 1 : 0;  // [RE 0x444C45] 送神符(卡22)
}

// [RE 0x441CCD] g_cardEffectFuncs[id]() 分派。返回非 0 = 结束流程，0 = 重选
int cardEffect(Application& app, int id) {
    switch (id) {
        case 1:  return cardEffectEqualRich(app);  // 均富卡 0x4420D8
        case 2:  return cardEffectEqualPoor(app);  // 均貧卡 0x4421B4
        case 3:  return cardEffectBuyLand(app);    // 購地卡 0x442325
        case 4:  return cardEffectSwapLand(app);   // 換地卡 0x442622
        case 5:  return cardEffectSwapHouse(app);  // 換屋卡 0x442B02
        case 6:  return cardEffectTurn(app);       // 轉向卡 0x442F4D
        case 7:  return cardRebuildEffect(app);    // 改建卡 0x44309B
        case 8:  return cardEffectAuction(app);    // 拍賣卡 0x443225
        case 9:  return cardEffectAngel(app);      // 天使卡 0x4434C0
        case 10: return cardEffectDevil(app);      // 惡魔卡 0x4436E0
        case 11: return cardEffectMonster(app);    // 怪獸卡 0x443917
        case 12: return cardEffectDemolish(app);   // 拆除卡 0x443B0F
        case 13: return cardEffectSteal(app);      // 搶奪卡 0x443E3D
        case 14: return cardEffectStay(app);       // 停留卡 0x443F80
        case 15: return cardEffectHibernate(app);  // 冬眠卡 0x4440EA
        case 16: return cardEffectSleepwalk(app);  // 夢遊卡 0x4441DC
        case 17: return cardEffectFrame(app);      // 陷害卡 0x4444BF
        case 22: return cardEffectBanish(app);     // 送神符 0x444C45
        case 23: return cardEffectInvite(app);     // 请神符 0x444E1A
        case 24: return cardEffectRedStock(app);   // 紅卡 0x444F25
        case 25: return cardEffectBlackStock(app); // 黑卡 0x44503F
        case 26: return cardEffectTaxAudit(app);   // 查稅卡 0x4451F0
        case 27: return cardEffectPriceUp(app);    // 漲價卡 0x44542D
        case 28: return cardEffectSeal(app);       // 查封卡 0x445593
        case 29: return cardEffectAlly(app);       // 同盟卡 0x445710
        case 30: return cardEffectTurtle(app);     // 烏龜卡 0x4458DF
        default: return cardEffectStub(app, id);
    }
}

}  // namespace

// [RE 0x441B0A] drawCardBagAt：卡片栏绘制（帧0 背景 + 5×3 网格卡名）到 (x,y)
// 依据: 0x441B0A 反编译; 工具条 (14,130) / 百货商店 (bagX,293) 共用
void drawCardBagAt(Application& app, const UiImage& sheet, int player, int x, int y) {
    Surface& dst = app.surface();
    GameState& st = app.gameState();
    blitElementOpaque(dst, sheet.frame(kCardFrame), x, y);
    TextRenderer& t = app.text();
    t.setFont(20, 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
    int cx = kCol0;
    int cy = kRow0;
    for (int i = 0; i < kSlots; ++i) {
        const int card = st.cardState60[15 * player + i];
        if (card != 0) {
            t.drawText(dst, kCardNames[card], x + cx, y + cy, 2);
            cx += kColStep;
            if (cx > kColMax) {
                cx = kCol0;
                cy += kRowStep;
            }
        }
    }
}

// [RE 0x44309B] cardRebuildEffect：见头文件说明。针对当前玩家所站格 special(objId)
int cardRebuildEffect(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return 0;
    }
    const Player& pl = st.players[cur];
    if (pl.cellEntId == 0 || pl.cellEntId >= st.cellEnts.size()) {
        return 0;
    }
    const uint16_t id = st.cellEnts[pl.cellEntId].special;
    if (id >= 2000 && id < 4000) {  // 住宅用地：level != 0 才可改建
        const int i = id - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        Estate& es = st.estates[i];
        if (es.level == 0) {
            return 0;
        }
        const bool wasChain = (es.type == 1);
        playCardLine(app, cur, 6);  // [RE 0x4430xx 使用者台词 改建]
        es.type ^= 1;
        if (!wasChain && es.level > 1) {
            es.level = 1;
        }
        buildMiniMapMarks(app);
        cardBagRemove(st, cur, 7);  // [RE 0x44320A] sub_441343(cur,7)
        RICH4_LOGI("cardRebuild: estate[%d] %s type=%u level=%u (RE 0x44309B)", i,
                   wasChain ? "chain->house" : "house->chain", es.type, es.level);
        return 1;
    }
    if (id > 4000 && id < 6000) {  // 商業用地：sub != 0 才可变更设施
        const int i = id - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        Corp& cp = st.corps[i];
        if (cp.sub == 0) {
            return 0;
        }
        playCardLine(app, cur, 6);  // [RE 使用者台词 改建]
        const int r = selectFacilityDialog(app, 1);  // [RE 0x4431C4]
        if (r < 0) {
            return 0;
        }
        cp.type = static_cast<uint8_t>(r);
        if ((cp.type == 0 || cp.type == 3) && cp.sub > 1) {  // 公園/加油站 降级 [RE 0x4431FC]
            cp.sub = 1;
        }
        buildMiniMapMarks(app);
        cardBagRemove(st, cur, 7);  // [RE 0x44320A]
        RICH4_LOGI("cardRebuild: corp[%d] type -> %u sub %u (RE 0x44309B)", i, cp.type, cp.sub);
        return 1;
    }
    return 0;  // [TODO P4] 台词 sub_44EF41
}

// [RE 0x441BAA] useCardFlow
void useCardDialog(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return;
    }
    // [RE 0x441BAA AI 分支] 电脑/托管（alive&6）且 aiCardItem bit0（使用卡片）：
    //   手牌随机序逐张试 aiCardSelect（persona 判定 + 目标预选 dword_48BE58）→ 命中即用
    if (st.players[cur].alive != 1) {
        if ((st.players[cur].alive & 6) == 0 || (st.players[cur].aiCardItem & 1) == 0) {
            return;
        }
        int cards[15];
        int n = 0;
        for (int i = 0; i < 15; ++i) {
            const uint8_t c = st.cardState60[15 * cur + i];
            if (c != 0) {
                cards[n++] = c;
            }
        }
        if (n == 0) {
            return;
        }
        for (int i = n - 1; i > 0; --i) {  // Fisher-Yates（原版随机试序）
            const int j = std::rand() % (i + 1);
            const int t = cards[i];
            cards[i] = cards[j];
            cards[j] = t;
        }
        for (int i = 0; i < n; ++i) {
            const int id = cards[i];
            if (aiCardSelect(app, id) != 1) {
                continue;
            }
            char text[64];
            std::snprintf(text, sizeof text, "使用%s", kCardNames[id]);
            showCardGet(app, id, text);
            cardEffect(app, id);
            return;  // 原版每回合最多使用一张
        }
        return;
    }
    st.manualView = false;  // [RE 0x41D546] dword_48BE18=0（sub_41906A 全屏重绘停动画由全量重绘替代）

    UiImage sheet;
    if (auto blob = st.panel.read(11)) {  // [RE 0x441BE4] mkfRead(panel, 11)
        sheet.load(std::move(*blob));
    }
    if (sheet.frameCount() < kCardFrame + 1) {
        RICH4_LOGW("useCardDialog: panel.mkf[11] unavailable (RE 0x441BAA)");
        return;
    }

    for (;;) {
        CardBagState s;
        s.app = &app;
        s.sheet = &sheet;
        s.cur = cur;
        const int id = runModal(app, &cardModalHandler, &s);  // [RE 0x441C83]
        if (id <= 0) {
            break;  // 取消 → 结束
        }
        char text[64];
        std::snprintf(text, sizeof text, "使用%s", kCardNames[id]);  // [RE 0x465305]
        showCardGet(app, id, text);  // [RE 0x441F73/0x441CBC]
        if (cardEffect(app, id) != 0) {
            break;  // 效果非 0 → 结束
        }
        // 效果返回 0 → 重弹面板再选
    }
}

// ===== [RE 0x44192A / 0x4413EC] 生日收卡：从目标玩家卡包/道具包选一项 =====

namespace {

// 面板落点（原版 sub_44192A blitElementFullscreen (14,70)/(14,270)）
constexpr int kGiftPanelX = 14;
constexpr int kGiftCardY = 70;
constexpr int kGiftItemY = 270;
// 命中（原版 sub_4413EC）：x∈[19,419)；卡 y∈[75,243)、道具 y∈[275,443)
constexpr int kGiftHitX0 = 19;
constexpr int kGiftHitX1 = 419;
constexpr int kGiftCardHitY0 = 75;
constexpr int kGiftCardHitY1 = 243;
constexpr int kGiftItemHitY0 = 275;
constexpr int kGiftItemHitY1 = 443;
// 高亮（原版 highlightRect）：l=80i+20 / t=56j+76（卡）或 +276（道具），78×54
constexpr int kGiftHiW = 78;
constexpr int kGiftHiH = 54;

struct GiftSelectState {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    int target = 0;
    bool hasItems = false;
    uint8_t itemMap[15] = {};
    int pressed = 0;      // 0=未选；卡 id 或 0x8000|道具 id
    int pressedSlot = -1; // 高亮槽
    bool pressedItem = false;
};

bool targetHasItems(const GameState& st, int target) {
    for (int i = 0; i < 13; ++i) {
        if (st.itemStock[15 * target + i] != 0) {
            return true;
        }
    }
    return false;
}

void renderGiftSelect(GiftSelectState& s) {
    drawCardBagAt(*s.app, *s.sheet, s.target, kGiftPanelX, kGiftCardY);
    if (s.hasItems) {
        drawItemBagAt(*s.app, *s.sheet, s.target, kGiftPanelX, kGiftItemY, s.itemMap);
    }
    // [RE 0x4413EC] highlightRect 按下下沉（卡区 +76 / 道具区 +276）
    if (s.pressedSlot >= 0) {
        const int l = 20 + kHitW * (s.pressedSlot % 5);
        const int t = (s.pressedItem ? 276 : 76) + kHitH * (s.pressedSlot / 5);
        pressDown(s.app->surface(), l, t, kGiftHiW, kGiftHiH, 1, kChannelHalf);
    }
}

int giftHitSlot(const GiftSelectState& s, int x, int y) {
    if (x < kGiftHitX0 || x >= kGiftHitX1) {
        return -1;
    }
    if (y >= kGiftCardHitY0 && y < kGiftCardHitY1) {
        return 5 * ((y - kGiftCardHitY0) / kHitH) + (x - kGiftHitX0) / kHitW;
    }
    if (s.hasItems && y >= kGiftItemHitY0 && y < kGiftItemHitY1) {
        return 5 * ((y - kGiftItemHitY0) / kHitH) + (x - kGiftHitX0) / kHitW;
    }
    return -1;
}

bool giftSelectHandler(const SDL_Event* event, void* user) {
    auto& s = *static_cast<GiftSelectState*>(user);
    GameState& st = s.app->gameState();
    if (event == nullptr) {
        renderGiftSelect(s);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button == SDL_BUTTON_RIGHT ||
                event->button.button == SDL_BUTTON_MIDDLE) {
                s.app->audio().playEffect(4); // g_uiSoundCancel
                s.app->events().requestExit(0);
                return true;
            }
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            const int x = static_cast<int>(event->button.x);
            const int y = static_cast<int>(event->button.y);
            const int slot = giftHitSlot(s, x, y);
            if (slot < 0 || slot >= 15) {
                return true;
            }
            const bool item = (y >= kGiftItemHitY0);
            const int id = item ? s.itemMap[slot] : st.cardState60[15 * s.target + slot];
            if (id == 0) {
                return true;
            }
            s.pressed = item ? (0x8000 | id) : id;
            s.pressedSlot = slot;
            s.pressedItem = item;
            s.app->audio().playEffect(1); // g_uiSoundClick
            renderGiftSelect(s);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            if (s.pressed != 0) { // [RE 0x4413EC] WM_LBUTTONUP → postModalExit(选择)
                const int r = s.pressed;
                s.pressed = 0;
                s.pressedSlot = -1;
                renderGiftSelect(s);
                s.app->events().requestExit(r);
            }
            return true;
        }
        default:
            return true;
    }
}

} // namespace

int selectCardOrItemFromPlayerDialog(Application& app, int targetPlayer) {
    GameState& st = app.gameState();
    if (targetPlayer < 0 || targetPlayer >= 4) {
        return 0;
    }
    UiImage sheet;
    if (auto blob = st.panel.read(11)) { // [RE 0x44192A] sub_450441(panel, 11)
        sheet.load(std::move(*blob));
    }
    if (sheet.frameCount() < 2) {
        RICH4_LOGW("selectCardOrItem: panel.mkf[11] unavailable (RE 0x44192A)");
        return 0;
    }
    GiftSelectState s;
    s.app = &app;
    s.sheet = &sheet;
    s.target = targetPlayer;
    s.hasItems = targetHasItems(st, targetPlayer);
    RICH4_LOGI("selectCardOrItem: target=%d items=%d (RE 0x44192A)", targetPlayer, s.hasItems);
    return runModal(app, &giftSelectHandler, &s, 0);
}

// [RE 0x44476A] 嫁祸卡目标选择（a2=0, a3=0：直接选目标，AI 自动）
int passOnCardDialog(Application& app, int user) {
    GameState& st = app.gameState();
    if (user < 0 || user >= 4) {
        return -1;
    }
    int target = -1;
    char text[160];
    if (st.players[user].alive != 1) {
        // AI：欠款最多者（无则随机存活无状态者）[RE 0x44476A AI 分支]
        target = findMaxCreditor(st, user);
        if (target == -1) {
            target = pickRandomActiveTarget(st, user);
        }
        if (target == -1) {
            return -1;
        }
        std::snprintf(text, sizeof text, "%s\n\n嫁禍卡生效！", playerNameNoSpace(st, user).c_str());
        showCardGet(app, 19, text); // [RE 0x44476A sub_441F73(19,...)]
        std::snprintf(text, sizeof text, "嫁禍給%s！", playerNameNoSpace(st, target).c_str());
        showMessage(app, text, 1500); // [RE 0x44476A showMessage]
    } else {
        int ids[8];
        int n = 0;
        for (int i = 0; i < st.playerCount && i < 8; ++i) {
            if (i != user && st.players[i].alive != 0) {
                ids[n++] = i;
            }
        }
        if (n == 0) {
            return -1;
        }
        std::snprintf(text, sizeof text, "%s\n\n嫁禍卡生效！", playerNameNoSpace(st, user).c_str());
        showCardGet(app, 19, text); // [RE 0x44476A sub_441F73(19,...)]
        if (n == 1) {
            std::snprintf(text, sizeof text, "是否嫁禍給%s？", // [RE 0x46534E]
                          playerNameNoSpace(st, ids[0]).c_str());
            if (!confirmDialog(app, text, 220, 320)) { // [RE 0x44476A askDialog]
                return -1;
            }
            target = ids[0];
        } else {
            target = selectPlayerDialog(app, ids, n, "請選擇嫁禍對象..."); // [RE 0x46535D]
            if (target == -1) {
                return -1;
            }
        }
    }
    cardBagRemove(st, user, 19); // [RE 0x44476A sub_441343(a1,19)]
    playCardLine(app, user, 18); // [RE 0x4447xx 使用者台词 嫁禍]
    playCardLine(app, target, kSpeechPassOnTarget);  // [RE 0x4447xx 被嫁祸者台词]
    RICH4_LOGI("passOnCard: user=%d -> target=%d (RE 0x44476A)", user, target);
    return target;
}

}  // namespace rich4
