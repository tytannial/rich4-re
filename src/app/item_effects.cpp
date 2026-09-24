#include "game/app/item_effects.h"

#include <SDL3/SDL.h>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/item_lines.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/target_select_dialog.h"
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

// [RE 0x446AFB] 機器娃娃（id1）：扣道具+台词 → 虚拟槽 8 复制玩家状态（位置/格/资源）→
//   currentPlayer=8 → startPlayerMove（槽 8 走 9 步，沿路由 onPlayerActionPhase 弹飞踢除
//   物件）→ 走完 nextPlayer 恢复原玩家继续回合
int useItemWorker(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4 || st.players[p].alive != 1) {
        return 0;
    }
    takePlayerItem(st, p, 1);  // [RE 0x446B05]
    playItemLine(app, p, 1);   // [RE 0x44EF41] off_480D5A
    const Player& src = st.players[p];
    st.workerPlayer = static_cast<uint8_t>(p);  // [RE 0x499070] byte_498E70
    st.workerFlag = 0;                          // [RE 0x498E72]
    st.workerFromCell = src.cellEntId;          // [RE word_498E6C]
    st.workerPrevCell = src.prevCellEnt;        // [RE word_498E6E]
    Player& w = st.players[8];
    w = src;              // 虚拟槽 = 玩家状态（坐标/格/朝向/角色资源）
    w.travel = 0;
    w.diceCount = 1;
    w.alive = 0x80;       // 娃娃标记（非人类/AI 位；渲染/移动用）
    w.cellNo = 0;
    w.cellTableIdx = 0;
    w.stateFlags = 0;
    w.byte54 = 0;
    st.currentPlayer = 8;  // [RE 0x446B94]
    loadWalkResources(st, 8);
    startPlayerMove(app);  // [RE 0x40DD1F] steps=9 state=1
    RICH4_LOGI("itemWorker: p%d -> slot8 from cell %u (RE 0x446AFB)", p, st.workerFromCell);
    return 1;
}

// ===== 放置类公共（路障 0x446BAA / 地雷 0x446C88 / 定時炸彈 0x446D69）=====
// 流程: 台词 → selectTargetDialog(mode) → createMapObject(type, cellEnt) →
//       flyObjectSprite(玩家→目标格, 100) → 音效 → refreshGameUi → itemStock-1
// 返回目标格 id（0 = 取消，不消耗）
int placeMapItem(Application& app, int itemId, int objType, int mode, int sfxSlot) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    playItemLine(app, p, itemId);  // [RE 0x44EF41]
    int v = 0;
    if (st.players[p].alive == 1) {
        v = selectTargetDialog(app, mode);  // [RE 0x446AE8]
    } else {
        v = 0;  // [RE 0x420EEE] AI 预选目标（dword_48BE64[0]；P5）
    }
    if (v == 0) {
        return 0;
    }
    const Player& pl = st.players[p];
    const int obj = createMapObject(st, objType, v, 0, 0);  // [RE 0x40E033]
    if (obj != 0 && v < static_cast<int>(st.cellEnts.size())) {
        // [RE 0x40E669] 从玩家精灵位置飞向目标格世界坐标，100 帧
        flyObjectSprite(app, obj, pl.spriteX, pl.spriteY, st.cellEnts[v].x, st.cellEnts[v].y, 100);
    }
    app.audio().playEffectSlot(sfxSlot);  // [RE 0x4542CE]
    renderGameFrame(app);                 // [RE 0x41D546]
    --st.itemStock[15 * p + itemId - 1];
    RICH4_LOGI("place item %d: p%d obj %d cell %d (RE 0x446BAA/C88/D69)", itemId, p, obj, v);
    return v;
}

// [RE 0x447295] 機器工人（id9）：选目标（住宅/商業 0x2090006 光标9）→ 扣道具 →
//   angelUpgrade 免费加盖 → FLC 553（switchFrame=44 视口锁定重绘时建筑已加盖）；
//   封顶（bit7）再播 FLC 523 施工（playGodBuildFlc）→ refreshGameUi
int useItemRobotWorker(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    playItemLine(app, p, 9);  // [RE 0x44EF41] off_480D7A
    int v = 0;
    if (st.players[p].alive == 1) {
        v = selectTargetDialog(app, 0x2090006);  // [RE 0x446AE8] flags=2|4、光标类型 9
    } else {
        v = 0;  // [RE 0x420EEE] AI 预选（P5）
    }
    if (v == 0) {
        return 0;
    }
    takePlayerItem(st, p, 9);  // [RE 0x445AA2] takePlayerCard(cur, 9)
    int x = 0;
    int y = 0;
    getObjectPosition(st, v, x, y);  // [RE 0x40AF12]
    // [RE 0x41D476] refreshGameUi(x, y, 0)：**视口中心对准目标地块**——施工动画/加盖结果
    //   在目标处显示；商业用地无设施时 angelUpgrade 内部弹出设施选择面板也在对准后的视口上，
    //   选完面板关闭才开始 FLC 553 施工
    st.manualView = true;
    st.viewSmoothX = x;
    st.viewSmoothY = y;
    st.viewScrolling = false;
    renderGameFrame(app);
    const int flags = angelUpgrade(app, v);          // [RE 0x40B110]
    // 商业用地无设施时 angelUpgrade 内弹出设施选择面板；面板关闭后 surface 仍残留面板画面
    //   （FLC 553 为保帧模式，会快照当前 surface 作背景）→ 先重绘干净场景，避免动画叠着面板
    renderGameFrame(app);
    app.renderFrame();
    playEventFlc(app, 553, 0, 40, 91, false, 44);    // [RE 0x45144F flags 0x2C0001 音效 91]
    if ((flags & 0x80) != 0) {
        playEventFlc(app, 523, 0, 40, 90, false, 0); // [RE 0x40B0CD] playGodBuildFlc 施工动画
    }
    st.manualView = false;  // [RE 0x41D546] 视口复位（跟随当前玩家）
    renderGameFrame(app);
    RICH4_LOGI("robot worker: p%d target %d flags %d (RE 0x447295)", p, v, flags);
    return v;
}

// [RE 0x446FBC / 0x447ACE] 飞弹/核弹公共：选目标（0x300C0/0x400C0 含滚动）→ 扣道具 →
//   取目标坐标 → 视口对准目标 → expireAssets（100/38/0 降级 或 -1/38/1 归公）→
//   FLC（528 音效81 / 530 音效83，switchFrame=9）→ 范围内受伤玩家（alive&0x40）
//   向发射者记账 90×M + 住院 3 → 面板复位
int launchMissile(Application& app, int itemId, int mode, int radius, int dump, int flcRes,
                  int soundId) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    playItemLine(app, p, itemId);  // [RE 0x44EF41]
    int v = 0;
    if (st.players[p].alive == 1) {
        v = selectTargetDialog(app, mode);  // [RE 0x446AE8]
    } else {
        v = 0;  // [RE 0x420EEE] AI 预选（P5）
    }
    if (v == 0) {
        return 0;
    }
    takePlayerItem(st, p, itemId);  // [RE 0x445AA2] takePlayerCard(cur, id)
    int x = 0;
    int y = 0;
    getObjectPosition(st, v, x, y);  // [RE 0x40AF12]
    // [RE 0x41D476] refreshGameUi(x, y, 0)：视口中心对准目标（expireAssets 以视野中心取范围）
    st.manualView = true;
    st.viewSmoothX = x;
    st.viewSmoothY = y;
    st.viewScrolling = false;
    renderGameFrame(app);
    expireAssets(app, x, y, radius, 38, dump, p);  // [RE 0x40AC7B]
    playEventFlc(app, flcRes, 0, 40, soundId, false, 9);  // [RE 0x45144F flags BYTE2=9]
    for (int i = 0; i < st.playerCount && i < 9; ++i) {
        if ((st.players[i].alive & 0x40) != 0) {  // 范围内受伤（damagePlayer 置位）
            addPlayerDebt(st, i, p, 90 * st.moneyMul);  // [RE 0x4470D4]
            hospitalizePlayer(app, i, 3);               // [RE 0x4470DF]
        }
    }
    st.manualView = false;  // [RE 0x41D546]
    renderGameFrame(app);
    RICH4_LOGI("missile item %d: p%d target %d (%d,%d) (RE 0x446FBC/0x447ACE)", itemId, p, v, x,
               y);
    return v;
}

// [RE 0x447C00] 下車（道具栏右下角按钮，id 14）：载具回包（机车→slot4 / 汽车→slot5）+
//   恢复步行（travel=0/dice=1）+ 重载行走资源 + 重绘；道具栏按钮绘制见 item_bag_dialog.cpp
int useItemGetOff(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    Player& pl = st.players[p];
    if (pl.travel == 1) {
        ++st.itemStock[15 * p + 4];  // [RE 0x447C1C] 机车回包
    } else if (pl.travel == 2) {
        ++st.itemStock[15 * p + 5];  // [RE 0x447C2A] 汽车回包
    }
    pl.travel = 0;
    pl.diceCount = 1;
    loadWalkResources(st, p);   // [RE 0x40B93B]
    renderGameFrame(app);       // [RE 0x41D476] refreshGameUi(0,0,1)
    RICH4_LOGI("itemGetOff: p%d back to walk (RE 0x447C00)", p);
    return 1;
}

// [RE 0x447387] 時光機（id10）：全状态回滚到本回合开始快照（0x448544）
//   成功 → 音乐重置（g_musicTimer 处理，重写简化跳过）+ 扣道具 + 地图/小地图重建 + 全屏重绘
int useItemTimeMachine(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    playItemLine(app, p, 10);  // [RE 0x44EF41] off_480D7E（列9）
    const bool ok = restoreTurnSnapshot(app, p);  // [RE 0x448544]
    if (ok) {
        // [RE 0x4473BD..0x4473F6] 音乐 g_musicTimer 处理（停/重播 track0）——重写简化跳过
        takePlayerItem(st, p, 10);  // [RE 0x445AA2]
        renderGameFrame(app);       // [RE 0x41906A(1)] 全屏重绘
        RICH4_LOGI("time machine: p%d rolled back (RE 0x447387)", p);
    }
    return ok ? 1 : 0;
}

// [RE 0x447428] 傳送機（id11）：选目标（人物/神明/房屋/物品 0x1200036）→ 按类型选目的地
//   （住宅 0x2090002 / 商业 0x2090004 / 空格 0x2090001）→ 搬迁；物件目标换算成 owner 玩家
// 依据: 0x447428 反编译；AI 分支 = 0x8000|(1<<cur)；玩家传送后 dir=最优出口朝向、
//   prevCellEnt=另一出口、state=1 触发落地结算
int useItemTeleport(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    playItemLine(app, p, 11);  // [RE 0x44EF41] off_480D82（列10）
    int v40 = 0;
    if (st.players[p].alive == 1) {
        v40 = selectTargetDialog(app, 0x1200036);  // [RE 0x446AE8] flags 2|4|0x10|0x20
    } else {
        v40 = 0x8000 | (1 << p);  // [RE 0x420EEE] AI：自己
    }
    if (v40 == 0) {
        return 0;
    }
    // 物件槽目标（0x8000|BYTE1=槽）→ 换算成 owner 玩家
    if ((v40 & 0x8000) != 0 && (v40 & 0x3F00) != 0) {
        const int slot = (v40 & 0x3F00) >> 8;
        if (slot >= 1 && slot <= 46) {
            const uint8_t owner = st.cellTable[24 * (slot - 1) + 5];  // g_cellOwner = cellTable+5
            if (owner != 0) {
                v40 = 0x8000 | (1 << (owner - 1));
            }
        }
    }
    bool done = false;
    if (v40 > 2000 && v40 < 4000) {
        // ---- 住宅用地搬迁 ----
        const int dest = selectTargetDialog(app, 0x2090002);  // [RE 0x446AE8]
        if (dest == 0) {
            return 0;
        }
        const int si = v40 - 2000;
        const int di = dest - 2000;
        if (si > 0 && si < static_cast<int>(st.estates.size()) && di > 0 &&
            di < static_cast<int>(st.estates.size())) {
            Estate& src = st.estates[si];
            Estate& dst = st.estates[di];
            dst.owner = src.owner;
            src.owner = 0;
            dst.level = src.level;
            src.level = 0;
            dst.type = src.type;
            src.type = 0;
            dst.expireDate = src.expireDate;
            src.expireDate = 0;
            src.price = 0;  // [RE +44] 本次租金清零
            buildMiniMapMarks(app);  // [RE 0x40AD80]
            done = true;
            RICH4_LOGI("teleport estate %d -> %d (RE 0x447428)", si, di);
        }
    } else if (v40 > 4000 && v40 < 6000) {
        // ---- 商業用地搬迁 ----
        const int dest = selectTargetDialog(app, 0x2090004);
        if (dest == 0) {
            return 0;
        }
        const int si = v40 - 4000;
        const int di = dest - 4000;
        if (si > 0 && si < static_cast<int>(st.corps.size()) && di > 0 &&
            di < static_cast<int>(st.corps.size())) {
            Corp& src = st.corps[si];
            Corp& dst = st.corps[di];
            dst.owner = src.owner;
            src.owner = 0;
            dst.sub = src.sub;
            src.sub = 0;
            dst.type = src.type;
            src.type = 0;
            dst.expireDate = src.expireDate;
            src.expireDate = 0;
            src.lastFee = 0;
            buildMiniMapMarks(app);
            done = true;
            RICH4_LOGI("teleport corp %d -> %d (RE 0x447428)", si, di);
        }
    } else if ((v40 & 0x8000) != 0 && (v40 & 0xFF) != 0) {
        // ---- 玩家传送（含 AI 自己）----
        const int pi = bitScanPlayer(static_cast<uint32_t>(v40) & 0xFF);  // [RE 0x40D293]
        if (pi < 0 || pi >= 4) {
            return 0;  // 事件槽 4..7（P5）
        }
        int dest = 0;
        if (st.players[p].alive == 1) {
            dest = selectTargetDialog(app, 0x2090001);
        } else {
            dest = 0;  // [RE 0x420EEE] AI 预选（P5）
        }
        if (dest == 0 || dest >= static_cast<int>(st.cellEnts.size())) {
            return 0;
        }
        // [RE 0x4474D1 段] 目的地可用出口（非 bit30-33 占用）：选与当前朝向最接近者
        uint16_t exits[4] = {};
        int ne = 0;
        uint32_t bit = 0x40000000u;
        for (int k = 0; k < 4; ++k) {
            const uint16_t exitId = st.cellEnts[dest].exits[k];
            if (exitId != 0 && (st.cellEnts[dest].occMask & bit) == 0) {
                exits[ne++] = exitId;
            }
            bit >>= 1;
        }
        const int curDir = st.players[pi].dir;
        int bestIdx = -1;
        int bestDelta = 8;
        int bestDir = 0;
        for (int j = 0; j < ne; ++j) {
            const int d = facingBetween(st, dest, exits[j]);  // sub_407A8C
            int delta = std::abs(curDir - d);
            if (delta > 4) {
                const int lo = (curDir < d ? curDir : d) + 8;
                const int hi = (curDir <= d ? d : curDir);
                delta = lo - hi;
            }
            if (delta < bestDelta) {
                bestDelta = delta;
                bestIdx = j;
                bestDir = d;
            }
        }
        uint16_t prevExit = 0;
        if (ne <= 1) {
            prevExit = (bestIdx >= 0) ? exits[bestIdx] : 0;
        } else {
            for (int j = 0; j < ne; ++j) {
                if (j != bestIdx) {
                    prevExit = exits[j];
                    break;
                }
            }
        }
        Player& pl = st.players[pi];
        const uint32_t mask = 256u << pi;
        if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
            st.cellEnts[pl.cellEntId].occMask &= ~mask;
        }
        pl.cellEntId = static_cast<uint16_t>(dest);
        pl.prevCellEnt = prevExit;
        pl.dir = static_cast<uint8_t>(bestDir);
        pl.spriteX = st.cellEnts[dest].x;
        pl.spriteY = st.cellEnts[dest].y;
        updatePlayerCarriedObjects(st, pi);  // [RE 0x40FC00]
        if (pl.alive != 0) {
            st.cellEnts[dest].occMask |= mask;
        }
        loadWalkResources(st, pi);  // [RE 0x40B93B]
        if (pi == p) {
            // [RE 0x44759D] 自己传送：state=1（steps=0 → 立即落地结算）
            st.remainingSteps = 0;
            st.playerActionState[pi] = 1;
            st.gameStateActive = true;
        }
        done = true;
        RICH4_LOGI("teleport player %d -> cell %d dir %d prev %u (RE 0x447428)", pi, dest,
                   bestDir, prevExit);
    } else if ((v40 & 0x8000) != 0 && (v40 & 0x3F00) != 0) {
        // ---- 挂身物件/神明传送 ----
        const int dest = selectTargetDialog(app, 0x2090001);
        if (dest == 0 || dest >= static_cast<int>(st.cellEnts.size())) {
            return 0;
        }
        const int slot = (v40 & 0x3F00) >> 8;
        if (slot >= 1 && slot <= 46) {
            const size_t off = static_cast<size_t>(24 * (slot - 1));
            const uint16_t oldCell = st.cellTable[off + 2] | (st.cellTable[off + 3] << 8);
            if (oldCell > 0 && oldCell < st.cellEnts.size()) {
                st.cellEnts[oldCell].occMask &= ~(static_cast<uint32_t>(slot) << 16);
            }
            st.cellTable[off + 2] = static_cast<uint8_t>(dest & 0xFF);
            st.cellTable[off + 3] = static_cast<uint8_t>((dest >> 8) & 0xFF);
            st.cellEnts[dest].occMask |= static_cast<uint32_t>(slot) << 16;
            uint16_t exitId = 0;
            for (int k = 0; k < 4; ++k) {
                if (st.cellEnts[dest].exits[k] != 0) {
                    exitId = st.cellEnts[dest].exits[k];
                    break;
                }
            }
            st.cellTable[off + 1] = static_cast<uint8_t>(facingBetween(st, exitId, dest));
            done = true;
            RICH4_LOGI("teleport object slot %d -> cell %d (RE 0x447428)", slot, dest);
        }
    }
    if (done) {
        renderGameFrame(app);         // [RE 0x41D476] refreshGameUi(0,0,1)
        takePlayerItem(st, p, 11);    // [RE 0x445AA2]
        return 1;
    }
    return 0;
}

// ===== [RE 0x446774] 遥控骰子选择模态（panel.mkf[72]：帧0 面板 + 帧 1..6 骰子高亮）=====
namespace {

struct DicePickCtx {
    Application* app = nullptr;
    const UiImage* sheet = nullptr;
    int hover = 0;  // 1..6，0 = 无
};

void drawDicePick(DicePickCtx& c) {
    Application& app = *c.app;
    Surface& dst = app.surface();
    renderGameFrame(app);
    const UiImage& s = *c.sheet;
    // [RE 0x447183] blitElementFullscreen = **不透明** blit（0x4563F5；面板底图含 0 像素时
    //   透明 blit 会露出地图 = 花屏）
    if (s.frameCount() > 0) {
        blitElementOpaque(dst, s.frame(0), 92, 300);
    }
    if (c.hover >= 1 && c.hover <= 6 &&
        s.frameCount() > static_cast<size_t>(c.hover)) {
        // [RE 0x4469xx] 悬停：blitElementFullscreen(帧 hover) @(104+40*(hover-1), 314)
        blitElementOpaque(dst, s.frame(c.hover), 104 + 40 * (c.hover - 1), 314);
    }
}

int diceHit(int x, int y) {
    if (y < 314 || y >= 343) {
        return 0;
    }
    for (int i = 0; i < 6; ++i) {
        const int l = 104 + 40 * i;
        if (x >= l && x < l + 30) {
            return i + 1;
        }
    }
    return 0;
}

bool dicePickHandler(const SDL_Event* event, void* user) {
    auto& c = *static_cast<DicePickCtx*>(user);
    if (!event) {
        drawDicePick(c);
        return true;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const int h = diceHit(static_cast<int>(event->motion.x),
                                  static_cast<int>(event->motion.y));
            if (h != c.hover) {
                c.hover = h;
                drawDicePick(c);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button == SDL_BUTTON_LEFT) {
                if (c.hover != 0) {  // [RE 0x446bxx] LBUTTONUP: dword_48C598 非 0 → postModalExit(i+1)
                    c.app->audio().playEffect(1);  // [RE g_uiSoundClick]
                    c.app->events().requestExit(c.hover);
                }
            } else if (event->button.button == SDL_BUTTON_RIGHT ||
                       event->button.button == SDL_BUTTON_MIDDLE) {
                c.app->audio().playEffect(4);  // [RE g_uiSoundCancel]
                c.app->events().requestExit(0);
            }
            return true;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (event->key.key == SDLK_ESCAPE) {
                c.app->events().requestExit(0);
            }
            return true;
        }
        default:
            return true;
    }
}

// [RE 0x4470F8 面板段] 六骰选择：返回 1..6，0 = 取消
int remoteDiceDialog(Application& app) {
    GameState& st = app.gameState();
    UiImage sheet;
    if (auto blob = st.panel.read(72)) {  // [RE 0x447113] sub_450441(panel, 72)
        sheet.load(std::move(*blob));
    }
    if (sheet.frameCount() < 7) {
        RICH4_LOGW("remoteDiceDialog: panel.mkf[72] unavailable (RE 0x446774)");
        return 0;
    }
    DicePickCtx c;
    c.app = &app;
    c.sheet = &sheet;
    return runModal(app, &dicePickHandler, &c, 16);
}

}  // namespace

// [RE 0x4470F8] 遙控骰子（id8）：modal 选点 → 直接进入移动（sub_40DD1F）+ 消耗 + byte_475DD8 强制点数
int useItemRemoteDice(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    int dice = 0;
    if (st.players[p].alive == 1) {
        playItemLine(app, p, 8);   // [RE 0x44EF41] off_480D76（面板 blit 之前）
        dice = remoteDiceDialog(app);
    } else {
        dice = 0;  // [RE 0x420EEE] dword_48BE64[0]（AI 预选目标；P5 接入）
    }
    if (dice != 0) {
        startPlayerMove(app);          // [RE 0x40DD1F]（先于 takePlayerCard）
        takePlayerItem(st, p, 8);      // [RE 0x445AA2]（id≤8 归还礼物池）
        st.forcedDice = static_cast<uint8_t>(dice);  // [RE 0x475DD8]
        RICH4_LOGI("remote dice: p%d value=%d (RE 0x4470F8)", p, dice);
    }
    return dice;
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
        case 1: return useItemWorker(app);
        case 2: return placeMapItem(app, 2, 16, 0x1, 4);       // 路障（dword_48236A=槽4）
        case 3: return placeMapItem(app, 3, 17, 0x10001, 5);   // 地雷（dword_482372=槽5）
        case 4: return placeMapItem(app, 4, 18, 0x20001, 2);   // 定時炸彈（g_effectSlot2=槽2）
        case 5: return useItemFoot(app);
        case 6: return useItemCar(app);
        case 7: return launchMissile(app, 7, 0x300C0, 100, 0, 528, 81);  // 飛彈（降级+受伤）
        case 8: return useItemRemoteDice(app);
        case 9: return useItemRobotWorker(app);
        case 10: return useItemTimeMachine(app);
        case 11: return useItemTeleport(app);
        case 12: return useItemBulldozer(app);
        case 13: return launchMissile(app, 13, 0x400C0, -1, 1, 530, 83);  // 核子飛彈（全图归公）
        case 14: return useItemGetOff(app);  // 下車按鈕（道具栏，0x447C00）
        default:
            RICH4_LOGI("item effect id=%d (%s) not implemented (P4, RE 0x475DD4 + 4*id)", id,
                       (id >= 1 && id <= 13) ? kItemBagNames[id] : "?");
            return 0;  // 等价「不可用 → 重选」，不消耗
    }
}

} // namespace rich4
