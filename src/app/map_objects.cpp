#include "game/app/map_objects.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/economy.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/render/blit.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// [RE 0x46352C] dbl_46352C = 0.5（弹飞速度系数，sub_40FAFD）
constexpr double kBounceScale = 0.5;
// [RE 0x46324C] flt_46324C = 0.125（移动动画帧数系数 = 距离/8+1，sub_40E669）
constexpr float kFlyFrameScale = 0.125f;

// [RE 0x463250..0x463495] 神明旁白（BIG5→UTF-8，索引 = cellTable 类型）
const char* const kGodNarr[16] = {
    "",
    "呦喝！～發財發財！\n\n我雖然還小，沒什麼神力\n\n但是我可以讓你損失減半。",
    "呵呵～恭喜發財。\n\n我保佑你財源滾滾\n\n誰也不能罰你的錢。",
    "Ｙｅａｈ!!不要看我小喔\n\n我可以讓你投資事半功倍！",
    "呵呵呵～天官賜福好運到\n\n我保佑你福星高照\n\n投資加倍順利、買地不用錢。",
    "噎噎噎～賠錢！賠錢！",
    "喔～錢掉了。",
    "有我小小衰神，害你一事無成。",
    "哈哈哈！真是好衰啊！",
    "哈雷路亞～\n\n我將使您落腳的土地更加繁榮。",
    "哈～哈～與我同生共死。\n\n我要沿路破壞你所經過的土地。",
    "",
    "呵～呵，有土斯有財\n\n我就是土地公公。\n\n有我在，你想要多少土地就有多少。",
    "",
    "",
    "嘿嘿嘿嘿﹒﹒你慘了！\n\n被我盯上，你死定了。",
};

// [RE 0x4632FD/463353/4633AB/4633D5/463AA8] 神明确告文本模板
constexpr char kFmtGodGet[] = "%s附身\n\n得到%s！";      // 0x4632FD（小福神）
constexpr char kFmtGodGet2[] = "大福神附身\n\n得到%s及%s！"; // 0x463353（大福神）
constexpr char kFmtGodLose[] = "小衰神附身\n\n遺失%s！";  // 0x4633AB（小衰神）
constexpr char kFmtGodLoseHalf[] = "大衰神附身\n\n遺失一半卡片！"; // 0x4633D5（大衰神）
constexpr char kFmtGift[] = "得到%s！";                  // 0x463AA8（禮物/卡片格）

// [RE 0x4652A9/4652C1/4652D1/4652E7] 老虎机标题（%s=神明名；a1=0/1/4/5）
const char* const kSlotTitle[6] = {
    "%s附身\n\n向所有對手收...", "%s附身\n\n送您...", "", "",
    "%s附身\n\n付給每個人...", "%s附身\n\n損錢...",
};
// 老虎机神明名（原版 *(&off_47ED7A + a1) 字节错位取指针，a1=1/5 为原版笔误；
//   重写按 a1 → 小財神/大財神/小窮神/大窮神 对应名修正）
const int kSlotGodType[6] = {1, 2, 0, 0, 5, 6};

// [RE 0x475CE0] dword_475CE0 老虎机数字预览 x（v1=!(a1&1)：4轮/3轮）
constexpr int kSlotPreviewX[2] = {317, 298};
// [RE 0x475CE8] word_475CE8 滚轮 x 坐标（4*a1+v3；3 轮版 [4]=0 不用）
constexpr uint16_t kSlotReelX[8] = {145, 182, 219, 256, 0, 163, 200, 237};
// [RE 0x475D3C] 老虎机音效 = Effect.mkf[51]
constexpr int kSlotMachineSound = 51;

// 神明插图/音效对（attachObject case；[RE 0x40EAD7] sub_450441/sub_45144F 实参）
struct GodFlc {
    int type;
    int res;     // data.mkf 插图
    int sound;   // Effect.mkf 音效
};
constexpr GodFlc kGodFlc[] = {
    {1, 540, 102},  {2, 541, 103},  {3, 542, 104},   {4, 543, 105},
    {5, 544, 106},  {6, 545, 107},  {7, 546, 108},   {8, 547, 109},
    {9, 548, 110},  {10, 549, 112}, {12, 550, 111},  {15, 551, 113},
};

int godFlcRes(int type) {
    for (const GodFlc& g : kGodFlc) {
        if (g.type == type) {
            return g.res;
        }
    }
    return 0;
}
int godFlcSound(int type) {
    for (const GodFlc& g : kGodFlc) {
        if (g.type == type) {
            return g.sound;
        }
    }
    return -1;
}

uint8_t cellTypeAt(const GameState& st, int slot) {
    return st.cellTable[static_cast<size_t>(24 * slot)];
}

} // namespace

const char* godNarrText(int type) {
    return (type >= 1 && type <= 15) ? kGodNarr[type] : "";
}

// [RE 0x40EA62] canAttach（type≤12 且 ≠11 惡犬，或 15 死神）
bool canAttachObject(const GameState& st, int objId) {
    if (objId <= 0 || objId > 46) {
        return false;
    }
    const unsigned t = cellTypeAt(st, objId - 1);
    return (t <= 12 && t != 11) || t == 15;
}

// [RE 0x40FC00] updatePlayerCarriedObjects：附身/挂身物件 cellEnt 同步为玩家格
void updatePlayerCarriedObjects(GameState& st, int p) {
    Player& pl = st.players[p];
    if (pl.cellTableIdx != 0) {
        const size_t off = static_cast<size_t>(24 * (pl.cellTableIdx - 1));
        st.cellTable[off + 2] = static_cast<uint8_t>(pl.cellEntId & 0xFF);
        st.cellTable[off + 3] = static_cast<uint8_t>(pl.cellEntId >> 8);
    }
    if (pl.cellNo != 0) {
        const size_t off = static_cast<size_t>(24 * (pl.cellNo - 1));
        st.cellTable[off + 2] = static_cast<uint8_t>(pl.cellEntId & 0xFF);
        st.cellTable[off + 3] = static_cast<uint8_t>(pl.cellEntId >> 8);
    }
}

// [RE 0x40FAFD] bounceObject：弹飞（速度 = (起点−目标)×0.5，pos = 起点+速度；
//   flyCount(+6)=-1 起步、flyDir(+7)=朝向；renderMap 每帧 pos+=vel、flyCount--）
void bounceObject(GameState& st, int objId, int fromCell, int toCell) {
    if (objId <= 0 || fromCell <= 0 || toCell <= 0 ||
        fromCell >= static_cast<int>(st.cellEnts.size()) ||
        toCell >= static_cast<int>(st.cellEnts.size())) {
        return;
    }
    const size_t off = static_cast<size_t>(24 * (objId - 1));
    uint8_t* e = &st.cellTable[off];
    const CellEnt& a = st.cellEnts[fromCell];
    const CellEnt& b = st.cellEnts[toCell];
    float vx = static_cast<float>((static_cast<double>(a.x - b.x)) * kBounceScale);
    float vy = static_cast<float>((static_cast<double>(a.y - b.y)) * kBounceScale);
    std::memcpy(e + 16, &vx, 4);
    std::memcpy(e + 20, &vy, 4);
    float px = static_cast<float>(a.x) + vx;
    float py = static_cast<float>(a.y) + vy;
    std::memcpy(e + 8, &px, 4);
    std::memcpy(e + 12, &py, 4);
    e[6] = 0xFF;  // flyCount = -1（渲染每帧递减）
    e[7] = e[1];  // flyDir = 当前朝向
}

// [RE 0x40CC56] relocatePlayer：乞丐/传送随机换位（清旧占用 → randomCellEnt(旧格) →
//   首个出口定朝向 → 占新格；spriteX/Y = 新格坐标）
void relocatePlayer(GameState& st, int p) {
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    if (pl.cellEntId == 0 || pl.cellEntId >= st.cellEnts.size()) {
        return;
    }
    st.cellEnts[pl.cellEntId].occMask &= ~(256u << p);
    const int newEnt = randomCellEnt(st, pl.cellEntId);
    if (newEnt <= 0 || newEnt >= static_cast<int>(st.cellEnts.size())) {
        return;
    }
    CellEnt& ce = st.cellEnts[newEnt];
    uint16_t exitId = 0;
    for (int i = 0; i < 4; ++i) {
        if (ce.exits[i] != 0) {
            exitId = ce.exits[i];
            break;
        }
    }
    pl.spriteX = ce.x;
    pl.spriteY = ce.y;
    pl.cellEntId = static_cast<uint16_t>(newEnt);
    pl.prevCellEnt = exitId;
    pl.dir = static_cast<uint8_t>(facingBetween(st, exitId, newEnt));
    ce.occMask |= 256u << p;
    updatePlayerCarriedObjects(st, p);
}

// ===== 老虎机（0x440706/0x43F23E）=====

namespace {

struct SlotCtx {
    Application* app = nullptr;
    UiImage ui;
    int a1 = 0;
    int reels = 4;         // a1==0 → 4 轮，否则 3 轮
    int byte_[4] = {0, 0, 0, 0};  // [RE byte_48C504] 每轮帧值（0..19，奇数=定格）
    int x[4] = {0, 0, 0, 0};
    int phase = 1;         // 1 等待/转 2 预览1 3 预览2 4..7 停轮 8 金额 9 尾帧
    int frame = 0;
    bool humanCtl = false;
    int result = 0;
    int soundPlayed = 0;
};

// [RE 0x43F23E] 绘制：背景帧 v1=!(a1&1) @(220,320)；各轮 +frame(4+byte) @轮x,320
void slotDraw(SlotCtx& sc) {
    Application& app = *sc.app;
    Surface& dst = app.surface();
    GameState& st = app.gameState();
    renderGameFrame(app);
    const int bg = !(sc.a1 & 1);
    if (sc.ui.frameCount() > 1) {
        blitElement(dst, sc.ui.frame(bg), 220, 320, false);
    }
    const int first = (sc.a1 == 0) ? 0 : 1;
    for (int i = first; i < 4; ++i) {
        int f = sc.byte_[i] + 4;
        if (f >= sc.ui.frameCount()) {
            f = sc.ui.frameCount() - 1;
        }
        if (f >= 0 && sc.ui.frameCount() > 0) {
            blitElement(dst, sc.ui.frame(f), sc.x[i], 320, false);
        }
    }
    // 预览框（phase2 画 +36=帧3、phase3 画 +48=帧4 于第一停轮位）
    if (sc.phase == 2 && sc.ui.frameCount() > 3) {
        blitElement(dst, sc.ui.frame(3), kSlotPreviewX[bg], 240, false);
    } else if (sc.phase == 3 && sc.ui.frameCount() > 4) {
        blitElement(dst, sc.ui.frame(4), kSlotPreviewX[bg], 240, false);
    }
    // 标题（data.mkf[517] 帧5 提示框 + 16 号字）
    const UiImage& tip = st.estateTiles;
    if (tip.frameCount() > 5) {
        blitElement(dst, tip.frame(5), 220, 140, false);
    }
    const char* godName = kObjectNames[kSlotGodType[sc.a1]];
    char text[192];
    std::snprintf(text, sizeof(text), kSlotTitle[sc.a1], godName);
    app.text().setFont(16, 0xF0F0F0, 0x101010, kTextStyleShadow | kTextStyleBold, 1);
    app.text().drawText(dst, text, 220, 140, 4);
    if (sc.phase == 8) {
        char amount[16];
        std::snprintf(amount, sizeof(amount), "%d", sc.result);
        app.text().drawText(dst, amount, 220, 140, 4);
    }
}

// [RE 0x43EF3E] 每帧推进已内联到 slotHandler 各阶段（原版全局计数 dword_475D5C 节奏等价）

bool slotHandler(const SDL_Event* event, void* user) {
    SlotCtx& sc = *static_cast<SlotCtx*>(user);
    Application& app = *sc.app;
    if (event == nullptr) {
        slotDraw(sc);
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT &&
        sc.humanCtl && sc.phase == 1) {
        sc.phase = 2;
        sc.frame = 0;
        return true;
    }
    if (event->type != kModalTimerEvent) {
        return true;  // 模态吞事件
    }
    if (sc.phase <= 7) {
        app.audio().playEffect(kSlotMachineSound);  // [RE 0x475D3C] 滚动循环音
    }
    const int first = (sc.a1 == 0) ? 0 : 1;
    switch (sc.phase) {
        case 1:  // 等待：人类无限转（点击进 2），AI/托管 40 帧自动
            for (int i = first; i < 4; ++i) {
                if ((sc.frame % 10) == 0 && (i + sc.frame) % 3 == 0) {
                    sc.byte_[i] = std::rand() % 10 * 2 + 1;
                }
                sc.byte_[i] = (sc.byte_[i] + 1) % 20;
            }
            ++sc.frame;
            if (!sc.humanCtl && sc.frame >= 40) {
                sc.phase = 2;
                sc.frame = 0;
            }
            break;
        case 2:  // 预览数字 1（帧3）+ 点击音
            app.audio().playEffect(1);
            sc.phase = 3;
            sc.frame = 0;
            break;
        case 3:  // 预览数字 2（帧4）后 4 帧 → 停轮阶段
            if (++sc.frame >= 4) {
                sc.phase = 4;
                sc.frame = 0;
            }
            break;
        case 4:  // 停轮 1..4（每轮 8 帧；a1=0 含轮0）
        {
            const int stopReel = first;
            bool done = false;
            for (int i = first; i < 4; ++i) {
                if (i != stopReel) {
                    sc.byte_[i] = (sc.byte_[i] + 1) % 20;
                }
            }
            if (++sc.frame >= 8) {
                sc.byte_[stopReel] = (sc.byte_[stopReel] | 1);
                done = true;
            }
            if (done) {
                sc.phase = stopReel >= 3 ? 8 : 4 + (stopReel + 1 - first);
                sc.frame = 0;
            }
            break;
        }
        case 5:
        case 6:
        case 7:  // 停后续轮（阶段号 = 已停轮数；每轮 8 帧）
        {
            const int stopReel = sc.a1 == 0 ? sc.phase - 3 : sc.phase - 2;
            for (int i = stopReel; i < 4; ++i) {
                sc.byte_[i] = (sc.byte_[i] + 1) % 20;
            }
            if (++sc.frame >= 8) {
                sc.byte_[stopReel] = (sc.byte_[stopReel] | 1);
                sc.phase = (stopReel >= 3) ? 8 : sc.phase + 1;
                sc.frame = 0;
            }
            break;
        }
        case 8:  // 定格金额显示
            if (sc.result == 0) {
                sc.result = (sc.a1 == 0 ? (sc.byte_[0] >> 1) * 1000 : 0) +
                            (sc.byte_[1] >> 1) * 100 + (sc.byte_[2] >> 1) * 10 +
                            (sc.byte_[3] >> 1);
            }
            if (++sc.frame >= 40) {
                sc.phase = 9;
            }
            break;
        default:
            app.events().requestExit(sc.result);
            return true;
    }
    slotDraw(sc);
    if (sc.phase >= 9) {
        app.events().requestExit(sc.result);
    }
    return true;

}

} // namespace

// [RE 0x440706] slotMachineDialog：財神/窮神数字老虎机（panel.mkf[67]）
// 依据: 0x440706/0x43F23E 反编译; a1=0 4 轮 0..9999、1 3 轮 0..999（4/5 同 3 轮）；
//   人类点击提前定格（alive==1 且无夢遊），AI 40 帧自动；结果 = 数字帧值/2 加权
// 差异: 原版滚轮 sub_43EF3E 用全局计数 dword_475D5C 控制逐轮定格时机（8 帧/轮
//   + 预览 2 帧），重写按同等节奏实现；原版标题指针 *(&off_47ED7A+a1) 字节错位
//   （a1=1/5 为笔误），重写直接取对应神明名
int slotMachineDialog(Application& app, int a1) {
    GameState& st = app.gameState();
    SlotCtx sc;
    sc.app = &app;
    sc.a1 = a1;
    sc.reels = (a1 == 0) ? 4 : 3;
    const int first = (a1 == 0) ? 0 : 1;
    for (int i = first; i < 4; ++i) {
        sc.byte_[i] = (std::rand() % 10) * 2 + 1;
        sc.x[i] = kSlotReelX[4 * (a1 != 0) + i];
    }
    if (auto blob = st.panel.read(67)) {
        sc.ui.load(std::move(*blob));
    }
    if (sc.ui.frameCount() < 24) {
        RICH4_LOGW("slotMachineDialog: panel.mkf[67] unavailable (frames=%d)",
                   sc.ui.frameCount());
        // 回退：直接随机（0..9999 / 0..999），保证效果数值可用
        return (a1 == 0) ? std::rand() % 10000 : std::rand() % 1000;
    }
    const int cp = st.currentPlayer;
    sc.humanCtl = cp >= 0 && cp < 4 && st.players[cp].alive == 1 && st.players[cp].state37 == 0;
    RICH4_LOGI("slotMachine: a1=%d human=%d (RE 0x440706)", a1, sc.humanCtl ? 1 : 0);
    const int result = runModal(app, slotHandler, &sc, 24);
    RICH4_LOGI("slotMachine result=%d (RE 0x43F23E)", result);
    return result;
}

// ===== 附身（0x40EAD7 / 0x40E32C）=====

// [RE 0x40E2A2] showGodNarration：屏幕下方 28px 白字旁白，2400ms
void showGodNarration(Application& app, const char* text) {
    Surface& dst = app.surface();
    app.text().setFont(28, 0xFFFFFF, 0x101010, kTextStyleShadow, 0);
    renderGameFrame(app);
    app.text().drawText(dst, text, 220, 460, 7);
    app.renderFrame();
    const uint64_t end = SDL_GetTicks() + 2400;
    while (static_cast<int64_t>(SDL_GetTicks() - end) < 0) {
        app.audio().update();
        SDL_Delay(20);
    }
}

// [RE 0x40E32C] attachEnd：附身结束（24 帧上升飘走 → 删除 → 配对轮替）
// 依据: 0x40E32C 反编译; 挂起态（stateFlags≠0）跳过动画静默删除；
//   动画：物件精灵自玩家位置每帧 y-=10、朝向帧 (f+1)&7、60ms/帧，顶出 40 早停；
//   音效槽 19（dword_4823E2）；结束后穷神/衰神/死神追加哭台词（sub_44EF41 → [TODO P4]）
void attachEnd(Application& app, int p) {
    GameState& st = app.gameState();
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    const int objId = pl.cellTableIdx;
    if (objId == 0) {
        return;
    }
    const size_t off = static_cast<size_t>(24 * (objId - 1));
    const uint8_t type = st.cellTable[off];
    if (pl.stateFlags != 0) {
        releaseCellTableSlot(app, objId);
        return;
    }
    // 临时摘除物件（renderGameFrame 每帧重画场景）
    const uint16_t savedEnt =
        static_cast<uint16_t>(st.cellTable[off + 2] | (st.cellTable[off + 3] << 8));
    st.cellTable[off + 2] = 0;
    st.cellTable[off + 3] = 0;
    int sx = 0;
    int sy = 0;
    if (!projectMapPoint(st, pl.spriteX, pl.spriteY, sx, sy)) {
        sx = 220;
        sy = 260;
    }
    UiImage& img = (type >= 1 && type <= 20) ? st.cellTypeSprites[type] : st.cellTypeSprites[0];
    app.audio().playEffectSlot(19);  // [RE dword_4823E2]
    int frame = (pl.dir + 8 - st.mapRotation) & 7;
    setClipRect(0, 40, 440, 480);
    Surface& dst = app.surface();
    for (int i = 0; i < 24; ++i) {
        sy -= 10;
        frame = (frame + 1) & 7;
        if (sy < 40) {
            break;
        }
        renderGameFrame(app);
        blitSpriteFrameClipped(dst, img, frame, sx, sy, true);
        app.renderFrame();
        app.pumpEvents();
        const uint64_t end = SDL_GetTicks() + 60;
        while (static_cast<int64_t>(SDL_GetTicks() - end) < 0) {
            app.audio().update();
            SDL_Delay(5);
        }
    }
    setClipRect(0, 0, 640, 480);
    app.audio().stopEffectSlot(19);
    st.cellTable[off + 2] = static_cast<uint8_t>(savedEnt & 0xFF);
    st.cellTable[off + 3] = static_cast<uint8_t>(savedEnt >> 8);
    // [RE 0x40E14D] 删除 + 配对轮替
    releaseCellTableSlot(app, objId);
    RICH4_LOGI("attachEnd: player %d god type %u left (RE 0x40E32C)", p, type);
}

// [RE 0x40EAD7] attachObject：踩中可附身物件 → 旧神飘走、新神挂身（寿命 7/13）、
//   luckA/B/C += kLuck*、case 即时效果（插图+旁白受设置 byte_497159=settings[1] 控制）
// 差异: 角色台词/语音 sub_44EF41（哭/笑，off_48086A/862/8A2 表）与卡片语音
//   sub_44F230（byte_47FDEF 价→语音）为 [TODO P4]；大財神超额台词 sub_44F354 仅选句 → P4
void attachObject(Application& app, int p, int cellEntId, int objId) {
    GameState& st = app.gameState();
    if (objId == 0 || !canAttachObject(st, objId) || p < 0 || p >= 9) {
        return;
    }
    const size_t off = static_cast<size_t>(24 * (objId - 1));
    uint8_t* e = &st.cellTable[off];
    const uint8_t type = e[0];
    Player& pl = st.players[p];
    if (pl.cellTableIdx != 0) {
        attachEnd(app, p);  // 旧神飘走（→轮替）
    }
    pl.cellTableIdx = static_cast<uint8_t>(objId);
    e[2] = static_cast<uint8_t>(pl.cellEntId & 0xFF);
    e[3] = static_cast<uint8_t>(pl.cellEntId >> 8);
    e[5] = static_cast<uint8_t>(p + 1);
    e[4] = (type == 15) ? 13 : 7;  // 死神 13 天、其余 7 天
    if (cellEntId > 0 && cellEntId < static_cast<int>(st.cellEnts.size())) {
        st.cellEnts[cellEntId].occMask &= ~0x00FF0000u;
    }
    pl.luckA += kLuckA[type];
    pl.luckB += kLuckB[type];
    pl.luckC += kLuckC[type];
    const bool voiceOn = st.settings[1] != 0;  // [RE byte_497159]
    char text[256];
    switch (type) {
        case 1: {  // 小財神：向所有对手收取（老虎机 4 位）
            if (voiceOn) {
                playEventFlc(app, godFlcRes(1), 0, 40, godFlcSound(1));
                showGodNarration(app, kGodNarr[1]);
            }
            const int v = slotMachineDialog(app, 0);
            for (int i = 0; i < st.playerCount && !st.sceneRequest; ++i) {
                if (i != p && st.players[i].alive != 0) {
                    transferMoney(app, i, p, v, 1);  // 对手→我（现金）
                }
            }
            // [TODO P4] v>700 大笑台词 sub_44EF41(p,3,off_48086A[char])
            RICH4_LOGI("attach smallRich: %d each from rivals (RE 0x40EAD7 case1)", v);
            break;
        }
        case 2: {  // 大財神：入账（老虎机 3 位）
            if (voiceOn) {
                playEventFlc(app, godFlcRes(2), 0, 40, godFlcSound(2));
                showGodNarration(app, kGodNarr[2]);
            }
            const int v = slotMachineDialog(app, 1);
            addMoney(app, p, v, true);
            // [TODO P4] v≥5000×M 超额台词 sub_44F354（三档选句）
            RICH4_LOGI("attach bigRich: %d to cash (RE 0x40EAD7 case2)", v);
            break;
        }
        case 3: {  // 小福神：抽 1 卡
            if (voiceOn) {
                playEventFlc(app, godFlcRes(3), 0, 40, godFlcSound(3));
                showGodNarration(app, kGodNarr[3]);
            }
            const int card = drawFreeCard(st, p);
            if (card != 0) {
                std::snprintf(text, sizeof(text), kFmtGodGet, kObjectNames[3],
                              kCardNames[card]);
                showMessage(app, text, 1500);
                // [TODO P4] 卡片语音 sub_44F230(currentPlayer, kCardPrices[card])
            }
            break;
        }
        case 4: {  // 大福神：抽 2 卡
            if (voiceOn) {
                playEventFlc(app, godFlcRes(4), 0, 40, godFlcSound(4));
                showGodNarration(app, kGodNarr[4]);
            }
            const int c1 = drawFreeCard(st, p);
            const int c2 = drawFreeCard(st, p);
            std::snprintf(text, sizeof(text), kFmtGodGet2, kCardNames[c1], kCardNames[c2]);
            showMessage(app, text, 1500);
            // [TODO P4] 双卡语音 sub_44F230(两卡价和)
            break;
        }
        case 5: {  // 小窮神：付给每个对手（老虎机 3 位）
            // [TODO P4] 哭台词 sub_44EF41(p,2,off_4808A2[char])
            if (voiceOn) {
                playEventFlc(app, godFlcRes(5), 0, 40, godFlcSound(5));
                showGodNarration(app, kGodNarr[5]);
            }
            const int v = slotMachineDialog(app, 4);
            for (int i = 0; i < st.playerCount && !st.sceneRequest; ++i) {
                if (i != p && st.players[i].alive != 0) {
                    transferMoney(app, p, i, v, 0);  // 我→对手（入银行）
                }
            }
            break;
        }
        case 6: {  // 大窮神：付给银行
            // [TODO P4] 哭台词
            if (voiceOn) {
                playEventFlc(app, godFlcRes(6), 0, 40, godFlcSound(6));
                showGodNarration(app, kGodNarr[6]);
            }
            const int v = slotMachineDialog(app, 5);
            transferMoney(app, p, -1, v, 0);
            break;
        }
        case 7: {  // 小衰神：随机丢 1 卡
            // [TODO P4] 哭台词
            if (voiceOn) {
                playEventFlc(app, godFlcRes(7), 0, 40, godFlcSound(7));
                showGodNarration(app, kGodNarr[7]);
            }
            const int card = discardRandomCard(app, p);
            if (card != 0) {
                std::snprintf(text, sizeof(text), kFmtGodLose, kCardNames[card]);
                showMessage(app, text, 1500);
            }
            break;
        }
        case 8: {  // 大衰神：丢一半卡
            // [TODO P4] 哭台词
            if (voiceOn) {
                playEventFlc(app, godFlcRes(8), 0, 40, godFlcSound(8));
                showGodNarration(app, kGodNarr[8]);
            }
            if (discardHalfCards(app, p) != 0) {
                showMessage(app, kFmtGodLoseHalf, 1500);
            }
            break;
        }
        case 9:  // 天使：仅插图/旁白 + 三属性
            if (voiceOn) {
                playEventFlc(app, godFlcRes(9), 0, 40, godFlcSound(9));
                showGodNarration(app, kGodNarr[9]);
            }
            break;
        case 10:  // 惡魔
            if (voiceOn) {
                playEventFlc(app, godFlcRes(10), 0, 40, godFlcSound(10));
                showGodNarration(app, kGodNarr[10]);
            }
            break;
        case 12:  // 土地公
            if (voiceOn) {
                playEventFlc(app, godFlcRes(12), 0, 40, godFlcSound(12));
                showGodNarration(app, kGodNarr[12]);
            }
            break;
        case 15:  // 死神：没收全部道具+卡片（寿命 13 已在上方）
            // [TODO P4] 哭台词
            if (voiceOn) {
                playEventFlc(app, godFlcRes(15), 0, 40, godFlcSound(15));
                showGodNarration(app, kGodNarr[15]);
            }
            confiscateItems(app, p);
            confiscateCards(app, p);
            break;
        default:
            break;
    }
}

// ===== 道具/效果辅助 =====

// [RE 0x40CD07] damagePlayer：载具报废 + 受伤（0x40 位）+ 步行资源重载
// 差异: 载具损毁计数 byte_497324/25（新闻/统计用）未建模；淘汰分支同原版
void damagePlayer(Application& app, int p) {
    GameState& st = app.gameState();
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    if (pl.alive == 0 || pl.stateFlags != 0) {
        if (pl.alive == 0) {
            eliminatePlayer(app, p);
        }
        return;
    }
    if (pl.travel != 0) {
        pl.travel = 0;
        pl.diceCount = 1;
    }
    pl.alive |= 0x40;  // [RE 0x40CD5E] 受伤标志
    loadWalkResources(st, p);
    RICH4_LOGI("damagePlayer: p=%d vehicle destroyed (RE 0x40CD07)", p);
}

// [RE 0x40AB4A] demolishAtObjId：住宅用地（2000..3999）/商業用地（4000..5999）拆除
//   mode 0 = 降一级；1 = 没收重置；2 = 拆建筑留地（type≠0 一并清）
// 差异: 原版小地图即时重绘（重写绘制时重建）；corp sub_40DFFA（费用统计）未建模
bool demolishAtObjId(Application& app, int objId, int mode) {
    GameState& st = app.gameState();
    bool changed = false;
    if (objId > 2000 && objId < 4000) {
        Estate& es = st.estates[static_cast<size_t>(objId - 2000)];
        if (mode == 1) {
            es.owner = 0;
            es.level = 0;
            es.type = 0;
            es.expireDate = 0;
            changed = true;
        } else if (mode == 2) {
            if (es.level != 0) {
                es.level = 0;
                es.type = 0;
                changed = true;
            }
        } else if (mode == 0 && es.level != 0) {
            --es.level;
            if (es.type != 0) {
                es.level = 0;
                es.type = 0;
            }
            changed = true;
        }
    } else if (objId > 4000 && objId < 6000) {
        Corp& cp = st.corps[static_cast<size_t>(objId - 4000)];
        if (mode == 1) {
            cp.owner = 0;
            cp.sub = 0;
            cp.type = 0;
            cp.expireDate = 0;
            changed = true;
        } else if (mode == 2) {
            if (cp.sub != 0) {
                cp.sub = 0;
                cp.type = 0;
                changed = true;
            }
        } else if (mode == 0 && cp.sub != 0) {
            --cp.sub;
            if (cp.sub == 0) {
                cp.type = 0;
            }
            changed = true;
        }
    }
    return changed;
}

// [RE 0x445ADA] drawGiftCard：礼物池 misc8A[0..7]（道具 id1..8）按库存展开随机抽 1
int drawGiftCard(Application& app, int player) {
    GameState& st = app.gameState();
    uint8_t list[136];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < st.misc8A[i] && n < 136; ++k) {
            list[n++] = static_cast<uint8_t>(i);
        }
    }
    if (n == 0) {
        return 0;
    }
    const int item = list[std::rand() % n] + 1;
    givePlayerItem(st, player, item);
    return item;
}

// [RE 0x441E77] discardRandomCard：卡包随机 1 张丢弃（后槽前移），返回卡 id
int discardRandomCard(Application& app, int player) {
    GameState& st = app.gameState();
    const int n = cardBagCount(st, player);
    if (n == 0) {
        return 0;
    }
    const size_t off = static_cast<size_t>(15 * player);
    const int card = st.cardState60[off + std::rand() % n];
    cardBagRemove(st, player, card);
    return card;
}

// [RE 0x441ECE] discardHalfCards：丢前 数量/2 张（原版 v1 自增不回填，逐字保持）
int discardHalfCards(Application& app, int player) {
    GameState& st = app.gameState();
    const int n = cardBagCount(st, player);
    int v1 = 0;
    if (n > 1) {
        while (v1 < n / 2) {
            cardBagRemove(st, player, st.cardState60[15 * player + v1]);
            ++v1;
        }
        return 1;
    }
    return 0;
}

// [RE 0x445B3F] confiscateItems：没收玩家全部道具 →
//   前 8 类回礼物池 misc8A；载具/特殊道具归还玩家槽（机车=+5、汽车=+6、
//   时光机类=+12 按原版地址 0x499160/61/67）；载具恢复步行
// 差异: 原版 byte_47FEE7[8*v3] 为道具结构 +1（价格，点券），重写以价值累加返回
//   （本返回值原版未使用于扣款——仅没收；调用方直接丢弃返回值）
int confiscateItems(Application& app, int player) {
    GameState& st = app.gameState();
    Player& pl = st.players[player];
    if (pl.travel != 0) {
        const int t = pl.travel & 3;
        if (t == 1) {
            ++st.itemStock[15 * player + 4];  // [RE 0x499160]
        } else if (t == 2) {
            ++st.itemStock[15 * player + 5];  // [RE 0x499161]
        } else if (t == 3) {
            ++st.itemStock[15 * player + 11];  // [RE 0x499167]
        }
        pl.travel = 0;
        pl.diceCount = 1;
        loadWalkResources(st, player);
    }
    int value = 0;
    for (int i = 0; i < 13; ++i) {
        const size_t s = static_cast<size_t>(15 * player + i);
        const uint8_t cnt = st.itemStock[s];
        if (cnt == 0) {
            continue;
        }
        if (i < 8) {
            st.misc8A[i] = static_cast<uint8_t>(st.misc8A[i] + cnt);
        }
        value += cnt * kItemPrice[i];
        st.itemStock[s] = 0;
    }
    return value;
}

// [RE 0x441F21] confiscateCards：没收卡包全部卡 → 赠卡池 propStock 归还
int confiscateCards(Application& app, int player) {
    GameState& st = app.gameState();
    int value = 0;
    for (int i = 0; i < 15; ++i) {
        const size_t s = static_cast<size_t>(15 * player + i);
        const uint8_t card = st.cardState60[s];
        if (card == 0) {
            continue;
        }
        if (card >= 1 && card <= 30) {
            ++st.propStock[card - 1];  // [RE 0x441F54 ++byte_499197[card]]
            value += kCardPrices[card];
        }
        st.cardState60[s] = 0;
    }
    return value;
}

// [RE 0x41D3F4] addMoney：入账（true=现金/false=银行）+ 本月意外之财（monthSettleB）
void addMoney(Application& app, int player, int32_t amount, bool cash) {
    GameState& st = app.gameState();
    Player& pl = st.players[player];
    if (cash) {
        pl.cash += amount;
    } else {
        pl.bank += amount;
    }
    pl.monthSettleB += amount;  // [RE 0x44D418 dword_496BC8]
}

// [RE 0x40E669] flyObjectSprite：物件精灵 A→B 直线插值动画（24ms/帧 + 结束停留）
// 依据: 0x40E669 反编译; 帧数 = 距离×0.125+1；objId=0 用通用精灵（dword_49697C）；
//   朝向 = (物件dir + 8 - 视角) & 7（objId=0 → 帧 0）
void flyObjectSprite(Application& app, int objId, int fromX, int fromY, int toX, int toY,
                     int holdMs) {
    GameState& st = app.gameState();
    Surface& dst = app.surface();
    int dir = 0;
    UiImage* img = nullptr;
    if (objId != 0) {
        const size_t off = static_cast<size_t>(24 * (objId - 1));
        dir = (st.cellTable[off + 1] + 8 - st.mapRotation) & 7;
        const uint8_t type = st.cellTable[off];
        if (type >= 1 && type <= 45) {
            img = &st.cellTypeSprites[type];
        }
    }
    if (!img || img->frameCount() == 0) {
        return;
    }
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!projectMapPoint(st, fromX, fromY, x0, y0) || !projectMapPoint(st, toX, toY, x1, y1)) {
        return;
    }
    const double dist = std::sqrt(static_cast<double>((x1 - x0) * (x1 - x0) +
                                                      (y1 - y0) * (y1 - y0)));
    const int frames = static_cast<int>(dist * kFlyFrameScale) + 1;
    const double stepX = static_cast<double>(x0 - x1) / frames;
    const double stepY = static_cast<double>(y0 - y1) / frames;
    double px = x1;
    double py = y1;
    setClipRect(0, 40, 440, 480);
    for (int i = 0; i < frames; ++i) {
        const uint64_t end = SDL_GetTicks() + 24;
        renderGameFrame(app);
        blitSpriteFrameClipped(dst, *img, dir, static_cast<int>(px), static_cast<int>(py), true);
        app.renderFrame();
        app.pumpEvents();
        while (static_cast<int64_t>(SDL_GetTicks() - end) < 0) {
            app.audio().update();
            SDL_Delay(2);
        }
        px += stepX;
        py += stepY;
    }
    setClipRect(0, 0, 640, 480);
    (void)holdMs;
    RICH4_LOGI("flyObject: obj %d (%d,%d)->(%d,%d) %d frames (RE 0x40E669)", objId, fromX, fromY,
               toX, toY, frames);
}

// [RE 0x445A4D] givePlayerItem：itemStock[15p+id-1]++（上限 9）；id≤8 消耗礼物池 misc8A
// 依据: 0x445A4D 反编译; 原版 g_playerCards 基址 0x49915B、重写 itemStock 基址
//   0x49915C（索引 = 15p+id-1）；g_cardPool[1..8] = misc8A[0..7]
void givePlayerItem(GameState& st, int player, int itemId) {
    if (player < 0 || player >= 4 || itemId < 1 || itemId > 13) {
        return;
    }
    const size_t s = static_cast<size_t>(15 * player + itemId - 1);
    if (st.itemStock[s] >= 9) {
        return;
    }
    if (itemId <= 8) {
        if (st.misc8A[itemId - 1] == 0) {
            return;
        }
        --st.misc8A[itemId - 1];
    }
    ++st.itemStock[s];
}

// [RE 0x445AA2] takePlayerItem：扣 1（id≤8 归还礼物池），成功返回 true
bool takePlayerItem(GameState& st, int player, int itemId) {
    if (player < 0 || player >= 4 || itemId < 1 || itemId > 13) {
        return false;
    }
    const size_t s = static_cast<size_t>(15 * player + itemId - 1);
    if (st.itemStock[s] == 0) {
        return false;
    }
    --st.itemStock[s];
    if (itemId <= 8 && st.misc8A[itemId - 1] < 0xFF) {
        ++st.misc8A[itemId - 1];
    }
    return true;
}

} // namespace rich4
