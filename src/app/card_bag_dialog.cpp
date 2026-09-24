#include "game/app/card_bag_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "game/app/event_stack.h"
#include "game/app/facility_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
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
constexpr int kHiL0 = 20;   // 高亮框 l = 80*(slot%5)+20
constexpr int kHiT0 = 136;  // 高亮框 t = 56*(slot/5)+136
constexpr int kHiW = 78;
constexpr int kHiH = 54;
constexpr uint16_t kWhite555 = 0x7FFF;

struct CardBagState {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    int cur = 0;
    int selSlot = -1;  // 选中槽（高亮），-1 无
    int selId = 0;     // 选中卡 id（0 无）
};

// [RE 0x441B0A] drawCardBag：帧0 背景 + 5×3 网格卡名（非空槽）
void renderCardBag(CardBagState& s) {
    Surface& dst = s.app->surface();
    GameState& st = s.app->gameState();
    blitElementOpaque(dst, s.sheet->frame(kCardFrame), kPanelX, kPanelY);
    TextRenderer& t = s.app->text();
    t.setFont(20, 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
    int x = kCol0;
    int y = kRow0;
    for (int i = 0; i < kSlots; ++i) {
        const int card = st.cardState60[15 * s.cur + i];
        if (card != 0) {
            t.drawText(dst, kCardNames[card], kPanelX + x, kPanelY + y, 2);
            x += kColStep;
            if (x > kColMax) {
                x = kCol0;
                y += kRowStep;
            }
        }
    }
    if (s.selSlot >= 0) {
        const int l = kHiL0 + kHitW * (s.selSlot % 5);
        const int top = kHiT0 + kHitH * (s.selSlot / 5);
        drawRectBorder(dst, l, top, kHiW, kHiH, kWhite555);
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
                    s.selSlot = slot;
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
                s.selSlot = -1;
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
        case 7:  return cardRebuildEffect(app);  // 改建卡 0x44309B
        case 22: return cardEffectBanish(app);   // 送神符 0x444C45
        case 23: return cardEffectInvite(app);   // 请神符 0x444E1A
        default: return cardEffectStub(app, id);
    }
}

}  // namespace

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
    if (cur < 0 || cur >= 4 || st.players[cur].alive != 1) {
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

}  // namespace rich4
