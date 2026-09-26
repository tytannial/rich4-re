// 命運格事件（landingEvent case 3 → fateEvent 0x44DB81）
// 依据: 0x44DB81 主循环 + fateEventCheck 0x44BB4B 判定/载具改写 + 效果表 funcs_44DC44
//       (0x475EF0) 49 项；插画 word_475FB4 (0x475FB4) → data.mkf[477..516]（388×251 RGB555）；
//       逐条文本/判定/效果见 docs/reverse/functions/44db81-fate-events.md。
// 原版流程：抽 g_fateOrder[g_fatePos]（未触发则 ++pos 继续）→ panel.mkf[66] 画布（SMP
//   440×480，帧 0）+ 插画 @(25,44) + 效果(0) 文本 @(24,330)/头像 @(390,344) → 全屏 blt →
//   阻塞 1600ms → 效果(1)（数值/状态/高亮）→ 800ms → 释放。
// 重写：单 Surface 事件驱动（同 news_dialog），进入时绘制一次；runModal 每 tick renderFrame。
// 差异: 语音 sub_44EF41/sub_44F354 → TODO(P4)；byte_497324/5 统计并入 misc8A（与 damagePlayer
//       同源）；生日人类逐人顶部行演出简化为直接弹选卡面板。

#include "game/app/fate_event.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/app/bank_stay_dialog.h"
#include "game/app/card_bag_dialog.h"
#include "game/app/economy.h"
#include "game/app/event_common.h"
#include "game/app/event_stack.h"
#include "game/app/game_loop.h"
#include "game/app/map_objects.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/app/stock_system.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"
#include "game/render/blit.h"
#include "game/render/raw_bitmap.h"
#include "game/render/surface.h"
#include "game/render/text.h"
#include "game/render/ui_image.h"

namespace rich4 {
namespace {

// ===== 文本表（[RE 0x465915..0x465DBF] BIG5 原文；'#NNNN' 前缀由 TextRenderer 触发语音）=====
// 0..32 通用（#0185..#0217）+ 33..48 地图专属坐牢（#0218..#0233）
const char* const kFateTexts[49] = {
    /* 00 */ "#0185強制拆除房屋一棟",
    /* 01 */ "#0186強制徵收土地一處",
    /* 02 */ "#0187人頭被盜用冒貸%d元",
    /* 03 */ "#0188支票跳票\n銀行拒絕往來一個月",
    /* 04 */ "#0189侵入銀行電腦\n挪用其他人存款%d％",
    /* 05 */ "#0190今天是你生日\n向每人收取一張卡片",
    /* 06 */ "#0191強迫出國觀光%d天",
    /* 07 */ "#0192被外星人綁架%d天",
    /* 08 */ "#0193股票違約交割損失股票%d％",
    /* 09 */ "#0194變賣所有股票求現",
    /* 10 */ "#0195機車被偷遺失",
    /* 11 */ "#0196汽車撞電線桿全毀",
    /* 12 */ "#0197掉進水溝就醫%d天",
    /* 13 */ "#0198騎機車摔傷住院%d天",
    /* 14 */ "#0199行人闖越馬路罰款%d元",
    /* 15 */ "#0200騎機車未戴安全帽\n罰款%d元",
    /* 16 */ "#0201汽車超速罰款%d元",
    /* 17 */ "#0202請所有人吃大餐\n花費%d元",
    /* 18 */ "#0203亂丟垃圾罰款%d元",
    /* 19 */ "#0204你家小狗亂大小便\n罰款%d元",
    /* 20 */ "#0205在路邊撿到%d元",
    /* 21 */ "#0206在路邊撿到%d元",
    /* 22 */ "#0207在路邊撿到%d元",
    /* 23 */ "#0208遺失錢包損失%d元",
    /* 24 */ "#0209遺失錢包損失%d元",
    /* 25 */ "#0210意外獲得遺產%d元",
    /* 26 */ "#0211被倒會損失%d元",
    /* 27 */ "#0212發票中獎%d元",
    /* 28 */ "#0213發票中獎%d元",
    /* 29 */ "#0214發票中獎%d元",
    /* 30 */ "#0215付保險金%d元",
    /* 31 */ "#0216領取保險金%d元",
    /* 32 */ "#0217變賣所有卡片道具",
    /* 33 */ "#0218酒醉大鬧警局坐牢%d天",
    /* 34 */ "#0219防礙風化坐牢%d天",
    /* 35 */ "#0220走私毒品坐牢%d天",
    /* 36 */ "#0221販賣大補帖坐牢%d天",
    /* 37 */ "#0222酒醉大鬧警局坐牢%d天",
    /* 38 */ "#0223違法聚眾示威坐牢%d天",
    /* 39 */ "#0224獵捕保育動物坐牢%d天",
    /* 40 */ "#0225盜賣國寶坐牢%d天",
    /* 41 */ "#0226誘騙未成年少女拘役%d天",
    /* 42 */ "#0227防礙風化坐牢%d天",
    /* 43 */ "#0228走私毒品坐牢%d天",
    /* 44 */ "#0229施放毒氣坐牢%d天",
    /* 45 */ "#0230非法持有槍械坐牢%d天",
    /* 46 */ "#0231毆打警員坐牢%d天",
    /* 47 */ "#0232獵捕保育動物坐牢%d天",
    /* 48 */ "#0233盜賣國家機密坐牢%d天",
};

// [RE 0x475FB4] word_475FB4 插画索引（49 项 u16）：0..36 通用，33..36 被 4 地图复用，
//   37..48 = map1..3 的 33..36 改写目标（tableIdx = v>=33 ? 4*mapIndex+v : v）
const uint16_t kFateIllust[49] = {
    477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493,
    494, 495, 496, 497, 497, 497, 498, 498, 499, 500, 501, 501, 501, 502, 503, 504, 505,
    506, 507, 508, 505, 509, 510, 511, 512, 506, 507, 513, 514, 515, 510, 516,
};

// ===== 事件行为分类 =====

enum class FateKind : uint8_t {
    HouseDemolish,   // 0  强制拆除房屋（补偿 升级价×等级）
    LandRequisition, // 1  强制徵收土地（补偿 购地价，归公）
    FakeLoan,        // 2  人头被盗用冒贷（loan += 10000M）
    CheckBounce,     // 3  支票跳票（bankRefuseDays += 30）
    BankHack,        // 4  侵入银行电脑（他人存款 10% → 我）
    Birthday,        // 5  生日收卡
    Travel,          // 6  强迫出国观光 3 天
    Abducted,        // 7  被外星人绑架 3 天
    SellStockPct,    // 8  股票违约交割（卖 10%）
    SellAllStock,    // 9  变卖所有股票求现
    VehicleLost,     // 10/11 机车被偷 / 汽车撞毁（失去载具）
    Hospitalize,     // 12/13 摔伤住院 3 天
    Fine,            // 14..19/23/24/26/30 罚款
    Gain,            // 20..22/25/27..29/31 捡到/中奖
    Confiscate,      // 32 变卖所有卡片道具 → 点券
    Jail,            // 33..48 坐牢 N 天（地图专属）
};

// 幸运判定来源 [RE 0x44B896 sub_44B896(a3,a4)]
enum FateLuck : uint8_t {
    kLuckNone = 0,    // 无判定
    kLuckPenalty = 1, // (a3,a4)=(0,1) luckB：1=免付罰金 / 2=罰金加倍
    kLuckBonus = 2,   // (0,0)      luckB：1=獎金作廢 / 2=獎金加倍
    kLuckHeavy = 3,   // (1,1)      luckC：1=逃過此劫 / 2=倒霉加倍
};

struct FateDef {
    FateKind kind;
    int32_t amount;  // 金额（×moneyMul）/ 天数 / 百分比
    uint8_t luck;
    uint8_t piece;   // 头像帧号（原版实参 +12*piece：3=+36 休息 / 4=+48 受罚 / 5=+60 释放 / 2=+24）
};

const FateDef kFateDefs[49] = {
    /* 00 */ {FateKind::HouseDemolish, 0, kLuckNone, 3},
    /* 01 */ {FateKind::LandRequisition, 0, kLuckNone, 3},
    /* 02 */ {FateKind::FakeLoan, 10000, kLuckPenalty, 4},
    /* 03 */ {FateKind::CheckBounce, 0, kLuckPenalty, 4},
    /* 04 */ {FateKind::BankHack, 10, kLuckNone, 2},
    /* 05 */ {FateKind::Birthday, 0, kLuckNone, 5},
    /* 06 */ {FateKind::Travel, 3, kLuckHeavy, 3},
    /* 07 */ {FateKind::Abducted, 3, kLuckHeavy, 4},
    /* 08 */ {FateKind::SellStockPct, 10, kLuckPenalty, 4},
    /* 09 */ {FateKind::SellAllStock, 0, kLuckPenalty, 3},
    /* 10 */ {FateKind::VehicleLost, 0, kLuckHeavy, 4},
    /* 11 */ {FateKind::VehicleLost, 0, kLuckHeavy, 4},
    /* 12 */ {FateKind::Hospitalize, 3, kLuckHeavy, 4},
    /* 13 */ {FateKind::Hospitalize, 3, kLuckHeavy, 4},
    /* 14 */ {FateKind::Fine, 3000, kLuckPenalty, 3},
    /* 15 */ {FateKind::Fine, 3000, kLuckPenalty, 3},
    /* 16 */ {FateKind::Fine, 3000, kLuckPenalty, 3},
    /* 17 */ {FateKind::Fine, 6000, kLuckPenalty, 4},
    /* 18 */ {FateKind::Fine, 600, kLuckPenalty, 4},
    /* 19 */ {FateKind::Fine, 1500, kLuckPenalty, 3},
    /* 20 */ {FateKind::Gain, 1000, kLuckBonus, 2},
    /* 21 */ {FateKind::Gain, 2000, kLuckBonus, 5},
    /* 22 */ {FateKind::Gain, 3000, kLuckBonus, 5},
    /* 23 */ {FateKind::Fine, 1000, kLuckPenalty, 3},
    /* 24 */ {FateKind::Fine, 2000, kLuckPenalty, 4},
    /* 25 */ {FateKind::Gain, 10000, kLuckBonus, 5},
    /* 26 */ {FateKind::Fine, 8000, kLuckPenalty, 4},
    /* 27 */ {FateKind::Gain, 4000, kLuckBonus, 2},
    /* 28 */ {FateKind::Gain, 6000, kLuckBonus, 5},
    /* 29 */ {FateKind::Gain, 8000, kLuckBonus, 5},
    /* 30 */ {FateKind::Fine, 5000, kLuckPenalty, 3},
    /* 31 */ {FateKind::Gain, 5000, kLuckBonus, 5},
    /* 32 */ {FateKind::Confiscate, 0, kLuckHeavy, 3},
    /* 33 */ {FateKind::Jail, 3, kLuckHeavy, 4},
    /* 34 */ {FateKind::Jail, 5, kLuckHeavy, 4},
    /* 35 */ {FateKind::Jail, 7, kLuckHeavy, 4},
    /* 36 */ {FateKind::Jail, 9, kLuckHeavy, 4},
    /* 37 */ {FateKind::Jail, 3, kLuckHeavy, 4},
    /* 38 */ {FateKind::Jail, 5, kLuckHeavy, 4},
    /* 39 */ {FateKind::Jail, 7, kLuckHeavy, 4},
    /* 40 */ {FateKind::Jail, 9, kLuckHeavy, 4},
    /* 41 */ {FateKind::Jail, 3, kLuckHeavy, 4},
    /* 42 */ {FateKind::Jail, 5, kLuckHeavy, 4},
    /* 43 */ {FateKind::Jail, 7, kLuckHeavy, 4},
    /* 44 */ {FateKind::Jail, 9, kLuckHeavy, 4},
    /* 45 */ {FateKind::Jail, 3, kLuckHeavy, 4},
    /* 46 */ {FateKind::Jail, 5, kLuckHeavy, 4},
    /* 47 */ {FateKind::Jail, 7, kLuckHeavy, 4},
    /* 48 */ {FateKind::Jail, 9, kLuckHeavy, 4},
};

constexpr int kFateShowMs = 1600; // [RE 0x44DD49] sub_4544F6(1600)
constexpr int kFateTailMs = 800;  // [RE 0x44DD80] sub_4528B9(800)

// ===== 上下文 =====

struct FateCtx {
    Application* app = nullptr;
    UiImage panel;              // panel.mkf[66]（SMP 2 帧 440×480，帧 0）
    std::vector<uint16_t> art;  // 插画 data.mkf[kFateIllust[idx]]（388×251 RGB555）
    int artW = 0;
    int artH = 0;
    int eventIdx = -1;    // 表索引（0..48）
    int targetEstate = 0; // id 0/1：phase0 选中的 estate 索引
    int startMs = 0;
    bool phase1Done = false;
};

// ===== 幸运判定（[RE 0x44B896]）=====

struct FateLuckResult {
    int code = 0; // 0=无事 / 1=幸运（免罚·逃过·作废） / 2=倒霉（加倍）
    char text[128] = {};
};

// [RE off_47ED76] 挂身神明名（cellTableIdx 1..15；0=无神明不会走到消息分支）
const char* attachedGodName(const GameState& st, int p) {
    const uint8_t idx = st.players[p].cellTableIdx;
    return idx > 0 && idx < 19 ? kObjectNames[idx] : "";
}

// [RE 0x44B896] sub_44B896(sel)：luck >100 幸运 / 0..50 无事 / 50..100 随机 / <0 倒霉
FateLuckResult fateLuckRoll(const GameState& st, int p, uint8_t sel) {
    FateLuckResult r;
    const Player& pl = st.players[p];
    const char* god = attachedGodName(st, p);
    auto set = [&](int code, const char* fmt) {
        r.code = code;
        std::snprintf(r.text, sizeof r.text, fmt, god);
    };
    if (sel == kLuckHeavy) {
        const int luck = pl.luckC;
        if (luck > 100) {
            set(1, "%s保佑\n\n逃過此劫！"); // [RE aS_36 0x4658e7]
        } else if (luck < 0) {
            set(2, "%s作祟\n\n倒霉加倍！"); // [RE aS_35 0x4658d4]
        } else if (luck > 50 && (std::rand() & 1) != 0) {
            set(1, "%s保佑\n\n逃過此劫！");
        }
        return r;
    }
    if (sel == kLuckPenalty) {
        const int luck = pl.luckB;
        if (luck > 100) {
            set(1, "%s保佑\n\n免付罰金！"); // [RE aS_34 0x4658c1]
        } else if (luck < 0) {
            set(2, "%s作祟\n\n罰金加倍！"); // [RE aS_33 0x4658ae]
        } else if (luck > 50 && (std::rand() & 1) != 0) {
            set(1, "%s保佑\n\n免付罰金！");
        }
        return r;
    }
    if (sel == kLuckBonus) {
        const int luck = pl.luckB;
        if (luck > 100) {
            set(2, "%s保佑\n\n獎金加倍！"); // [RE aS_31 0x465888]
        } else if (luck < 0) {
            set(1, "%s作祟\n\n獎金作廢！"); // [RE aS_32 0x46589b]
        } else if (luck > 50 && (std::rand() & 1) != 0) {
            set(2, "%s保佑\n\n獎金加倍！");
        }
        return r;
    }
    return r;
}

// ===== 特殊效果辅助 =====

// [RE 0x40D375] 强迫出国/外星人绑架（首次：清占用 + 停载具 + 罚款 2000×M×天 + FLC）
//   kind: false=出國 FLC 558 @(0,40) sound 96 switchFrame 20；true=綁架 FLC 533 sound 84 switchFrame 28
void fateStartTravelState(Application& app, Player& pl, int player, int days, bool abducted) {
    GameState& st = app.gameState();
    const uint8_t v9 =
        static_cast<uint8_t>((abducted ? 1u : 0u) << 6) | static_cast<uint8_t>(days & 0x3F);
    const uint8_t b1 = static_cast<uint8_t>((pl.stateFlags >> 8) & 0xFF);
    if (b1 != 0) { // [RE 0x40D3B0] 已有状态：天数累加（含 bit6 标记）
        const uint8_t next = static_cast<uint8_t>((b1 & 0x3F) + v9);
        pl.stateFlags = (pl.stateFlags & 0xFFFF00FFu) | (static_cast<uint32_t>(next) << 8);
        return;
    }
    // [RE 0x40D761] 设置新状态前清旧状态（監獄/醫院标志 + stateFlags）
    st.jailFlags[player] = 0;
    st.hospitalFlags[player] = 0;
    pl.stateFlags = 0;
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) {
        st.cellEnts[pl.cellEntId].occMask &= ~(256u << player); // [RE 0x40D399] 清占用
    }
    pl.travel = 0;                       // [RE 0x44F2C2] 停载具
    pl.diceCount = 1;
    loadWalkResources(st, player);       // [RE 0x40D3E8]
    insurancePayout(app, player, 2000 * days * st.moneyMul); // [RE 0x44BA63]
    pl.byte66 = static_cast<uint8_t>(pl.byte66 + days);      // [RE byte_496BAA 累计]
    pl.stateFlags = (pl.stateFlags & 0xFFFF00FFu) | (static_cast<uint32_t>(v9) << 8);
    const int flc = abducted ? 533 : 558;
    const int sound = abducted ? 84 : 96;
    const int switchFrame = abducted ? 28 : 20;
    playEventFlc(app, flc, 0, 40, sound, false, switchFrame);
    if (pl.cellEntId > 0 && pl.cellEntId < st.cellEnts.size()) { // [RE sprite = 当前格坐标]
        pl.spriteX = st.cellEnts[pl.cellEntId].x;
        pl.spriteY = st.cellEnts[pl.cellEntId].y;
    }
    RICH4_LOGI("fate: p=%d %s days=%d (RE 0x40D375)", player, abducted ? "abducted" : "travel",
               days);
}

// [RE 0x44CA46/0x44CB53] 载具损毁（命运 10/11）：travel=0 + 回收礼物流通池 + 重载行走
void fateLoseVehicle(Application& app, int player) {
    GameState& st = app.gameState();
    Player& pl = st.players[player];
    const int t = pl.travel & 3;
    if (t == 1 || t == 2) {
        ++st.misc8A[3 + t]; // [RE 0x497324/25] 机车/汽车回收计数
    }
    pl.travel = 0;
    pl.diceCount = 1;
    loadWalkResources(st, player);
    RICH4_LOGI("fate: p=%d vehicle lost (RE 0x44CA46)", player);
}

// [RE 0x44192A AI 分支 sub_41E6F2(1)] 随机取目标玩家一张道具（返回 id，0=无）
int fateTakeRandomItem(Application& app, int target) {
    GameState& st = app.gameState();
    int ids[13];
    int n = 0;
    for (int i = 0; i < 13; ++i) {
        if (st.itemStock[15 * target + i] != 0) {
            ids[n++] = i + 1;
        }
    }
    if (n == 0) {
        return 0;
    }
    return ids[std::rand() % n];
}

// [RE 0x44BFB1/0x44BE16 phase0] 随机选自有地产（requireBuilt：+26 等级 !=0 / ==0）
int fatePickEstate(const GameState& st, int cur, bool requireBuilt) {
    std::vector<int> list;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& es = st.estates[i];
        if (es.owner == cur + 1 && ((es.level != 0) == requireBuilt)) {
            list.push_back(static_cast<int>(i));
        }
    }
    return pickRandom(list);
}

// ===== 绘制 =====

void fateDrawBase(FateCtx& ctx) {
    Application& app = *ctx.app;
    Surface& dst = app.surface();
    if (ctx.panel.frameCount() > 0) {
        blitElementOpaque(dst, ctx.panel.frame(0), 0, 0); // [RE blitElementFullscreen 440×480]
    }
    if (!ctx.art.empty()) {
        blitOpaqueRgb(dst, ctx.art.data(), ctx.artW, ctx.artH, 25, 44); // [RE blitBackground]
    }
}

bool fateTextHasAmount(FateKind k) {
    switch (k) {
        case FateKind::HouseDemolish:
        case FateKind::LandRequisition:
        case FateKind::CheckBounce:
        case FateKind::Birthday:
        case FateKind::SellAllStock:
        case FateKind::VehicleLost:
        case FateKind::Confiscate:
            return false;
        default:
            return true;
    }
}

int32_t fateTextParam(const FateDef& d, int32_t moneyMul) {
    switch (d.kind) {
        case FateKind::Fine:
        case FateKind::Gain:
        case FateKind::FakeLoan:
            return d.amount * moneyMul;
        default:
            return d.amount; // 天数 / 百分比
    }
}

// 事件文本 + 角色头像（phase0；[RE 各效果函数 a3==0 分支 drawText(24,330) + blit(390,344)])
void fateDrawPhase0(FateCtx& ctx) {
    Application& app = *ctx.app;
    const GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateDef& def = kFateDefs[ctx.eventIdx];
    if (def.kind == FateKind::HouseDemolish) { // [RE 0x44BE16] phase0 随机选目标
        ctx.targetEstate = fatePickEstate(st, cur, true);
    } else if (def.kind == FateKind::LandRequisition) { // [RE 0x44BFB1]
        ctx.targetEstate = fatePickEstate(st, cur, false);
    }
    char text[192];
    if (fateTextHasAmount(def.kind)) {
        std::snprintf(text, sizeof text, kFateTexts[ctx.eventIdx],
                      fateTextParam(def, st.moneyMul));
    } else {
        std::snprintf(text, sizeof text, "%s", kFateTexts[ctx.eventIdx]);
    }
    drawEventText(app, text, 24, 330);
    drawPieceFrame(app, cur, def.piece, 390, 344);
}

// ===== 效果阶段 1（[RE funcs_44DC44 各函数 a3!=0 分支]）=====

void fateApplyFine(FateCtx& ctx, const FateDef& def) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, def.luck);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    int32_t amt = def.amount * st.moneyMul;
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
        amt *= 2;
    }
    RICH4_LOGI("fate fine: idx=%d p=%d code=%d base=%d mul=%d amt=%d cash=%d bank=%d (RE 0x44CD99)",
               ctx.eventIdx, cur, lr.code, def.amount, st.moneyMul, amt,
               st.players[cur].cash, st.players[cur].bank);
    transferMoney(app, cur, -1, amt, 0); // [RE sub_41D2C6(cur,-1,amt,0)]
    if (st.players[cur].alive != 0 && st.sceneRequest == 0) {
        insurancePayout(app, cur, amt); // [RE sub_44BA63]
        // [TODO(P4)] 语音 sub_44F42D
    }
    RICH4_LOGI("fate fine after: p=%d cash=%d bank=%d insurance=%u (RE 0x44CD99)", cur,
               st.players[cur].cash, st.players[cur].bank, st.players[cur].insuranceDays);
}

void fateApplyGain(FateCtx& ctx, const FateDef& def) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, def.luck);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    int32_t amt = def.amount * st.moneyMul;
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
        amt *= 2;
    }
    addMoney(app, cur, amt, true); // [RE addMoney(cur, amt, 1)]
    // [TODO(P4)] 语音 sub_44F354
}

void fateApplyHouseDemolish(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (ctx.targetEstate <= 0 || ctx.targetEstate >= static_cast<int>(st.estates.size())) {
        return;
    }
    Estate& es = st.estates[ctx.targetEstate];
    focusView(app, es.x, es.y); // [RE 0x44BECF refreshGameUi(*v5, v5[1], 2)]
    addMoney(app, cur, static_cast<int32_t>(es.priceBase) * es.level, true); // [RE +30×等级]
    es.level = 0;
    es.type = 0;
    blinkSingleObj(app, ctx.targetEstate + 2000); // [RE rebuildPickBuffer/markPickBuffer/highlightBlink]
    resetView(app);
}

void fateApplyLandRequisition(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (ctx.targetEstate <= 0 || ctx.targetEstate >= static_cast<int>(st.estates.size())) {
        return;
    }
    Estate& es = st.estates[ctx.targetEstate];
    focusView(app, es.x, es.y);
    addMoney(app, cur, es.priceAdd, true); // [RE +28 购地价]
    es.owner = 0;
    es.expireDate = 0;
    buildMiniMapMarks(app);                        // [RE rebuildMiniMap(0)]
    blinkSingleObj(app, ctx.targetEstate + 2000);  // [RE highlightBlink]
    resetView(app);
}

void fateApplyFakeLoan(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckPenalty);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    int32_t amt = 10000 * st.moneyMul;
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
        amt *= 2;
    }
    st.players[cur].loan += amt;           // [RE g_playerLoan += dword_48C5B4]
    setLoanDate(app, cur);                 // [RE 0x433B7E] 首次贷款设 90 天到期日（跳过特殊日期）
    insurancePayout(app, cur, amt);        // [RE sub_44BA63]
    RICH4_LOGI("fate: fake loan p=%d +%d loan=%d loanDate=%u (RE 0x44C0E8)", cur, amt,
               st.players[cur].loan, st.players[cur].loanDate);
}

void fateApplyCheckBounce(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckPenalty);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    st.players[cur].bankRefuseDays =
        static_cast<uint8_t>(st.players[cur].bankRefuseDays + 30); // [RE byte_496BA3 += 30]
    RICH4_LOGI("fate: check bounce p=%d refuse+30 (RE 0x44C229)", cur);
}

void fateApplyBankHack(FateCtx& ctx, const FateDef& def) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int n = st.playerCount < 8 ? st.playerCount : 8;
    for (int i = 0; i < n; ++i) {
        if (i == cur || st.players[i].alive == 0 || st.players[i].bank == 0) {
            continue;
        }
        // [RE dword_48C5B0 = 10（phase0 设置）] 比例 = def.amount％
        const int32_t amt =
            static_cast<int32_t>(st.players[i].bank * (def.amount / 100.0));
        transferMoney(app, i, cur, amt, 4); // [RE sub_41D2C6(i, cur, amt, 4)]
        RICH4_LOGI("fate bank hack: p=%d bank=%d -> %d take=%d%% amt=%d (RE 0x44C2C2)", i,
                   st.players[i].bank, cur, def.amount, amt);
    }
}

void fateApplyBirthday(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const int n = st.playerCount < 4 ? st.playerCount : 4;
    for (int i = 0; i < n; ++i) {
        if (i == cur || st.players[i].alive == 0 || cardBagCount(st, i) == 0) {
            continue;
        }
        if (st.players[cur].alive == 1) {
            // [RE 0x44192A] 人类：弹出目标玩家卡包/道具包选择面板
            const int sel = selectCardOrItemFromPlayerDialog(app, i);
            if (sel == 0) {
                continue; // 取消
            }
            if ((sel & 0x8000) == 0) {
                cardBagRemove(st, i, sel);
                giveCardToBag(st, cur, sel);
            } else {
                const int itemId = sel & 0x7FFF;
                takePlayerItem(st, i, itemId);
                givePlayerItem(st, cur, itemId);
            }
        } else {
            // [RE sub_41E6F2(1)] AI：随机拿一张卡（无卡则道具）
            const int card = discardRandomCard(app, i);
            if (card != 0) {
                giveCardToBag(st, cur, card);
            } else {
                const int itemId = fateTakeRandomItem(app, i);
                if (itemId != 0) {
                    takePlayerItem(st, i, itemId);
                    givePlayerItem(st, cur, itemId);
                }
            }
        }
    }
    // [TODO(P4)] 语音（收到卡时）
}

void fateApplyTravel(FateCtx& ctx, bool abducted) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckHeavy);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    int days = 3;
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
        days *= 2;
    }
    const int t = resolvePenaltyTarget(app, cur); // [RE sub_441210]
    if (t == -1) {
        return;
    }
    fateStartTravelState(app, st.players[t], t, days, abducted);
}

void fateApplyVehicleLost(FateCtx& ctx, int idx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckHeavy);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
    }
    fateLoseVehicle(app, cur); // [RE 0x44CA46/0x44CB53]
    RICH4_LOGI("fate: vehicle lost idx=%d (RE 0x44C%s)", idx, idx == 10 ? "A46" : "B53");
}

void fateApplySellStockPct(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckPenalty);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    for (int i = 0; i < 12; ++i) {
        const int32_t shares = st.playerShares[cur][i];
        if (shares <= 0) {
            continue;
        }
        const int count = static_cast<int>(shares * (10 / 100.0)); // [RE dword_48C5B4=10]
        if (count > 0) {
            sellStock(app, cur, i, count, false); // [RE sellStock(cur,i,count,0)]
        }
    }
    // [RE sub_436B0A(0)] 面板刷新由每帧重绘替代
}

void fateApplySellAllStock(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckPenalty);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    for (int i = 0; i < 12; ++i) {
        const int32_t shares = st.playerShares[cur][i];
        if (shares > 0) {
            sellStock(app, cur, i, shares, true); // [RE sellStock(cur,i,shares,1)]
        }
    }
    // [RE sub_41D433(cur)] 面板刷新由每帧重绘替代；语音 TODO(P4)
}

void fateApplyConfiscate(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckHeavy);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    const int pts = confiscateItems(app, cur); // [RE g_playerPoints += sub_445B3F]
    const int pts2 = confiscateCards(app, cur); // [RE g_playerPoints += sub_441F21]
    st.players[cur].points =
        static_cast<uint16_t>(st.players[cur].points + pts + pts2);
    RICH4_LOGI("fate: confiscate p=%d +%d points (RE 0x44D677)", cur, pts + pts2);
}

void fateApplyHospitalize(FateCtx& ctx) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckHeavy);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    int days = 3;
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
        days *= 2;
    }
    const int t = resolvePenaltyTarget(app, cur); // [RE sub_441210]
    if (t == -1) {
        return;
    }
    damagePlayer(app, t);            // [RE damagePlayer(v4)]
    hospitalizePlayer(app, t, days); // [RE hospitalizePlayer]
}

void fateApplyJail(FateCtx& ctx, const FateDef& def) {
    Application& app = *ctx.app;
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    const FateLuckResult lr = fateLuckRoll(st, cur, kLuckHeavy);
    if (lr.code == 1) {
        showMessage(app, lr.text, 1500);
        return;
    }
    int days = def.amount;
    if (lr.code == 2) {
        showMessage(app, lr.text, 1500);
        days *= 2;
    }
    const int t = resolvePenaltyTarget(app, cur); // [RE sub_441210]
    if (t == -1) {
        return;
    }
    jailPlayer(app, t, days); // [RE jailPlayer(days, t, days)]
}

void fateApplyPhase1(FateCtx& ctx) {
    const FateDef& def = kFateDefs[ctx.eventIdx];
    switch (def.kind) {
        case FateKind::Fine:
            fateApplyFine(ctx, def);
            break;
        case FateKind::Gain:
            fateApplyGain(ctx, def);
            break;
        case FateKind::HouseDemolish:
            fateApplyHouseDemolish(ctx);
            break;
        case FateKind::LandRequisition:
            fateApplyLandRequisition(ctx);
            break;
        case FateKind::FakeLoan:
            fateApplyFakeLoan(ctx);
            break;
        case FateKind::CheckBounce:
            fateApplyCheckBounce(ctx);
            break;
        case FateKind::BankHack:
            fateApplyBankHack(ctx, def);
            break;
        case FateKind::Birthday:
            fateApplyBirthday(ctx);
            break;
        case FateKind::Travel:
            fateApplyTravel(ctx, false);
            break;
        case FateKind::Abducted:
            fateApplyTravel(ctx, true);
            break;
        case FateKind::SellStockPct:
            fateApplySellStockPct(ctx);
            break;
        case FateKind::SellAllStock:
            fateApplySellAllStock(ctx);
            break;
        case FateKind::VehicleLost:
            fateApplyVehicleLost(ctx, ctx.eventIdx);
            break;
        case FateKind::Hospitalize:
            fateApplyHospitalize(ctx);
            break;
        case FateKind::Confiscate:
            fateApplyConfiscate(ctx);
            break;
        case FateKind::Jail:
            fateApplyJail(ctx, def);
            break;
    }
}

// ===== 模态流程 =====

bool fateHandler(const SDL_Event* event, void* user) {
    FateCtx& ctx = *static_cast<FateCtx*>(user);
    Application& app = *ctx.app;
    if (event == nullptr) {
        // 模态进入：面板/插画 + 效果阶段 0
        ctx.startMs = static_cast<int>(SDL_GetTicks());
        fateDrawBase(ctx);
        fateDrawPhase0(ctx);
        return true;
    }
    if (event->type == kModalTimerEvent) {
        const int elapsed = static_cast<int>(SDL_GetTicks()) - ctx.startMs;
        if (!ctx.phase1Done && elapsed >= kFateShowMs) {
            ctx.phase1Done = true;
            // [RE 各效果函数 phase1 开头 refreshGameUi(0,0,3)] 相位1 前先切回场景：
            //   原版效果函数先重绘地图（覆盖全屏事件面板），再播 FLC（警车/护士/飞碟）与消息；
            //   重写单 Surface 必须显式全量重绘，否则动画画在事件面板上（实机反馈 2026-09-26）。
            renderGameFrame(app);
            app.renderFrame();
            fateApplyPhase1(ctx); // [RE funcs[idx](1)]
            // [RE 各效果函数尾部 refreshGameUi(0,0,1)] 效果（转账/状态/保险消息）后主动重绘：
            //   否则 800ms 尾部期间场景/右侧资金面板停留在变化前（实机反馈 2026-09-26）
            renderGameFrame(app);
            app.renderFrame();
        } else if (ctx.phase1Done && elapsed >= kFateShowMs + kFateTailMs) {
            app.events().requestExit(0);
        }
        return true;
    }
    return true; // [RE sub_4544F6(1600)] 阻塞期间不响应输入
}

void fateRun(Application& app, int tableIdx) {
    GameState& st = app.gameState();
    FateCtx ctx;
    ctx.app = &app;
    ctx.eventIdx = tableIdx;
    // panel.mkf[66]（SMP 2 帧 440×480，帧 0 = 命运底图，与新闻共用）
    if (auto blob = st.panel.read(66)) {
        ctx.panel.load(std::move(*blob));
    } else {
        RICH4_LOGW("fate: panel.mkf[66] unavailable (RE 0x44DB9D)");
    }
    // data.mkf[kFateIllust[tableIdx]]（388×251 RGB555 无头位图）
    const int artIdx = kFateIllust[tableIdx];
    if (auto blob = st.data.read(static_cast<size_t>(artIdx))) {
        const RawBitmap raw = decodeRawBitmap(*blob);
        if (raw.valid()) {
            ctx.artW = raw.width;
            ctx.artH = raw.height;
            ctx.art.assign(raw.pixels, raw.pixels + static_cast<size_t>(raw.width) * raw.height);
        } else {
            RICH4_LOGW("fate: data.mkf[%d] raw bitmap invalid (RE 0x44DC69)", artIdx);
        }
    } else {
        RICH4_LOGW("fate: data.mkf[%d] unavailable (RE 0x44DC69)", artIdx);
    }
    RICH4_LOGI("fate event: idx %d text=%s (RE 0x44DB81)", tableIdx, kFateTexts[tableIdx]);
    runModal(app, &fateHandler, &ctx, 16);
}

// ===== 触发判定（[RE 0x44BB4B fateEventCheck]）=====

bool fateEventCheck(const GameState& st, int& v) {
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return false;
    }
    const Player& pl = st.players[cur];
    const int n = st.playerCount < 8 ? st.playerCount : 8;
    if (v < 12) {
        if (v >= 8) {
            if (v < 10) { // 8/9：有股票持仓
                for (int i = 0; i < 12; ++i) {
                    if (st.playerShares[cur][i] != 0) {
                        return true;
                    }
                }
                return false;
            }
            if (v <= 10) { // 10：机车/汽车（汽车 → 11）
                if (pl.travel != 1 && pl.travel != 2) {
                    return false;
                }
                if (pl.travel == 2) {
                    v = 11;
                }
                return true;
            }
            // 11：机车/汽车（机车 → 10）
            if (pl.travel != 1 && pl.travel != 2) {
                return false;
            }
            if (pl.travel == 1) {
                v = 10;
            }
            return true;
        }
        if (v != 0) {
            if (v > 1) {
                if (v != 5) {
                    return true;
                }
                // 5：其他玩家卡包总数 > 0
                int total = 0;
                for (int i = 0; i < n; ++i) {
                    if (i != cur) {
                        total += cardBagCount(st, i);
                    }
                }
                return total != 0;
            }
            // 1：自有未建（+26==0）地产
            for (size_t i = 1; i < st.estates.size(); ++i) {
                if (st.estates[i].owner == cur + 1 && st.estates[i].level == 0) {
                    return true;
                }
            }
            return false;
        }
        // 0：自有已建（+26!=0）地产
        for (size_t i = 1; i < st.estates.size(); ++i) {
            if (st.estates[i].owner == cur + 1 && st.estates[i].level != 0) {
                return true;
            }
        }
        return false;
    }
    if (v <= 12) { // 12：步行/机车（机车 → 13）
        if (pl.travel > 1) {
            return false;
        }
        if (pl.travel == 1) {
            v = 13;
        }
        return true;
    }
    if (v < 16) {
        if (v >= 14) {
            if (v == 14) { // 14：步行/机车/汽车（机车 → 15 / 汽车 → 16）
                if (pl.travel > 2) {
                    return false;
                }
                if (pl.travel == 1) {
                    v = 15;
                } else if (pl.travel == 2) {
                    v = 16;
                }
                return true;
            }
            // 15：步行 → 14 / 汽车 → 16
            if (pl.travel > 2) {
                return false;
            }
            if (pl.travel == 0) {
                v = 14;
            } else if (pl.travel == 2) {
                v = 16;
            }
            return true;
        }
        // 13：步行/机车（步行 → 12）
        if (pl.travel > 1) {
            return false;
        }
        if (pl.travel == 0) {
            v = 12;
        }
        return true;
    }
    if (v <= 16) { // 16：步行 → 14 / 机车 → 15
        if (pl.travel > 2) {
            return false;
        }
        if (pl.travel == 0) {
            v = 14;
        } else if (pl.travel == 1) {
            v = 15;
        }
        return true;
    }
    // 17..32 恒触发；33..36 仅基础 4 图（gameMode==0）；其余（>=37）恒触发
    if (v == 33 || v == 34 || v == 35 || v == 36) {
        return st.gameMode == 0;
    }
    return true;
}

} // namespace

// ===== 入口 =====

void fateEvent(Application& app) {
    GameState& st = app.gameState();
    // [RE 0x44DBC8] 抽取：取 g_fateOrder[g_fatePos] → check（可改写）→ ++pos（无论触发）→
    //   未触发继续；触发即显示。正常数据必有可触发事件。
    int tableIdx = -1;
    for (int guard = 0; guard < 37; ++guard) {
        const int v = st.fateOrder[st.fatePos];
        int out = v;
        const bool fire = fateEventCheck(st, out);
        if (++st.fatePos == 37) {
            st.fatePos = 0;
        }
        if (fire) {
            tableIdx = (out >= 33) ? 4 * st.mapIndex + out : out;
            break;
        }
    }
    if (tableIdx < 0 || tableIdx >= 49) {
        // [NEW] 防御：37 条全不可触发（原版此处死循环）→ 强制显示当前条
        int out = st.fateOrder[st.fatePos];
        tableIdx = (out >= 33) ? 4 * st.mapIndex + out : out;
        tableIdx = tableIdx < 0 ? 0 : (tableIdx >= 49 ? 32 : tableIdx);
        RICH4_LOGW("fate: no fireable event in 37 draws, forcing idx %d (RE 0x44DB81)",
                   tableIdx);
    }
    fateRun(app, tableIdx);
}

void fateDebugFire(Application& app, int idx) {
    if (idx < 0) {
        fateEvent(app);
        return;
    }
    fateRun(app, idx >= 49 ? 48 : idx);
}

} // namespace rich4
