#include "game/app/item_bag_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/ai_item.h"
#include "game/app/event_stack.h"
#include "game/app/item_effects.h"   // [RE 0x475DD4] g_itemEffectFuncs 分派
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/new_game_tables.h"
#include "game/application.h"
#include "game/core/debug_hooks.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"

namespace rich4 {
namespace {

// 面板落点 / 帧号（panel.mkf[11]：帧1=道具背景，帧 id+1=道具图标）[RE 0x447C6E]
constexpr int kPanelX = 14;
constexpr int kPanelY = 130;
constexpr int kItemFrame = 1;

// 网格（资源内坐标，同卡片栏）[RE 0x447C6E: 图标 (x-16,y)，数量 (x+34,y-10)]
constexpr int kCol0 = 45;
constexpr int kColStep = 80;
constexpr int kColMax = 365;
constexpr int kRow0 = 33;
constexpr int kRowStep = 56;
constexpr int kItemTypes = 13;  // 道具类型 1..13

// 命中/高亮（屏幕坐标）[RE 0x445C14]
constexpr int kHitX0 = 19;
constexpr int kHitY0 = 135;
constexpr int kHitW = 80;
constexpr int kHitH = 56;
constexpr int kHiL0 = 20;
constexpr int kHiT0 = 136;
constexpr int kHiW = 78;
constexpr int kHiH = 54;

struct ItemBagState {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    int cur = 0;
    int pressedSlot = -1;  // 按住中的格（下沉浮雕），-1 无
    int selId = 0;         // 按住时记下的道具 id（抬起送出）
    uint8_t visibleMap[15] = {};  // [RE 0x447C6E] g_itemVisibleMap：网格位 → 道具 id
};

// [RE 0x447C6E] drawItemBag：帧1 背景 + 逐格图标(帧 id+1) + ×N 数量 + visibleMap
void renderItemBag(ItemBagState& s) {
    drawItemBagAt(*s.app, *s.sheet, s.cur, kPanelX, kPanelY, s.visibleMap);
    Surface& dst = s.app->surface();
    // [RE 0x447DEE..0x447E24] 载具中（機車/汽車）→ 右下角「下車」按钮：
    //   panel[11] 帧 15（机车）/16（汽车）@ 资源内 (325,117)；g_itemVisibleMap[14] = 14
    //   （固定网格位 14 = 行 2 列 4，与图标位置对齐；点击返回 id 14 → 0x447C00 下车）
    const uint8_t travel = s.app->gameState().players[s.cur].travel;
    if (travel == 1 || travel == 2) {
        const int frame = (travel == 1) ? 15 : 16;
        if (static_cast<size_t>(s.sheet->frameCount()) > static_cast<size_t>(frame)) {
            blitElement(dst, s.sheet->frame(frame), kPanelX + 325, kPanelY + 117, false);
        }
        s.visibleMap[14] = 14;
    }
    // [RE 0x445D59] 按住格 highlightRect 下沉浮雕（1px/kChannelHalf；原版无白框）
    if (s.pressedSlot >= 0 && s.pressedSlot < 15) {
        const int l = kHiL0 + kHitW * (s.pressedSlot % 5);
        const int top = kHiT0 + kHitH * (s.pressedSlot / 5);
        pressDown(dst, l, top, kHiW, kHiH, 1, kChannelHalf);
    }
}

int hitItemSlot(int px, int py) {
    const int lx = px - kHitX0;
    const int ly = py - kHitY0;
    if (lx < 0 || px >= 419 || ly < 0 || py >= 303) {
        return -1;
    }
    return 5 * (ly / kHitH) + (lx / kHitW);
}

// [RE 0x445C14] itemModal：几何同 cardModal，命中经 visibleMap → 道具 id
bool itemModalHandler(const SDL_Event* event, void* user) {
    auto& s = *static_cast<ItemBagState*>(user);
    if (!event) {
        renderItemBag(s);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button == SDL_BUTTON_RIGHT ||
                event->button.button == SDL_BUTTON_MIDDLE) {
                s.app->audio().playEffect(4);
                s.app->events().requestExit(0);
                return true;
            }
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            const int slot = hitItemSlot(static_cast<int>(event->button.x),
                                         static_cast<int>(event->button.y));
            if (slot >= 0 && slot < 15) {
                const int itemId = s.visibleMap[slot];
                if (itemId != 0) {
                    s.pressedSlot = slot;  // [RE 0x445D59] highlightRect 下沉
                    s.selId = itemId;
                    s.app->audio().playEffect(1);  // [RE 0x445D75] g_uiSoundClick
                    renderItemBag(s);
                }
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            if (s.selId != 0) {  // [RE 0x445DA3] g_itemSelId → postModalExit
                s.pressedSlot = -1;
                renderItemBag(s);
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

}  // namespace

// [RE 0x447C6E] drawItemBagAt：道具栏绘制（帧1 背景 + 图标帧 id+1 + ×N）到 (x,y)
// 依据: 0x447C6E 反编译; 工具条 (14,130) / 百货商店 (bagX,293) 共用；visibleMap 非空时写映射
void drawItemBagAt(Application& app, const UiImage& sheet, int player, int x, int y,
                   uint8_t visibleMap[15]) {
    Surface& dst = app.surface();
    GameState& st = app.gameState();
    if (visibleMap != nullptr) {
        std::memset(visibleMap, 0, 15);
    }
    blitElementOpaque(dst, sheet.frame(kItemFrame), x, y);
    TextRenderer& t = app.text();
    t.setFont(20, 0xFFFFFF, 0x101010, kTextStyleShadow | kTextStyleBold, 0);
    int vis = 0;
    int cx = kCol0;
    int cy = kRow0;
    for (int i = 0; i < kItemTypes; ++i) {
        const int n = st.itemStock[15 * player + i];
        if (n != 0) {
            const int itemId = i + 1;
            blitElement(dst, sheet.frame(itemId + 1), x + (cx - 16), y + cy, false);
            char buf[16];
            std::snprintf(buf, sizeof buf, "×%d", n);  // [RE 0x4653E0]
            t.drawText(dst, buf, x + cx + 34, y + cy - 10, 1);
            if (visibleMap != nullptr) {
                visibleMap[vis] = static_cast<uint8_t>(itemId);
            }
            ++vis;
            cx += kColStep;
            if (cx > kColMax) {
                cx = kCol0;
                cy += kRowStep;
            }
        }
    }
}

// [RE 0x447D97] itemPanelFlow
void itemBagDialog(Application& app) {
    trace::logf("dialog open name=item_bag");
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return;
    }
    // [RE 0x447D97 AI 分支] 电脑/托管（alive&6）且 aiCardItem bit1（使用道具）：
    //   手持道具（**排除 id10 時光機**，0x447FDD `i != 9`）随机起点、**最多试 4 件**
    //   （0x448009）逐件 aiItemSelect（persona + 目标预选 dword_48BE64）→ 命中即用一件
    if (st.players[cur].alive != 1) {
        if ((st.players[cur].alive & 6) == 0 || (st.players[cur].aiCardItem & 2) == 0) {
            return;
        }
        int items[13];
        int n = 0;
        for (int i = 0; i < 13; ++i) {
            if (st.itemStock[15 * cur + i] != 0 && i != 9) {
                items[n++] = i + 1;
            }
        }
        if (n == 0) {
            return;
        }
        int idx = (n <= 4) ? 0 : dbg::roll(dbg::SlotAi, n);  // [RE 0x447FF3] 随机起点
        for (int t = 0; t < 4; ++t) {
            const int id = items[idx];
            if (id == 0) {
                break;
            }
            if (aiItemSelect(app, id) == 1) {
                char text[64];
                std::snprintf(text, sizeof text, "使用%s", kItemBagNames[id]);  // [RE 0x4653E5]
                showMessage(app, text, 1500);
                itemEffect(app, id);
                return;
            }
            if (++idx == n) {
                idx = 0;  // [RE 0x448012] 回绕
            }
        }
        return;
    }
    st.manualView = false;  // [RE 0x41D546]

    UiImage sheet;
    if (auto blob = st.panel.read(11)) {  // [RE 0x447DCB] mkfRead(panel, 11)
        sheet.load(std::move(*blob));
    }
    if (sheet.frameCount() < 17) {  // 帧 0..16（15/16 = 机车/汽车下車按鈕）
        RICH4_LOGW("itemBagDialog: panel.mkf[11] unavailable/short (RE 0x447D97)");
        return;
    }

    for (;;) {
        ItemBagState s;
        s.app = &app;
        s.sheet = &sheet;
        s.cur = cur;
        const int id = runModal(app, &itemModalHandler, &s, 0, true, false);  // [RE 0x447ECD]
        if (id <= 0) {
            break;  // 取消 → 结束
        }
        if (itemEffect(app, id) != 0) {
            break;  // 效果非 0 → 结束
        }
        // 效果返回 0 → 重弹面板再选
    }
}

}  // namespace rich4
