#include "game/app/item_effects.h"

#include <SDL3/SDL.h>

#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/item_lines.h"
#include "game/app/map_tables.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {
namespace {

// ===== [RE 0x44EF41] 角色气泡台词 =====
struct ItemLineCtx {
    Application* app = nullptr;
    const char* line = nullptr;
    int player = 0;
    uint64_t startMs = 0;
};

void drawItemLine(ItemLineCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    renderGameFrame(app);
    const UiImage& tip = st.estateTiles;  // [RE 0x48BAD8] g_tipFrame = data.mkf[517]
    if (tip.frameCount() > 5) {
        blitElement(dst, tip.frame(5), 220, 130, false);  // [RE 0x456418]
    }
    // 说话头像 = pieceSprites[p] 帧 expr+1（expr=0 → 帧 1）[RE 0x44EF41]
    if (ctx.player >= 0 && ctx.player < 9 && st.pieceSprites[ctx.player].frameCount() > 1) {
        blitElement(dst, st.pieceSprites[ctx.player].frame(1), 170, 130, false);
    }
    const char* t = ctx.line;
    if (t[0] == '#') {
        t += 5;  // 跳过 #NNNN 语音前缀（语音进入时已播）
    }
    if (t[0] == '@') {
        // '#NNNN@MM' → 显示表情帧 MM-1（dword_48BAD4 = data.mkf[519]）[RE 0x44EF41]
        const int mm = (t[1] - '0') * 10 + (t[2] - '0');
        const UiImage& face = st.mapTiles519;  // [RE 0x48BAD4] data.mkf[519]
        if (mm >= 1 && face.frameCount() >= mm) {
            blitElement(dst, face.frame(mm - 1), 240, 130, false);
        }
        return;
    }
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    app.text().drawText(dst, t, 200, 130, 5);  // [RE 0x44FABC]
}

bool itemLineHandler(const SDL_Event* event, void* user) {
    auto& ctx = *static_cast<ItemLineCtx*>(user);
    if (!event) {
        ctx.startMs = SDL_GetTicks();
        drawItemLine(ctx);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (SDL_GetTicks() - ctx.startMs >= 1000) {  // [RE 0x44EF41] sub_4544F6(1000)
            ctx.app->events().requestExit(0);
        } else {
            drawItemLine(ctx);
        }
        return true;
    }
    return true;  // 纯延时，期间不响应输入
}

}  // namespace

// [RE 0x44EF41] sub_44EF41：角色气泡台词（道具/卡片效果入口调用）
// 依据: 0x44EF41 反编译；tip 框 data.mkf[517] 帧 5 @(220,130) + 头像 pieceSprites[p] 帧 1
//   @(170,130) + 文本 @(200,130) 或 '@MM' 表情 data.mkf[519] 帧 MM-1 @(240,130)；
//   时长 1000ms；同文本指针去重（dword_4762C8）；状态中（stateFlags BYTE1/state37/byte54）不显示
void playItemLine(Application& app, int player, int itemId) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || itemId < 1 || itemId > 13) {
        return;
    }
    const Player& pl = st.players[player];
    if (((pl.stateFlags >> 8) & 0xFF) != 0 || pl.state37 != 0 || pl.byte54 != 0) {
        return;
    }
    const int ci = pl.charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const char* line = kItemLines[ci][itemId - 1];
    if (line == nullptr || line[0] == '\0') {
        return;
    }
    static const char* s_last = nullptr;  // [RE 0x4762C8] 去重
    if (line == s_last) {
        return;
    }
    s_last = line;
    // [RE 0x45441A drawTextSpeak] '#NNNN' 语音编号 → Speaking.mkf
    if (line[0] == '#') {
        int vid = 0;
        bool ok = true;
        for (int i = 1; i <= 4; ++i) {
            if (line[i] < '0' || line[i] > '9') {
                ok = false;
                break;
            }
            vid = vid * 10 + (line[i] - '0');
        }
        if (ok) {
            app.audio().playVoice(vid);
        }
    }
    ItemLineCtx ctx;
    ctx.app = &app;
    ctx.line = line;
    ctx.player = player;
    RICH4_LOGI("playItemLine: p%d item%d %s (RE 0x44EF41)", player, itemId, line);
    runModal(app, &itemLineHandler, &ctx, 16);
}

// [RE 0x446E4A] 機車（id5）：travel=1 / 骰子数 2；已是机车 → 0；原汽车 → 归还一辆汽车
// 依据: 0x446E4A 反编译；消耗 = itemStock[15p+4]--（不还礼物池，池归还见 confiscateItems）
int useItemFoot(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    Player& pl = st.players[p];
    if (pl.travel == 1) {
        return 0;  // 已是机车
    }
    if (pl.travel == 2) {
        ++st.itemStock[15 * p + 5];  // [RE 0x446E7F] 汽车归还（slot5 = id6）
    }
    pl.travel = 1;
    pl.diceCount = 2;
    loadWalkResources(st, p);   // [RE 0x40B93B]
    renderGameFrame(app);       // [RE 0x41D476] refreshGameUi(0,0,1)
    playItemLine(app, p, 5);    // [RE 0x44EF41] off_480D6A
    --st.itemStock[15 * p + 4]; // [RE 0x446EF9] 消耗机车
    RICH4_LOGI("itemFoot: p%d travel=1 dice=2 (RE 0x446E4A)", p);
    return 1;
}

// [RE 0x446F05] 汽車（id6）：travel=2 / 骰子数 3；已是汽车 → 0；原机车 → 归还一辆机车
// 依据: 0x446F05 反编译（尾部共享 0x446EFF 返回 1）
int useItemCar(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    Player& pl = st.players[p];
    if (pl.travel == 2) {
        return 0;  // 已是汽车
    }
    if (pl.travel == 1) {
        ++st.itemStock[15 * p + 4];  // [RE 0x446F37] 机车归还（slot4 = id5）
    }
    pl.travel = 2;
    pl.diceCount = 3;
    loadWalkResources(st, p);   // [RE 0x40B93B]
    renderGameFrame(app);       // [RE 0x41D476] refreshGameUi(0,0,1)
    playItemLine(app, p, 6);    // [RE 0x44EF41] off_480D6E
    --st.itemStock[15 * p + 5]; // [RE 0x446FB1] 消耗汽车
    RICH4_LOGI("itemCar: p%d travel=2 dice=3 (RE 0x446F05)", p);
    return 1;
}

// [RE 0x4479D2] 工程車（id12）：travel=31（低2位=3 临时载具计时）/ 骰子数 1；
//   已是工程车 → 0；原机车/汽车归还库存；到期（sub_41C84F 每回合 -4）恢复原载具或步行
// 依据: 0x4479D2 反编译；原载具/骰子暂存 kind(+100)/byte_496BCD(+101)（重写独立字段）
int useItemBulldozer(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    Player& pl = st.players[p];
    if ((pl.travel & 3) == 3) {
        return 0;  // 已是工程车（travel=31 → &3==3）
    }
    if (pl.travel == 1) {
        ++st.itemStock[15 * p + 4];  // [RE 0x4479F8] 机车归还
    }
    if (pl.travel == 2) {
        ++st.itemStock[15 * p + 5];  // [RE 0x447A24] 汽车归还
    }
    pl.vehicleRestore = pl.travel;   // [RE 0x447A49] 原版写 kind(+100)
    pl.diceRestore = pl.diceCount;   // [RE 0x447A55] byte_496BCD
    pl.travel = 31;
    pl.diceCount = 1;
    loadWalkResources(st, p);   // [RE 0x40B93B]
    renderGameFrame(app);       // [RE 0x41D476] refreshGameUi(0,0,1)
    playItemLine(app, p, 12);   // [RE 0x44EF41] off_480D86
    --st.itemStock[15 * p + 11]; // [RE 0x447AB1] 消耗工程车（slot11 = id12）
    RICH4_LOGI("itemBulldozer: p%d travel=31 dice=1 (RE 0x4479D2)", p);
    return 1;
}

// [RE 0x475DD4] g_itemEffectFuncs[id] 分派（id = 道具 1..13）
int itemEffect(Application& app, int id) {
    switch (id) {
        case 5: return useItemFoot(app);
        case 6: return useItemCar(app);
        case 12: return useItemBulldozer(app);
        default:
            RICH4_LOGI("item effect id=%d (%s) not implemented (P4, RE 0x475DD4 + 4*id)", id,
                       (id >= 1 && id <= 13) ? kItemBagNames[id] : "?");
            return 0;  // 等价「不可用 → 重选」，不消耗
    }
}

} // namespace rich4
