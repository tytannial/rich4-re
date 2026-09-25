// 魔法屋（magicHouseVisit 0x43380A + magicHouseWndProc 0x4325C2 +
//        magicSelectTargets 0x431842 + applyMagicPenalty 0x431CAA + turnToAdjacentCell 0x40C78C）。
// 依据: docs/reverse/functions/43380a-magic-house.md §1..§4。
//
// 流程：人类 → panel[18] 施法 UI（100ms 状态机 1..8）：
//   #0037→#0038→#0039 → EXIT 按钮(帧3) → 倒计时(动画开 1s/关 1tick) → 随机条件（0x431842
//   非空目标）→ 条件图标帧(cond+11)@(326,296) + 浮字条件名 → #0040 → 交互：
//   panel[19] RAW pick（1..12 惩罚 / 13 EXIT / 0 无）hover 高亮（帧 v+22 小图标 @off_4756E4
//   + 144×128 大图/名称 saveBackground 缓冲 + 音效 39）→ 点击 → 大图恢复 + 帧2 确认罩
//   @(182,142) + #0041 → 播 panel[20] FLC @(0,0)（音效 59）→ postModalExit(pen)。
//   右键/双击（state<3）→ 跳过动画直达选择；EXIT → 回浮字重看。
//   AI/托管 → 条件 rand%12（须有目标）+ 惩罚 rand%11（6→7；自己中招强制 6）+
//   showMessage("条件名\n\n惩罚名")（'#' 前缀跳 5 字节）。
// 惩罚执行（applyMagicPenalty）：对目标名单逐一（≤4）：强制场景重绘（[RE 0x41906A]）→
//   "玩家名\n\n惩罚名" showMessage → 效果（复用收租/神明/卡片/拍卖链）。
// 差异: turnToAdjacentCell 原版音效句柄 dword_4823F2 从未注册（静默 quirk）；
//   NPC（事件槽）转向分支 a1>=4 依赖犯人表 → P5 未建模。

#include "game/app/magic_house_dialog.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/auction_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/event_stack.h"
#include "game/app/fate_event.h"
#include "game/app/float_message.h"
#include "game/app/game_loop.h"
#include "game/app/item_lines.h"
#include "game/app/map_objects.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/platform/audio.h"
#include "game/render/blit.h"
#include "game/render/fli.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {

namespace {

// ===== 数据表（43380a-magic-house.md §2/§3）=====

// [RE 0x4756B8] 12 条件名（FloatMessage '#NNNN' 前缀自动播语音；AI 消息跳 5 字节）
const char* const kCondNames[12] = {
    "#0046財產最多的人", "#0047土地最多的人", "#0048房屋最多的人", "#0049現金最多的人",
    "#0050存款最多的人", "#0051點券最多的人", "#0052走路的人",   "#0053騎機車的人",
    "#0054開汽車的人",   "#0055神明附身的人", "#0056所有男生",   "#0057所有女生",
};

// [RE 0x475708 stride16 表 id1..12] {大图帧, 大图中心x, y, 惩罚名}
struct MagicPenalty {
    int bigFrame;
    int bigX;
    int bigY;
    const char* name;
};
const MagicPenalty kPenalties[13] = {
    {0, 0, 0, ""},
    {9, 208, 167, "變賣所有卡片"},  {10, 510, 150, "抽取命運三張"},
    {7, 545, 88, "立刻坐牢三天"},   {7, 568, 147, "原地停留一回合"},
    {7, 550, 234, "存入所有現金"},  {7, 510, 320, "就地加蓋房屋"},
    {7, 422, 320, "得一張卡片"},    {6, 134, 318, "向後轉"},
    {6, 92, 250, "變賣所有道具"},   {6, 72, 130, "就地拆除房屋"},
    {6, 77, 85, "住院檢查三天"},    {9, 122, 154, "拍賣當格土地"},
};

// [RE 0x4756E4] 12 惩罚小图标中心（64×64 区）
const int kIconPos[12][2] = {
    {322, 91}, {414, 82}, {453, 157}, {509, 242}, {458, 314}, {415, 387},
    {322, 394}, {239, 393}, {188, 316}, {131, 222}, {184, 161}, {225, 83},
};

// [RE 0x4645E4..0x464653] UI 流程浮字
constexpr char kTextIn[] = "#0037進來魔法屋，就得完全照我的指示！";
constexpr char kTextPick[] = "#0038我選出符合條件的人。";
constexpr char kTextFate[] = "#0039你來決定他們的命運～";
constexpr char kTextTurn[] = "#0040嘿～輪到你了！";
constexpr char kTextCast[] = "#0041天靈靈地靈靈～";

// panel[18] 帧号
constexpr int kFrameBg = 0;
constexpr int kFrameBall = 1;      // 165×213 @(241,140)
constexpr int kFrameConfirm = 2;   // 284×210 @(182,142)
constexpr int kFrameExit = 3;      // 60×35 @(286,188)
constexpr int kFrameExitSub = 4;   // 60×18 @(286,220) 擦除层
constexpr int kFrameSparkle = 5;   // 60×21 @(286,217)
constexpr int kFrameMsg = 8;       // 280×173 浮动消息框 @(320,384)
constexpr int kCondIconBase = 11;  // 条件图标帧 cond+11
constexpr int kPenIconBase = 22;   // 惩罚小图标帧 pen+22

constexpr int kIconCondX = 326;  // 条件图标显示位
constexpr int kIconCondY = 296;

// [RE 0x450F04] 解码一帧；播完返回 true（同 minigame/lottery 辅助）
bool decodeFlcOnce(FliDecoder& flc) {
    if (!flc.valid()) {
        return true;
    }
    if (!flc.nextFrame()) {
        return true;
    }
    return flc.frameIndex() >= flc.frameCount();
}

// [RE 0x45144F 帧呈现] FLC 当前帧落到 (x,y)（transparent=色键索引 0 色）
void blitFlcFrame(Surface& dst, const FliDecoder& flc, int x, int y, bool transparent) {
    if (!flc.valid()) {
        return;
    }
    const uint16_t key = flc.colorKey();
    const uint16_t* src = flc.pixels();
    for (int row = 0; row < flc.height(); ++row) {
        const int py = y + row;
        if (py < 0 || py >= Surface::kHeight) {
            continue;
        }
        for (int col = 0; col < flc.width(); ++col) {
            const int px = x + col;
            if (px < 0 || px >= Surface::kWidth) {
                continue;
            }
            const uint16_t c = src[static_cast<size_t>(row) * flc.width() + col];
            if (transparent && c == key) {
                continue;
            }
            dst.pixels()[static_cast<size_t>(py) * Surface::kWidth + px] = c;
        }
    }
}

// ===== 目标选择（0x431842）=====

// 取"最大值并列"列表（0..5 类条件；alive≠0 全玩家；上限 4）
std::vector<int> pickMaxPlayers(Application& app, bool (*valueOf)(Application&, int, int&)) {
    GameState& st = app.gameState();
    std::vector<int> out;
    int best = 0;
    for (int p = 0; p < st.playerCount; ++p) {
        if (st.players[p].alive == 0) {
            continue;
        }
        int v = 0;
        if (!valueOf(app, p, v) || v == 0) {
            continue;
        }
        if (v > best) {
            best = v;
            out.clear();
        }
        if (v == best && out.size() < 4) {
            out.push_back(p);
        }
    }
    return out;
}

// [RE 0x431842] magicSelectTargets：条件 → 目标名单（≤4，玩家索引）
std::vector<int> magicSelectTargets(Application& app, int cond) {
    GameState& st = app.gameState();
    std::vector<int> out;
    switch (cond) {
    case 0: { // 財產最多
        return pickMaxPlayers(app, [](Application& a, int p, int& v) {
            v = playerTotalAssets(a, p);
            return true;
        });
    }
    case 1: { // 土地最多（estate+corp 拥有数）
        return pickMaxPlayers(app, [](Application& a, int p, int& v) {            GameState& s = a.gameState();            v = 0;
            for (size_t i = 1; i < s.estates.size(); ++i) {
                if (s.estates[i].owner == p + 1) {
                    ++v;
                }
            }
            for (size_t i = 1; i < s.corps.size(); ++i) {
                if (s.corps[i].owner == p + 1) {
                    ++v;
                }
            }
            return true;
        });
    }
    case 2: { // 房屋最多（有建筑地块数）
        return pickMaxPlayers(app, [](Application& a, int p, int& v) {            GameState& s = a.gameState();            v = 0;
            for (size_t i = 1; i < s.estates.size(); ++i) {
                if (s.estates[i].owner == p + 1 && s.estates[i].level != 0) {
                    ++v;
                }
            }
            for (size_t i = 1; i < s.corps.size(); ++i) {
                if (s.corps[i].owner == p + 1 && s.corps[i].sub != 0) {
                    ++v;
                }
            }
            return true;
        });
    }
    case 3: { // 現金最多
        return pickMaxPlayers(app, [](Application& a, int p, int& v) {            GameState& s = a.gameState();            v = s.players[p].cash;
            return true;
        });
    }
    case 4: { // 存款最多
        return pickMaxPlayers(app, [](Application& a, int p, int& v) {            GameState& s = a.gameState();            v = s.players[p].bank;
            return true;
        });
    }
    case 5: { // 點券最多
        return pickMaxPlayers(app, [](Application& a, int p, int& v) {            GameState& s = a.gameState();            v = s.players[p].points;
            return true;
        });
    }
    default: { // 6..11 全部符合（travel&3 / 附身 / 性别）
        for (int p = 0; p < st.playerCount && static_cast<int>(out.size()) < 4; ++p) {
            if (st.players[p].alive == 0) {
                continue;
            }
            bool hit = false;
            switch (cond) {
            case 6: hit = (st.players[p].travel & 3) == 0; break;
            case 7: hit = (st.players[p].travel & 3) == 1; break;
            case 8: hit = (st.players[p].travel & 3) == 2; break;
            case 9: hit = st.players[p].cellTableIdx != 0; break;
            case 10: hit = st.players[p].byte20 != 0; break;
            case 11: hit = st.players[p].byte20 == 0; break;
            default: break;
            }
            if (hit) {
                out.push_back(p);
            }
        }
        return out;
    }
    }
}

// ===== 惩罚执行（0x431CAA）=====

// [RE 0x40C78C] turnToAdjacentCell：向后转（朝向反向 + 重选来路格）。
// 差异: 原版音效句柄 dword_4823F2 从未注册（静默）；NPC 分支（a1>=4 犯人表）P5。
void turnToAdjacentCell(GameState& st, int p) {
    Player& pl = st.players[p];
    pl.dir = static_cast<uint8_t>((pl.dir + 4) & 7);
    if (pl.cellEntId >= st.cellEnts.size()) {
        return;
    }
    const CellEnt& ce = st.cellEnts[pl.cellEntId];
    uint16_t cand[4];
    int n = 0;
    uint32_t mask = 0x40000000u;
    for (int i = 0; i < 4; ++i, mask >>= 1) {
        const uint16_t id = ce.exits[i];
        if (id != 0 && (mask & ce.occMask) == 0 && id != pl.prevCellEnt) {
            cand[n++] = id;
        }
    }
    pl.prevCellEnt = n != 0 ? cand[std::rand() % n] : 0;
}

// [RE 0x431CAA] applyMagicPenalty：对目标名单逐一执行惩罚 pen（entrant = 触发魔法屋玩家）
void applyMagicPenalty(Application& app, int pen, const std::vector<int>& targets, int entrant) {
    GameState& st = app.gameState();
    const int savedCur = st.currentPlayer;
    for (int t : targets) {
        if (t < 0 || t >= st.playerCount) {
            continue;
        }
        st.currentPlayer = t;
        Player& pl = st.players[t];
        // [RE 0x41906A(1)] 强制场景重绘（消息画在最新场景上）
        renderGameFrame(app);
        app.renderFrame();
        char text[256];
        std::snprintf(text, sizeof(text), "%s\n\n%s", playerNameNoSpace(st, t).c_str(),
                      kPenalties[pen + 1].name);
        showMessage(app, text, 1500);
        const uint16_t objId =
            pl.cellEntId < st.cellEnts.size() ? st.cellEnts[pl.cellEntId].special : 0;
        const bool onEstate = objId > 2000 && objId < 6000;
        switch (pen) {
        case 0: { // 變賣所有卡片 [RE 0x441F21]
            pl.points = static_cast<uint16_t>(pl.points + confiscateCards(app, t));
            eventAudioWait(app, 200); // [RE 0x431D50 sub_45285E(200)]
            break;
        }
        case 1: { // 抽取命運三張 [RE 0x431DBA]
            for (int i = 0; i < 3; ++i) {
                fateEvent(app);
            }
            break;
        }
        case 2: { // 立刻坐牢三天 [RE 0x431E19]
            addPlayerDebt(st, entrant, t, 90 * st.moneyMul);
            const int victim = resolvePenaltyTarget(app, t);
            if (victim != -1) {
                jailPlayer(app, victim, 3);
            }
            break;
        }
        case 3: { // 原地停留一回合 [RE 0x431ECA]
            pl.skipMove = static_cast<uint8_t>((pl.skipMove + 1) & 0x7F);
            break;
        }
        case 4: { // 存入所有現金 [RE 0x431F44]
            pl.bank += pl.cash;
            pl.cash = 0;
            break;
        }
        case 5: { // 就地加蓋房屋 [RE 0x431F6E]（stateFlags==0 且地块 2000..6000）
            if (pl.stateFlags == 0 && onEstate) {
                int tx = 0;
                int ty = 0;
                getObjectPosition(st, objId, tx, ty);
                focusView(app, tx, ty); // [RE 0x432050 refreshGameUi(坐标,0)]
                Surface& dst = app.surface();
                std::vector<uint16_t> pre(dst.pixels(),
                                          dst.pixels() +
                                              static_cast<size_t>(Surface::kWidth) *
                                                  Surface::kHeight);
                const int flags = angelUpgrade(app, objId); // [RE 0x40B110]
                std::memcpy(dst.pixels(), pre.data(), pre.size() * sizeof(uint16_t));
                app.renderFrame();
                playEventFlc(app, 553, 0, 40, 91, false, 44); // [RE 0x45144F 0x2C0001]
                if ((flags & 0x80) != 0) {
                    playEventFlc(app, 523, 0, 40, 90, false, 0); // [RE playGodBuildFlc]
                }
                resetView(app);
                playLine(app, t, "？？？..."); // [RE 0x44EF41 byte_46482F]
            }
            break;
        }
        case 6: { // 得一張卡片 [RE 0x441E12]
            const int card = drawFreeCard(st, t);
            if (card > 0) {
                char text2[256];
                char cardText[64];
                std::snprintf(cardText, sizeof(cardText), "得%s！", kCardNames[card]);
                std::snprintf(text2, sizeof(text2), "%s\n\n%s",
                              playerNameNoSpace(st, t).c_str(), cardText);
                showMessage(app, text2, 1500);
            }
            break;
        }
        case 7: { // 向後轉 [RE 0x40C78C]（仅 stateFlags==0）
            if (pl.stateFlags == 0) {
                turnToAdjacentCell(st, t);
                renderGameFrame(app); // [RE 0x4321DE refreshGameUi(0,0,1)]
                app.renderFrame();
                eventAudioWait(app, 500);
            }
            break;
        }
        case 8: { // 變賣所有道具 [RE 0x445B3F]
            pl.points = static_cast<uint16_t>(pl.points + confiscateItems(app, t));
            eventAudioWait(app, 200);
            break;
        }
        case 9: { // 就地拆除房屋 [RE 0x40AB4A]
            if (pl.stateFlags == 0 && onEstate) {
                int tx = 0;
                int ty = 0;
                getObjectPosition(st, objId, tx, ty);
                focusView(app, tx, ty);
                demolishAtObjId(app, objId, 0);
                playEventFlc(app, 529, 0, 40, 97, false, 38); // [RE 0x45144F 0x260001]
                resetView(app);
                playLine(app, t, "？？？...");
            }
            break;
        }
        case 10: { // 住院檢查三天 [RE 0x43EC3F]
            addPlayerDebt(st, entrant, t, 90 * st.moneyMul);
            const int victim = resolvePenaltyTarget(app, t);
            if (victim != -1) {
                hospitalizePlayer(app, victim, 3);
            }
            break;
        }
        case 11: { // 拍賣當格土地 [RE 0x43BDE5]
            if (pl.stateFlags == 0 && onEstate) {
                runAuction(app, t, objId, true);
            }
            break;
        }
        default:
            break;
        }
    }
    st.currentPlayer = savedCur; // [RE 0x432501]
}

// ===== 施法 UI（0x4325C2）=====

struct MagicCtx {
    Application* app = nullptr;
    UiImage sheet;              // panel[18]
    std::vector<uint8_t> pick;  // panel[19] RAW 640×480
    FliDecoder flc;             // panel[20]
    bool flcOk = false;
    bool flcStarted = false;
    int state = 0;              // byte_48C3A2
    int cond = 0;               // byte_48C3A3
    int countdown = 0;          // byte_48C3A1（state 3/4 倒计时）
    int hover = 0;              // byte_48C3A1（state 7 悬停项）
    bool skipAnim = false;      // byte_48C3A5
    int sparkleT = 0;           // byte_48C3A0
    std::vector<int> targets;   // g_magicTargets
    FloatMessage msg;
    std::vector<uint16_t> hoverBuf;  // 144×128 大图区背景
    int hoverBufX = 0;
    int hoverBufY = 0;
};

// 大图区左上角（中心 - 72/-64）
void bigRect(const MagicCtx& ui, int id, int& x, int& y) {
    x = kPenalties[id].bigX - 72;
    y = kPenalties[id].bigY - 64;
}

// 恢复大图区背景
void restoreBigBuf(MagicCtx& ui) {
    if (ui.hoverBuf.empty()) {
        return;
    }
    Surface& dst = ui.app->surface();
    for (int r = 0; r < 128; ++r) {
        std::memcpy(dst.pixels() + static_cast<size_t>(ui.hoverBufY + r) * Surface::kWidth +
                        ui.hoverBufX,
                    ui.hoverBuf.data() + static_cast<size_t>(r) * 144, 144 * sizeof(uint16_t));
    }
    ui.hoverBuf.clear();
}

bool magicHandler(const SDL_Event* event, void* user) {
    MagicCtx& ui = *static_cast<MagicCtx*>(user);
    Application& app = *ui.app;
    Surface& dst = app.surface();
    if (!event) { // [RE 0x401/0x405] 初始绘制 + 开场文本
        ui.state = 1;
        ui.msg.setup(ui.sheet, kFrameMsg, 320, 384, 0, 0, 14737632, 2105376);
        blitElementOpaque(dst, ui.sheet.frame(kFrameBg), 0, 0);
        blitElement(dst, ui.sheet.frame(kFrameBall), 241, 140, false);
        ui.msg.show(app, kTextIn);
        app.renderFrame();
        return true;
    }
    switch (event->type) {
    case kModalTimerEvent: {
        // [RE 0x432713] 状态 4：倒计时 → 随机条件
        if (ui.state == 4 && ui.countdown > 0 && --ui.countdown == 0) {
            do {
                ui.cond = std::rand() % 12;
                ui.targets = magicSelectTargets(app, ui.cond);
            } while (ui.targets.empty());
            blitElement(dst, ui.sheet.frame(kCondIconBase + ui.cond), kIconCondX, kIconCondY,
                        false);
            ui.state = 5;
            ui.msg.show(app, kCondNames[ui.cond]);
        }
        // 水晶球/EXIT 区闪烁（[RE 0x432894]：state 8 或（无消息且无闪烁计数）时跳过）
        if (ui.state != 8 && (ui.msg.active() || ui.sparkleT != 0)) {
            if (ui.sparkleT != 0) {
                if (ui.sparkleT == 1) {
                    blitElement(dst, ui.sheet.frame(kFrameExitSub), 286, 220, false);
                }
                --ui.sparkleT;
            } else if ((std::rand() >> 11) < 4) {
                if ((std::rand() & 1) != 0) {
                    blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBall), 286, 217, 45, 77, 60,
                                            21, false);
                } else {
                    blitElement(dst, ui.sheet.frame(kFrameSparkle), 286, 217, false);
                }
                ui.sparkleT = (std::rand() & 7) + 1;
            }
        }
        if (!ui.msg.active()) {
            // [RE 0x432830] 无消息时状态推进
            switch (ui.state) {
            case 1:
                ui.state = 2;
                ui.msg.show(app, kTextPick);
                break;
            case 2:
                ui.state = 3;
                ui.msg.show(app, kTextFate);
                break;
            case 3:
                ui.state = 4;
                blitElement(dst, ui.sheet.frame(kFrameExit), 286, 188, false);
                ui.countdown = ui.skipAnim ? 1 : 10;
                break;
            case 5:
                ui.state = 6;
                if (!ui.skipAnim) {
                    ui.msg.show(app, kTextTurn);
                }
                break;
            case 6:
                ui.state = 7;
                break;
            case 8: { // [RE 0x432A4C] 退出：播 panel[20] FLC（音效 59）→ postModalExit
                if (!ui.flcStarted) {
                    ui.flcStarted = true;
                    app.audio().playEffect(59);
                    app.renderFrame();
                    break;
                }
                // 每 tick 按 speedMs 折算播 1+ 帧（迁移: 原版阻塞 → tick 化）
                const int per = ui.flc.speedMs() > 0 ? (100 + ui.flc.speedMs() - 1) / ui.flc.speedMs() : 1;
                bool done = true;
                for (int i = 0; i < per; ++i) {
                    done = decodeFlcOnce(ui.flc);
                    if (done) {
                        break;
                    }
                }
                blitFlcFrame(dst, ui.flc, 0, 0, false);
                app.renderFrame();
                if (done) {
                    app.events().requestExit(ui.hover - 1);
                }
                break;
            }
            default:
                break;
            }
        }
        app.renderFrame();
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: { // [RE 0x432B57] 状态 7 悬停
        if (ui.state != 7) {
            return true;
        }
        const int mx = static_cast<int>(event->motion.x);
        const int my = static_cast<int>(event->motion.y);
        if (mx < 0 || mx >= 640 || my < 0 || my >= 480) {
            return true;
        }
        const int v = ui.pick[static_cast<size_t>(my) * 640 + mx];
        if (v == ui.hover) {
            return true;
        }
        if (ui.hover != 0 && ui.hover != 13) { // 擦旧小图标（帧0 patch 64×64）
            const int ox = kIconPos[ui.hover - 1][0] - 32;
            const int oy = kIconPos[ui.hover - 1][1] - 32;
            blitElementRegionOpaque(dst, ui.sheet.frame(kFrameBg), ox, oy, ox, oy, 64, 64, false);
        }
        restoreBigBuf(ui);
        ui.hover = v;
        if (v != 0 && v != 13) { // 新小图标 + 大图/名称
            blitElement(dst, ui.sheet.frame(kPenIconBase + v), kIconPos[v - 1][0],
                        kIconPos[v - 1][1], false);
            int bx = 0;
            int by = 0;
            bigRect(ui, v, bx, by);
            ui.hoverBuf.resize(static_cast<size_t>(144) * 128);
            for (int r = 0; r < 128; ++r) {
                std::memcpy(ui.hoverBuf.data() + static_cast<size_t>(r) * 144,
                            dst.pixels() + static_cast<size_t>(by + r) * Surface::kWidth + bx,
                            144 * sizeof(uint16_t));
            }
            ui.hoverBufX = bx;
            ui.hoverBufY = by;
            blitElement(dst, ui.sheet.frame(kPenalties[v].bigFrame), kPenalties[v].bigX,
                        kPenalties[v].bigY, false);
            app.text().setFont(14, 14737632, 2105376, 3, 0);
            app.text().drawText(dst, kPenalties[v].name, kPenalties[v].bigX, kPenalties[v].bigY,
                                2);
            app.audio().playEffect(39); // [RE g_magicHoverSound 0x4757E7={39}]
        }
        app.renderFrame();
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: { // [RE 0x432E97] 确认/跳过
        if (event->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        if (ui.state < 3) { // 提前跳过动画
            ui.skipAnim = true;
            ui.msg.finish(app);
            ui.state = 3;
            app.renderFrame();
            return true;
        }
        if (ui.state == 7) {
            if (ui.hover == 13) { // EXIT → 重看条件浮字
                ui.state = 6;
                ui.msg.show(app, kCondNames[ui.cond]);
            } else if (ui.hover != 0) { // 选定惩罚
                restoreBigBuf(ui);
                blitElement(dst, ui.sheet.frame(kFrameConfirm), 182, 142, false);
                blitElement(dst, ui.sheet.frame(kCondIconBase + ui.cond), kIconCondX, kIconCondY,
                            false);
                ui.state = 8;
                ui.msg.show(app, kTextCast);
            }
            app.renderFrame();
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: { // 右键跳过动画
        if (event->button.button == SDL_BUTTON_RIGHT && ui.state < 3) {
            ui.skipAnim = true;
            ui.msg.finish(app);
            ui.state = 3;
        }
        return true;
    }
    default:
        return true;
    }
}

} // namespace

// [RE 0x43380A] magicHouseVisit
void magicHouseVisit(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    std::vector<int> targets;
    int pen = -1;

    if (st.players[cur].alive == 1) {
        // ===== 人类：施法 UI =====
        MagicCtx ui;
        ui.app = &app;
        ui.skipAnim = st.settings[1] == 0; // [RE 0x43266E byte_497159^1]
        auto blob18 = st.panel.read(18);
        auto blob19 = st.panel.read(19);
        auto blob20 = st.panel.read(20);
        if (!blob18 || !ui.sheet.load(std::move(*blob18)) || ui.sheet.frameCount() < 35 ||
            !blob19 || blob19->size() < 640u * 480u || !blob20) {
            RICH4_LOGW("magicHouse: panel[18/19/20] unavailable (RE 0x43380A)");
            return;
        }
        ui.pick = std::move(*blob19);
        ui.flcOk = ui.flc.open(std::move(*blob20));
        app.audio().pushSceneMusic(7); // [RE 0x4338A0 musicPlayScene(7)]
        pen = runModal(app, &magicHandler, &ui, 100);
        app.audio().resumeSceneMusic();
        targets = ui.targets;
        if (pen < 0 || pen > 11) {
            return;
        }
    } else {
        // ===== AI/托管（[RE 0x43390B]）=====
        int cond = 0;
        do {
            cond = std::rand() % 12;
            targets = magicSelectTargets(app, cond);
        } while (targets.empty());
        for (int t : targets) {
            if (t == cur) {
                pen = 6; // 自己中招 → 强制"得一張卡片"
                break;
            }
        }
        if (pen < 0) {
            pen = std::rand() % 11; // 0..10
            if (pen == 6) {
                pen = 7; // 排除自肥档
            }
        }
        const char* condName = kCondNames[cond];
        if (condName[0] == '#') {
            condName += 5; // [RE 0x43398F] '#NNNN' 前缀跳 5 字节
        }
        char text[192];
        std::snprintf(text, sizeof(text), "%s\n\n%s", condName, kPenalties[pen + 1].name);
        showMessage(app, text, 1500);
    }

    applyMagicPenalty(app, pen, targets, cur);
    RICH4_LOGI("magicHouse: cond pen=%d targets=%zu entrant=%d (RE 0x43380A)", pen,
               targets.size(), cur);
}

} // namespace rich4
