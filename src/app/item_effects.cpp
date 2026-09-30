#include "game/app/item_effects.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/item_lines.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/target_select_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/app/debug/debug.h"
#include <cstdio>
#include "game/core/trace.h"
#include "game/core/clock.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"
#include "game/resource/mkf.h"
#include "game/core/rng.h"

namespace rich4 {
namespace {

// ===== [RE 0x44EF41] 角色气泡台词 =====
struct ItemLineCtx {
    Application* app = nullptr;
    const char* line = nullptr;
    int player = 0;
    int expr = 0;  // [RE 0x44EF41 a2] 头像表情帧索引（画帧 expr+1）
    uint64_t startMs = 0;
};

void drawItemLine(ItemLineCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    renderGameFrame(app);
    const UiImage& tip = st.estateTiles;  // [RE 0x48BAD8] g_tipFrame = data.mkf[517]
    if (tip.frameCount() > 6) {
        blitElement(dst, tip.frame(6), 220, 130, false);  // [RE 0x44F033] g_tipFrame+84 = 帧6 云朵气泡
    }
    // 说话头像 = pieceSprites[p] 帧 expr+1（expr=0 → 帧 1；表情 哭/被炸/笑 = 帧 2/3/4）[RE 0x44EF41]
    if (ctx.player >= 0 && ctx.player < 9 && st.pieceSprites[ctx.player].frameCount() > 1) {
        const UiImage& pc = st.pieceSprites[ctx.player];
        size_t f = static_cast<size_t>(ctx.expr) + 1;
        if (f >= static_cast<size_t>(pc.frameCount())) {
            f = 1;  // 表情帧缺失时回退普通帧
        }
        blitElement(dst, pc.frame(f), 170, 130, false);
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
    app.text().setFont(16, 0x101010, 0, kTextStyleBold, 1);  // [RE 0x44EFD2] setTextFont(16,0x101010,0,2,1) 黑字
    app.text().drawText(dst, t, 200, 130, 5);  // [RE 0x44FABC]
}

bool itemLineHandler(const SDL_Event* event, void* user) {
    auto& ctx = *static_cast<ItemLineCtx*>(user);
    if (!event) {
        ctx.startMs = nowMs();
        drawItemLine(ctx);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        if (nowMs() - ctx.startMs >= 1000) {  // [RE 0x44EF41] sub_4544F6(1000)
            ctx.app->events().requestExit(0);
        } else {
            drawItemLine(ctx);
        }
        return true;
    }
    // [RE 0x4544F6/0x4528B9] 原版台词延时消息泵检测 WM_LBUTTONUP(514)/WM_MBUTTONDOWN(517)/
    //   WM_KEYUP(257) → 提前结束并 voiceStop（sub_454493），后续流程照常；
    //   迁移: 左键按下/Esc/Enter/Space → 停语音 + 退出（消息被栈顶消费不穿透）
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        trace::logf("line skip (RE 0x4544F6)");
        ctx.app->audio().stopVoice();
        ctx.app->events().requestExit(0);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN &&
        (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_RETURN ||
         event->key.key == SDLK_SPACE)) {
        trace::logf("line skip key (RE 0x4544F6)");
        ctx.app->audio().stopVoice();
        ctx.app->events().requestExit(0);
        return true;
    }
    return true;  // 其余输入期间不响应（原版 PeekMessage 只取不派发）
}

}  // namespace

// [RE 0x44EF41] playLine：通用角色气泡台词（原 sub_44EF41 本体）
// 依据: 0x44EF41 反编译；tip 框 data.mkf[517] 帧 6（g_tipFrame+84，271×199 红边云朵气泡）@(220,130)
//   + 头像 pieceSprites[p] 帧 exprFrame+1（a2 = 表情帧索引）
//   @(170,130) + 文本 @(200,130) 或 '@MM' 表情 data.mkf[519] 帧 MM-1 @(240,130)；
//   时长 1000ms；同文本指针去重（dword_4762C8）；状态中（stateFlags BYTE1/state37/byte54）不显示
void playLine(Application& app, int player, const char* line, int exprFrame) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || line == nullptr || line[0] == '\0') {
        return;
    }
    const Player& pl = st.players[player];
    if (((pl.stateFlags >> 8) & 0xFF) != 0 || pl.state37 != 0 || pl.byte54 != 0) {
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
    ctx.expr = exprFrame;
    RICH4_LOGI("playLine: p%d expr%d %s (RE 0x44EF41)", player, exprFrame, line);
    trace::logf("line p=%d expr=%d text=\"%s\"", player, exprFrame, line);
    // [RE 0x44EFBD] refreshGameUi(g_playerSpriteX[a1], g_playerSpriteY[a1], 0)：
    //   视口对准说话玩家（说话者==当前玩家 → 复位跟随；否则手动视口）。
    //   原版 flags=0 不重绘，气泡首帧 renderGameFrame 即按新视口呈现；模态结束还原。
    const bool savedMv = st.manualView;
    const int savedSx = st.viewSmoothX;
    const int savedSy = st.viewSmoothY;
    const bool savedSc = st.viewScrolling;
    if (player == st.currentPlayer) {
        st.manualView = false;
    } else {
        st.manualView = true;
        st.viewSmoothX = st.players[player].spriteX;
        st.viewSmoothY = st.players[player].spriteY;
        st.viewScrolling = false;
    }
    runModal(app, &itemLineHandler, &ctx, 16);
    st.manualView = savedMv;
    st.viewSmoothX = savedSx;
    st.viewSmoothY = savedSy;
    st.viewScrolling = savedSc;
}

// [RE 0x44EF41] playItemLine：道具/卡片效果入口台词（查 kItemLines 表）
void playItemLine(Application& app, int player, int itemId, int exprFrame) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || itemId < 1 || itemId > 13) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    playLine(app, player, kItemLines[ci][itemId - 1], exprFrame);
}

// [RE 0x44F230] playValueLine：卡片/道具价值台词（12 角色 x 3 档）
// 依据: 0x44F230 反编译; price<=50 → off_480852（列 2）；50<price<=100 → off_48084A+4*(rand&1)
//   （列 0/1 随机）；price>100 → 列 0；随后 sub_44EF41(p, 0, 台词)
void playValueLine(Application& app, int player, int price) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const char* line;
    if (price > 100) {
        line = kValueLines[ci][0];
    } else if (price > 50) {
        line = (rng::next() & 1) ? kValueLines[ci][1] : kValueLines[ci][0];
    } else {
        line = kValueLines[ci][2];
    }
    playLine(app, player, line);
}

// [RE 0x44F354] playCollectorLine：收款方台词（金额档；>=9000M / 5000..9000M 随机 / >=2000M）
void playCollectorLine(Application& app, int player, int32_t amount) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const int32_t m = st.moneyMul;
    const char* line = nullptr;
    if (amount >= 9000 * m) {
        line = kMoneyLines[ci][0];
    } else if (amount >= 5000 * m) {
        line = (rng::next() & 1) ? kMoneyLines[ci][1] : kMoneyLines[ci][0];
    } else if (amount >= 2000 * m) {
        line = kMoneyLines[ci][2];
    }
    if (line != nullptr) {
        playLine(app, player, line, 3);  // [RE 0x44EF41] expr 3（收款笑）
    }
}

// [RE 0x44F42D] playPayerLine：付款方台词（金额档；>=9000M / 5000..9000M 随机 / >0）
void playPayerLine(Application& app, int player, int32_t amount) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const int32_t m = st.moneyMul;
    const char* line = nullptr;
    if (amount >= 9000 * m) {
        line = kMoneyLines[ci][3];
    } else if (amount >= 5000 * m) {
        line = (rng::next() & 1) ? kMoneyLines[ci][4] : kMoneyLines[ci][3];
    } else if (amount > 0) {
        line = kMoneyLines[ci][5];
    }
    if (line != nullptr) {
        playLine(app, player, line, 2);  // [RE 0x44EF41] expr 2（付款哭）
    }
}

// [RE 0x44F4ED] playDebtorLine：欠债者面对最大债主（50%）——返回 true 时调用方不再播付款台词
bool playDebtorLine(Application& app, int player, int creditor, int32_t amount) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || creditor < 0) {
        return false;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return false;
    }
    if (findMaxCreditor(st, player) == creditor && amount >= 5000 * st.moneyMul &&
        (rng::next() & 1)) {
        playLine(app, player, kMoneyLines[ci][7], 1);  // [RE 0x44EF41] expr 1（欠债怨念）
        return true;
    }
    return false;
}

// [RE 0x44F2C2] playStatusDaysLine：状态天数台词（>6 / 4..6 随机 / 1..3）
void playStatusDaysLine(Application& app, int player, int days) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const char* line = nullptr;
    if (days > 6) {
        line = kMoneyLines[ci][18];
    } else if (days > 3) {
        line = (rng::next() & 1) ? kMoneyLines[ci][19] : kMoneyLines[ci][18];
    } else if (days > 0) {
        line = kMoneyLines[ci][20];
    }
    if (line != nullptr) {
        playLine(app, player, line, 2);  // [RE 0x44EF41] expr 2
    }
}

// [RE 0x44EF41 off_480856] playUnluckyLine：倒霉台词（expr2；列3/4 随机或列3 固定）
void playUnluckyLine(Application& app, int player, bool random) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const int col = (random && (rng::next() & 1)) ? 19 : 18;
    playLine(app, player, kMoneyLines[ci][col], 2);
}

// [RE 0x44F567] playWindfallLine：意外之财台词（大財神免付；金额档 expr3）
void playWindfallLine(Application& app, int player, int32_t amount) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return;
    }
    const int ci = st.players[player].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    const int32_t m = st.moneyMul;
    const char* line = nullptr;
    if (amount >= 9000 * m) {
        line = kMoneyLines[ci][10];
    } else if (amount >= 5000 * m) {
        line = (rng::next() & 1) ? kMoneyLines[ci][11] : kMoneyLines[ci][10];
    } else if (amount > 0) {
        line = kMoneyLines[ci][12];
    }
    if (line != nullptr) {
        playLine(app, player, line, 3);  // [RE 0x44EF41] expr 3
    }
}

// [RE 0x44F627] playEstateChainSpeech：同名地产 ≥3 块（自家）感想
void playEstateChainSpeech(Application& app, const char* name, int flag) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4 || name == nullptr) {
        return;
    }
    int count = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& es = st.estates[i];
        if (std::strcmp(name, es.name) == 0 && es.owner == static_cast<uint8_t>(p + 1)) {
            ++count;
        }
    }
    if (count < 3) {
        return;
    }
    const int ci = st.players[p].charIndex;
    if (ci < 0 || ci >= 12) {
        return;
    }
    if (flag != 0) {
        // [RE 0x44F627] 1/3 概率（升级路径）
        if ((rng::next() % 3) == 0) {
            playLine(app, p, kMoneyLines[ci][9], 0);
        }
    } else {
        playLine(app, p, kMoneyLines[ci][8], 0);  // 买地路径必播
    }
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

// [RE 0x446AFB] 機器娃娃（道具 id1；IDA 名 startDemolishWorker 有误导）：扣道具+台词 →
//   写**槽 8 记录**（0x498E68/6A=玩家精灵坐标、6C/6E=格、70=bailer=玩家、71=dir、72=busy=0）→
//   currentPlayer=8 → startPlayerMove（9 步，沿路 onPlayerActionPhase 弹飞踢除物件）→
//   走完 nextPlayer 恢复原玩家（busy 0→3）。无独立 worker 字段（旧文档/旧实现误设）。
int useItemWorker(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    // 原版 0x446AFB 无 alive 守卫：AI/托管同样用娃娃（槽8 行走踢物件）
    if (p < 0 || p >= 4) {
        return 0;
    }
    takePlayerItem(st, p, 1);  // [RE 0x446B05 takePlayerCard(cur,1)]
    playItemLine(app, p, 1);   // [RE 0x44EF41 off_480D5A 列0「替我除掉障礙物！」]
    const Player& src = st.players[p];
    // [RE 0x446B44..0x446B8E] 槽 8 记录写入（与事件槽 4..7 同构 nibble）
    NpcSlot80& slot = st.npcSlots[4];
    slot.pixelX = src.spriteX;       // word_498E68
    slot.pixelY = src.spriteY;       // word_498E6A
    slot.cell = src.cellEntId;       // word_498E6C（弹飞 fromCell）
    slot.prevCell = src.prevCellEnt; // word_498E6E（弹飞 toCell）
    slot.bailer = static_cast<uint8_t>(p); // byte_498E70（结束恢复该玩家）
    slot.dir = src.dir;              // byte_498E71
    slot.busy = 0;                   // byte_498E72（使用中；结束 0→3）
    slot.status = 0;
    slot.timerA = 0;
    slot.timerB = 0;
    slot.timerC = 0;
    slot.timerD = 0;
    // 运行时镜像 players[8]（重写移动/渲染管线载体；原版无玩家 8 结构）
    Player& w = st.players[8];
    w = src;              // 坐标/格/朝向/角色
    w.travel = 0;
    w.diceCount = 1;
    w.alive = 0x80;       // 娃娃标记（非人类/AI 位）
    w.cellNo = 0;
    w.cellTableIdx = 0;
    w.stateFlags = 0;
    w.byte54 = 0;
    st.currentPlayer = 8;  // [RE 0x446B94]
    loadWalkResources(st, 8);
    startPlayerMove(app);  // [RE 0x40DD1F] steps=9 state=1 + 循环音槽9
    RICH4_LOGI("itemWorker: p%d -> slot8 from cell %u (RE 0x446AFB)", p, slot.cell);
    return 1;
}

// ===== 放置类公共（路障 0x446BAA / 地雷 0x446C88 / 定時炸彈 0x446D69）=====
// 流程: 台词 → selectTargetDialog(mode) → createMapObject(type, cellEnt) →
//       flyObjectSprite(玩家→目标格, hold 100) → 音效 → sub_41D546（视口复位+全屏重绘）
//       → itemStock-1
// **时序**（0x446BAA 逐行）：对象先写内存但屏幕不重绘；飞行期间只有精灵叠加在旧画面上
//   （0x40E669 保存背景+只画精灵）；飞行结束 sub_41D546 全屏重绘后物件才出现在目标格
//   ——"飞过去 → 出现"（重写 2026-09-27 修正前为"先出现 → 再飞"，实机反馈）
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
        v = st.aiItemTarget;  // [RE 0x420EEE] AI 预选目标（dword_48BE64[0]；ai_item.cpp）
    }
    if (v == 0) {
        RICH4_LOGI("place item %d: p%d cancelled (alive=%u stock=%d) (RE 0x446BAA)",
                   itemId, p, st.players[p].alive, st.itemStock[15 * p + itemId - 1]);
        return 0;
    }
    const Player& pl = st.players[p];
    const int obj = createMapObject(app, objType, v, 0, 0);  // [RE 0x40E033]
    if (obj != 0 && v < static_cast<int>(st.cellEnts.size())) {
        // [RE 0x40E669] 从玩家精灵位置飞向目标格世界坐标（背景快照叠加，目标格暂不显示）
        flyObjectSprite(app, obj, pl.spriteX, pl.spriteY, st.cellEnts[v].x, st.cellEnts[v].y, 100);
    }
    app.audio().playEffectSlot(sfxSlot);  // [RE 0x4542CE]
    st.manualView = false;                // [RE 0x41D546] dword_48BE18=0（视口复位）
    renderGameFrame(app);                 // [RE 0x41D546] sub_41906A(1) 全屏重绘 → 物件出现
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
        v = st.aiItemTarget;  // [RE 0x420EEE] AI 预选（ai_item.cpp tgtRobotWorker = 对象 id）
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
    // 动画前场景快照（升级前的旧建筑 + 无残留面板）：angelUpgrade 内部会弹设施选择面板
    //   （关闭后 surface 残留面板画面）且内存已加盖——返回后恢复此快照，使 FLC 553 的保帧
    //   基准 = **旧建筑**；[RE 0x450F04] switchFrame=44 切换帧全量重绘时才显示加盖后的新建筑
    //   （原版面板自身 saveBackground/restoreBackground，调用方看到的就是旧建筑画面）
    Surface& dst = app.surface();
    std::vector<uint16_t> preUpgrade(
        dst.pixels(),
        dst.pixels() + static_cast<size_t>(Surface::kWidth) * Surface::kHeight);
    const int flags = angelUpgrade(app, v);  // [RE 0x40B110]
    std::memcpy(dst.pixels(), preUpgrade.data(),
                preUpgrade.size() * sizeof(uint16_t));
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
//   取目标坐标 → 视口对准目标（refreshGameUi(x,y,0)：经 sub_415E70→sub_40829D **重绘
//   新视口并重建 g_drawList**）→ expireAssets（100=屏幕 ±100 方块 / -1=全视野；
//   重写 mapHitRegions 屏幕空间口径）→
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
        v = st.aiItemTarget;  // [RE 0x420EEE] AI 预选（ai_item.cpp）
    }
    if (v == 0) {
        return 0;
    }
    takePlayerItem(st, p, itemId);  // [RE 0x445AA2] takePlayerCard(cur, id)
    int x = 0;
    int y = 0;
    getObjectPosition(st, v, x, y);  // [RE 0x40AF12]
    // [RE 0x41D476] refreshGameUi(x, y, 0)：设视口 + 经 sub_415E70→sub_40829D **重绘**
    //   （视口中心=目标；expireAssets 随后的屏幕空间采样以此为基准）
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
        // [RE 0x4473BD..0x4473F6] 音乐 g_musicTimer（Audio::switchDays）处理：非0 → ++，
        //   低4>高4 则 置0 + stopMusic + musicPlayTrack(0) 下一首（2026-09-29 补 1:1）
        app.audio().musicTimeMachineRestore();
        takePlayerItem(st, p, 10);  // [RE 0x445AA2]
        renderGameFrame(app);       // [RE 0x41906A(1)] 全屏重绘
        RICH4_LOGI("time machine: p%d rolled back (RE 0x447387)", p);
    }
    return ok ? 1 : 0;
}

// [RE 0x409B18 / 0x446AE8(0x2090001)] 傳送機人物/神明/物品目的地：原版 flags=1 的格 id
//   路径**无 special 检查**——occMask 干净（bit8-23：玩家/事件槽/物件）即在拾取缓冲登记
//   （`rebuildPickBuffer` cellEnt 段），地块格/医院/监狱/事件格一律可选，落地触发
//   买地/住院/入狱/事件（[HELP 94]「须移至空道路/空地」为提示文本；原版不判别地块）。
//   2026-09-27 撤销 [USER] 魔改（曾拒绝 special 2000..7999）：撤销地块格对象归一化后，
//   地块格边缘恢复返回格 id，旧检查把它们全部拦下 → 传送残留"只能移到空的道路或空地！"。
//   occMask 检查在拾取层已保证，此处保留作防御。无主空地对象 id 路径见 resolveTeleportDest。
bool isVacantRoad(const GameState& st, int cellId) {
    if (cellId <= 0 || cellId >= static_cast<int>(st.cellEnts.size())) {
        return false;
    }
    const CellEnt& ce = st.cellEnts[cellId];
    if ((ce.occMask & 0x00FFFF00u) != 0) {
        return false;  // bit8-23：玩家/事件槽/物件占用（拾取层已过滤，防御）
    }
    return true;
}

// 傳送機目的地解析（[HELP 94]「須移至空道路/空地」）：
//   <2000 = 格 id（**原版路径**：occMask 干净即可，含地块格/医院/监狱/事件格 → 落地
//   触发买地/住院/入狱/事件）；
//   [USER] 2000..6000 = 无主空地**对象 id**（住宅 owner==0&&level==0 / 商业
//   owner==0&&sub==0，selectTargetDialog allowEmptyLand 选入；原版大菱形内不可选）→
//   解析为 special == id 且占用干净的格。
//   返回格 id；0 = 拒绝。
int resolveTeleportDest(const GameState& st, int sel) {
    if (sel > 0 && sel < 2000) {
        return isVacantRoad(st, sel) ? sel : 0;
    }
    if (sel <= 2000 || sel >= 6000) {
        return 0;
    }
    if (sel < 4000) {
        const int i = sel - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        if (st.estates[i].owner != 0 || st.estates[i].level != 0) {
            return 0;
        }
    } else {
        const int i = sel - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        if (st.corps[i].owner != 0 || st.corps[i].sub != 0) {
            return 0;
        }
    }
    for (size_t c = 1; c < st.cellEnts.size(); ++c) {
        if (st.cellEnts[c].special == static_cast<uint16_t>(sel) &&
            (st.cellEnts[c].occMask & 0x00FFFF00u) == 0) {
            return static_cast<int>(c);
        }
    }
    return 0;
}

// [USER] 保留魔改（第3轮）：传送源校验——完全空的无主地（estate owner==0&&level==0 /
//   corp owner==0&&sub==0）不可作为源。原版允许选中，实为"清空目标地块"的拆迁用法
//   （源全 0 覆盖目标）；重写拒绝并提示重选，白白消耗道具的保护。无主但有加盖
//   （level/sub>0，如天使/机器工人加盖）可搬运
bool teleportSourceValid(const GameState& st, int v40) {
    if (v40 > 2000 && v40 < 4000) {
        const int i = v40 - 2000;
        if (i > 0 && i < static_cast<int>(st.estates.size())) {
            return st.estates[i].owner != 0 || st.estates[i].level != 0;
        }
        return false;
    }
    if (v40 > 4000 && v40 < 6000) {
        const int i = v40 - 4000;
        if (i > 0 && i < static_cast<int>(st.corps.size())) {
            return st.corps[i].owner != 0 || st.corps[i].sub != 0;
        }
        return false;
    }
    return true;  // 玩家/神明/物品
}

// [RE 0x447428] 傳送機（id11）：选目标（人物/神明/房屋/物品 0x1200036）→ 按类型选目的地
//   （住宅 0x2090002 / 商业 0x2090004 / 空格 0x2090001）→ 搬迁；物件目标换算成 owner 玩家
// 依据: 0x447428 反编译；AI 分支 = 0x8000|(1<<cur)；玩家传送后 dir=最优出口朝向、
//   prevCellEnt=另一出口、state=1 触发落地结算；**传送自己前保存时光机快照**
//   （0x4477C3 sub_44808A，位置更新前）；神明/物品旧格清 occMask bit16-23（0x44793B）
//   ；目的地拾取语义 = occMask bit8-23 干净（医院/监狱/事件格原版可传，落地触发事件）
// 差异（保留魔改，用户确认）: ① 源 = 完全空的无主地拒绝（teleportSourceValid）；
//   ② 房屋/设施目的地限**无主空地**；人物/神明/物品限**空道路或无主空地**（[HELP 94]
//    "空道路/空地"；原版大菱形覆盖空地 → 不可选，[USER] 2026-09-26 放开空地：
//    selectTargetDialog(allowEmptyLand) + resolveTeleportDest；有主/有建筑地块拒绝）
// 差异（待补）: AI 目的地 sub_420EEE（P5）；事件槽玩家 4..7 作为卡片/道具目标（P5，不影响四大恶人本体）
int useItemTeleport(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4) {
        return 0;
    }
    playItemLine(app, p, 11);  // [RE 0x44EF41] off_480D82（列10）
    int v40 = 0;
    if (st.players[p].alive == 1) {
        for (int guard = 0; guard < 64; ++guard) {
            v40 = selectTargetDialog(app, 0x1200036);  // [RE 0x446AE8] flags 2|4|0x10|0x20
            if (v40 == 0) {
                break;
            }
            if (teleportSourceValid(st, v40)) {
                break;
            }
            showMessage(app, "這塊地沒有東西可移動！", 1000);
            v40 = 0;
        }
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
        // ---- 住宅用地搬迁（原版 mode **0x2090802** = flags 2 + BYTE1=8 case8：
        //   拾取层只允许无主空地 owner==0&&level==0；取消不消耗。原重写误记 0x2090002
        //   并把"限无主空地"当作魔改自行 guard 重选——实为原版 BYTE1 过滤语义）----
        const int dest = selectTargetDialog(app, 0x2090802);  // [RE 0x446AE8(0x2090802)]
        if (dest <= 2000 || dest >= 4000) {
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
        // ---- 商業用地搬迁（原版 mode **0x2090804** = flags 4 + BYTE1=8 case8）----
        const int dest = selectTargetDialog(app, 0x2090804);  // [RE 0x446AE8(0x2090804)]
        if (dest <= 4000 || dest >= 6000) {
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
        // ---- 玩家/事件槽 NPC 传送（含 AI 自己；原版 0x4477B5 按 pi>=4 分支）----
        const int pi = bitScanPlayer(static_cast<uint32_t>(v40) & 0xFF);  // [RE 0x40D293]
        if (pi < 0 || pi >= 8) {
            return 0;
        }
        // [HELP 94] 人物必须移至空道路/空地（[USER] 空地 = 无主地块；allowEmptyLand +
        //   resolveTeleportDest 把 2000+/4000+ 对象 id 解析为该地块所在格）
        int dest = 0;
        if (st.players[p].alive == 1) {
            for (int guard = 0; guard < 64; ++guard) {
                const int sel = selectTargetDialog(app, 0x2090001, true);
                if (sel == 0) {
                    break;
                }
                dest = resolveTeleportDest(st, sel);
                if (dest != 0) {
                    break;
                }
                // 诊断：[USER 第3轮] 目标被误拒排查
                RICH4_LOGI("teleport(player) dest rejected: sel=%d (RE 0x447428)", sel);
                showMessage(app, "只能移到空的道路或空地！", 1000);
            }
        } else {
            dest = st.aiItemTarget;  // [RE 0x420EEE] AI 预选目的地（ai_item.cpp）
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
        // [RE 0x4476E1] 朝向：NPC（>=4）取 npcSlots.dir，普通玩家取 g_player dir
        const int curDir = (pi >= 4) ? st.npcSlots[pi - 4].dir : st.players[pi].dir;
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
        const uint32_t mask = 256u << pi;
        if (pi >= 4) {
            // [RE 0x447857] 事件槽 NPC：旧格清位 → npcSlots 更新（cell/prev/dir/pixel）→
            //   新格占位 + 行走资源；players[pi] 运行时镜像同步
            NpcSlot80& slot = st.npcSlots[pi - 4];
            if (slot.cell > 0 && slot.cell < st.cellEnts.size()) {
                st.cellEnts[slot.cell].occMask &= ~mask;
            }
            slot.cell = static_cast<uint16_t>(dest);
            slot.prevCell = prevExit;
            slot.dir = static_cast<uint8_t>(bestDir);
            slot.pixelX = st.cellEnts[dest].x;
            slot.pixelY = st.cellEnts[dest].y;
            st.cellEnts[dest].occMask |= mask;
            Player& np = st.players[pi];
            np.cellEntId = static_cast<uint16_t>(dest);
            np.prevCellEnt = prevExit;
            np.dir = static_cast<uint8_t>(bestDir);
            np.spriteX = st.cellEnts[dest].x;
            np.spriteY = st.cellEnts[dest].y;
            loadWalkResources(st, pi);  // [RE 0x4478BC]
            done = true;
            RICH4_LOGI("teleport NPC %d -> cell %d dir %d prev %u (RE 0x447857)", pi, dest,
                       bestDir, prevExit);
        } else {
            Player& pl = st.players[pi];
            // [RE 0x4477C3] 传送自己：位置/占用更新**前**保存时光机快照（sub_44808A；原版顺序
            //   = sub_44808A → g_stepsRemaining=0 → state=1 → 再改 cellEntId/占位）
            if (pi == p) {
                saveTurnSnapshot(st, p);
            }
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
        }
    } else if ((v40 & 0x8000) != 0 && (v40 & 0x3F00) != 0) {
        // ---- 神明/物品传送（[HELP 94] 必须移至空道路/空地，同玩家分支）----
        int dest = 0;
        if (st.players[p].alive == 1) {
            for (int guard = 0; guard < 64; ++guard) {
                const int sel = selectTargetDialog(app, 0x2090001, true);
                if (sel == 0) {
                    break;
                }
                dest = resolveTeleportDest(st, sel);
                if (dest != 0) {
                    break;
                }
                // 诊断：[USER 第3轮] 目标被误拒排查
                RICH4_LOGI("teleport(object) dest rejected: sel=%d (RE 0x447428)", sel);
                showMessage(app, "只能移到空的道路或空地！", 1000);
            }
        } else {
            dest = st.aiItemTarget;  // [RE 0x420EEE] AI 预选（ai_item.cpp；AI 源=自己不走此分支）
        }
        if (dest == 0 || dest >= static_cast<int>(st.cellEnts.size())) {
            return 0;
        }
        const int slot = (v40 & 0x3F00) >> 8;
        if (slot >= 1 && slot <= 46) {
            const size_t off = static_cast<size_t>(24 * (slot - 1));
            const uint16_t oldCell = st.cellTable[off + 2] | (st.cellTable[off + 3] << 8);
            if (oldCell > 0 && oldCell < st.cellEnts.size()) {
                // [RE 0x44793B] 原版 `*(BYTE*)(cellEnt+38) = 0`：清 occMask bit16-23 全部
                //   槽位（与 deleteMapObject 0x40E243 同语义；原先只清单槽）
                st.cellEnts[oldCell].occMask &= ~0x00FF0000u;
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
        static_cast<size_t>(s.frameCount()) > static_cast<size_t>(c.hover)) {
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
    // [NEW] named region：骰面 1..6（0x4469xx diceHit 同源矩形 104+40*i,314,30,29）
    {
        static bool s_reg = false;
        if (!s_reg) {
            s_reg = true;
            for (int i = 0; i < 6; ++i) {
                char nm[12];
                std::snprintf(nm, sizeof(nm), "dice.%d", i + 1);
                debug::registerRegion(nm, 104 + 40 * i, 314, 30, 29);
            }
        }
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
        dice = st.aiItemTarget;  // [RE 0x420EEE] dword_48BE64[0]（AI 预设点数 1..6；ai_item.cpp）
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
        // 核子飛彈：原版 expireAssets(-1) = sub_40A45C(-1) 采集**整个 440×440 视野**
        //   （g_drawList 全量；帮助"9×9"为约数）；归公 dump=1
        case 13: return launchMissile(app, 13, 0x400C0, -1, 1, 530, 83);
        case 14: return useItemGetOff(app);  // 下車按鈕（道具栏，0x447C00）
        default:
            RICH4_LOGI("item effect id=%d (%s) not implemented (P4, RE 0x475DD4 + 4*id)", id,
                       (id >= 1 && id <= 13) ? kItemBagNames[id] : "?");
            return 0;  // 等价「不可用 → 重选」，不消耗
    }
}

} // namespace rich4
