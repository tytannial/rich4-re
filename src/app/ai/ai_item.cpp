#include "game/app/ai_item.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "game/app/economy.h"
#include "game/app/map_objects.h"
#include "game/app/map_tables.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/debug_hooks.h"
#include "game/game_state.h"

namespace rich4 {

// [RE 0x40B221] 前方 steps 步预测路径（word_48B8B4）→ out[steps]（≤8，0=无效）。
//   从玩家当前格出发逐步选出口：排除来路 prevCellEnt、排除 occMask bit30-33 占用出口；
//   多出口 → 随机选并置 *hasBranch（原版返回位 v12——路径不确定）；无出口 → 回填来路。
//   候选域核心（2026-09-28 距离语义修正）：娃娃/路障主/遙控骰子（要求无分叉）、
//   卡片查封 6 格/烏龜 3 格。实现于匿名 ns 外供 ai_card.cpp 复用。
void aiItemPredictPath(const GameState& st, int p, int steps, uint16_t out[8], bool* hasBranch) {
    std::memset(out, 0, sizeof(uint16_t) * 8);
    if (steps > 8) {
        steps = 8;
    }
    const Player& pl = st.players[p];
    uint16_t cur = pl.cellEntId;
    uint16_t prev = pl.prevCellEnt;
    for (int i = 0; i < steps; ++i) {
        uint16_t cand[4];
        int n = 0;
        if (cur > 0 && cur < static_cast<int>(st.cellEnts.size())) {
            const CellEnt& ce = st.cellEnts[cur];
            uint32_t bit = 0x40000000u;
            for (int k = 0; k < 4; ++k) {
                const uint16_t e = ce.exits[k];
                if (e != 0 && e != prev && (ce.occMask & bit) == 0) {
                    cand[n++] = e;
                }
                bit >>= 1;
            }
        }
        if (n == 0) {
            out[i] = prev;  // 死路 → 折回填来路
        } else if (n == 1) {
            out[i] = cand[0];
        } else {
            *hasBranch = true;
            out[i] = cand[dbg::roll(dbg::SlotAi, n)];
        }
        prev = cur;
        cur = out[i];
    }
}

namespace {

constexpr int kPlayerCount4 = 4;
constexpr double kCostMul = 2.5;  // [RE dbl_463D48]

uint16_t objIdAt(const GameState& st, int cellId) {
    if (cellId <= 0 || cellId >= static_cast<int>(st.cellEnts.size())) {
        return 0;
    }
    return st.cellEnts[cellId].special;
}

int ownerAt(const GameState& st, uint16_t objId) {
    if (objId > 2000 && objId < 4000) {
        return st.estates[objId - 2000].owner;
    }
    if (objId > 4000 && objId < 6000) {
        return st.corps[objId - 4000].owner;
    }
    return 0;
}

// 命中 → 格 id（cellEnt）：地块命中取来源格 cellId；cellEnt 直命中取 id
int cellIdOfHit(const GameState::MapHitRegion& h) {
    if (h.cellId != 0) {
        return h.cellId;
    }
    if (h.id != 0 && h.id < 2000) {
        return h.id;
    }
    return 0;
}

int randomOpponent(const GameState& st, int exclude) {
    int list[4];
    int n = 0;
    for (int i = 0; i < st.playerCount && i < kPlayerCount4; ++i) {
        if (i != exclude && st.players[i].alive != 0) {
            list[n++] = i;
        }
    }
    return n != 0 ? list[dbg::roll(dbg::SlotAi, n)] : -1;
}

int mineCountOnRoute(const GameState& st, int cur, const char* name) {
    int n = 0;
    for (size_t j = 1; j < st.estates.size(); ++j) {
        if (st.estates[j].owner == static_cast<uint8_t>(cur + 1) &&
            std::strcmp(st.estates[j].name, name) == 0) {
            ++n;
        }
    }
    return n;
}

int alivePlayerCount(const GameState& st) {  // [RE sub_40D2B4]
    int n = 0;
    for (int i = 0; i < st.playerCount && i < kPlayerCount4; ++i) {
        if (st.players[i].alive != 0) {
            ++n;
        }
    }
    return n;
}

// 附身/状态门槛组 [RE 0x42107F/0x421827]：bank+cash>10000 ∧ luckB≥0 ∧ 非固定步数
bool aiSpendOk(const GameState& st, int cur) {
    const Player& pl = st.players[cur];
    return pl.bank + pl.cash > 10000 && pl.luckB >= 0 && pl.fixedStep == 0;
}

// [RE sub_40A0B1 sub_409EF9 画布近似] 世界坐标是否落在"以 (tx,ty) 为中心 radius 屏幕 px"
//   画布内：2:1 等距（32 网格 → 屏幕步进 sx=(wx-wy)/2, sy=(wx+wy)/4）。
//   [PORT] 忽略 mapRotation 对投影轴的置换（旋转下菱形形状等价，判定集合一致）。
bool nearIso(int tx, int ty, int ex, int ey, int radius) {
    const int sx = std::abs(((ex - ey) - (tx - ty)) / 2);
    const int sy = std::abs(((ex + ey) - (tx + ty)) / 4);
    return sx <= radius && sy <= radius;
}

// [RE sub_40B343] 变体：从**上一格**出发逐步推进（首步 base=prevCellEnt、排除 cur），
// 无出口 → 截断（后续项保持 0）。用途：地雷/炸彈/路障兜底——候选域=身后回溯路线∩视口。
void predictPathBack(const GameState& st, int p, int steps, uint16_t out[8]) {
    std::memset(out, 0, sizeof(uint16_t) * 8);
    if (steps > 8) {
        steps = 8;
    }
    const Player& pl = st.players[p];
    uint16_t cur = pl.prevCellEnt;   // v12：首步从 prev 出发
    uint16_t prev = pl.cellEntId;    // v14：排除来路=当前格
    for (int i = 0; i < steps; ++i) {
        uint16_t cand[4];
        int n = 0;
        if (cur > 0 && cur < static_cast<int>(st.cellEnts.size())) {
            const CellEnt& ce = st.cellEnts[cur];
            uint32_t bit = 0x40000000u;
            for (int k = 0; k < 4; ++k) {
                const uint16_t e = ce.exits[k];
                if (e != 0 && e != prev && (ce.occMask & bit) == 0) {
                    cand[n++] = e;
                }
                bit >>= 1;
            }
        }
        if (n == 0) {
            break;  // 截断
        }
        out[i] = (n == 1) ? cand[0] : cand[dbg::roll(dbg::SlotAi, n)];
        prev = cur;
        cur = out[i];
    }
}

// [RE sub_409EF9] 视口格列表 word_48B8C4：occMask bit8-23 干净的格 id（纯格，无对象覆盖）
int viewportCells(const GameState& st, uint16_t out[], int cap) {
    int n = 0;
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
            continue;
        }
        if ((st.cellEnts[cid].occMask & 0x00FFFF00u) != 0) {
            continue;  // 占用格原版不登记
        }
        bool dup = false;
        for (int i = 0; i < n; ++i) {
            if (out[i] == static_cast<uint16_t>(cid)) {
                dup = true;
                break;
            }
        }
        if (!dup && n < cap) {
            out[n++] = static_cast<uint16_t>(cid);
        }
    }
    return n;
}

bool inList(const uint16_t* list, int n, uint16_t v) {
    for (int i = 0; i < n; ++i) {
        if (list[i] == v) {
            return true;
        }
    }
    return false;
}

// ===== 逐道具目标（[RE funcs_420EE6 @0x4753A0]）=====

int tgtDoll(GameState& st) {  // id1 機器娃娃 [RE 0x420EFA]：前方4步路径（无分叉）有可踢障碍
    const int cur = st.currentPlayer;
    uint16_t path[8];
    bool branch = false;
    aiItemPredictPath(st, cur, 4, path, &branch);
    if (branch) {
        return 0;  // 原版 !sub_40B221：路径不确定 → 不放娃娃
    }
    for (int i = 0; i < 4; ++i) {
        const int cid = path[i];
        if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
            continue;
        }
        const int slot = (st.cellEnts[cid].occMask & 0x3F0000u) >> 16;
        if (slot < 1 || slot > 46) {
            continue;
        }
        const int type = st.cellTable[24 * (slot - 1)];
        const uint16_t oid = objIdAt(st, cid);
        int owner = 0;
        int rent = 0;
        if (oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            owner = es.owner;
            rent = estateRouteRent(st, es.owner, es);
        } else if (oid > 4000 && oid < 6000) {
            owner = st.corps[oid - 4000].owner;
            rent = 10000000;  // [RE 0x420F4D] corp 固定高值（路障高租判定必过）
        }
        if (type == 5 || type == 6 || type == 7 || type == 8 || type == 11 ||
            (type == 17 && owner == cur + 1) ||
            (type == 16 && owner != 0 && owner != cur + 1 &&
             rent > 3000 * st.moneyMul)) {
            return 1;
        }
    }
    return 0;
}

int tgtRoadblock(GameState& st) {  // id2 路障 [RE 0x42107F]：主=前方4步（无分叉）卡位，兜底=回溯路线自家高租
    const int cur = st.currentPlayer;
    uint16_t path[8];
    bool branch = false;
    aiItemPredictPath(st, cur, 4, path, &branch);
    if (!branch) {
        for (int i = 0; i < 4; ++i) {
            const int cid = path[i];
            if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
                continue;
            }
            const CellEnt& ce = st.cellEnts[cid];
            if ((ce.occMask & 0x3FFF00u) != 0) {
                continue;  // 占用格不可放
            }
            const uint16_t oid = ce.special;
            if (oid <= 2000 || oid >= 6000) {
                // [RE 0x421288] 普通格：类型 15（道路）且点券>200 → 卡自己
                if ((ce.occMask & 0xFF) == 15 && st.players[cur].points > 200) {
                    st.aiItemTarget = cid;
                    return 1;
                }
                continue;
            }
            if (!aiSpendOk(st, cur)) {
                continue;
            }
            if (oid < 4000) {
                const Estate& es = st.estates[oid - 2000];
                if (es.owner != 0) {
                    continue;
                }
                if (mineCountOnRoute(st, cur, es.name) < 2 && es.level == 0) {
                    continue;  // 同段<2 块且无建筑 → 不值得卡
                }
                if (static_cast<double>(st.moneyMul) * es.priceAdd < st.players[cur].cash) {
                    st.aiItemTarget = cid;
                    return 1;
                }
            } else {
                const Corp& cp = st.corps[oid - 4000];
                if (cp.owner == 0 &&
                    static_cast<double>(st.moneyMul) * cp.buildPrice < st.players[cur].cash) {
                    st.aiItemTarget = cid;
                    return 1;
                }
            }
        }
    }
    // 兜底 [RE 0x42129E]：回溯路线 sub_40B343(6) ∩ 视口 → 自家 estate 租金>6000M 最高
    uint16_t back[8];
    predictPathBack(st, cur, 6, back);
    uint16_t cells[512];
    const int nc = viewportCells(st, cells, 512);
    int best = 0;
    int bestRent = 0;
    for (int i = 0; i < nc; ++i) {
        if (!inList(back, 6, cells[i])) {
            continue;
        }
        const uint16_t oid = objIdAt(st, cells[i]);
        if (oid <= 2000 || oid >= 4000) {
            continue;
        }
        const Estate& es = st.estates[oid - 2000];
        if (es.owner != static_cast<uint8_t>(cur + 1)) {
            continue;
        }
        const int rent = estateRouteRent(st, es.owner, es);
        if (rent > 6000 * st.moneyMul && rent > bestRent) {
            bestRent = rent;
            best = cells[i];
        }
    }
    if (best != 0) {
        st.aiItemTarget = best;
        return 1;
    }
    return 0;
}

// [RE 0x4213C5] 地雷：候选 = 回溯路线(6) ∩ 视口 ∩ 他人产业；监狱/医院格（有在押）命中即选
int tgtMine(GameState& st) {
    const int cur = st.currentPlayer;
    uint16_t back[8];
    predictPathBack(st, cur, 6, back);
    uint16_t cells[512];
    const int nc = viewportCells(st, cells, 512);
    int cand[256];
    int n = 0;
    for (int i = 0; i < nc; ++i) {
        if (!inList(back, 6, cells[i])) {
            continue;  // 原版：格须在前方路径列表（监狱/医院分支同样要求）
        }
        if ((cells[i] == st.jailCellEntId && st.jailFlags[0] != 0) ||
            (cells[i] == st.hospitalCellEntId && st.hospitalFlags[0] != 0)) {
            st.aiItemTarget = cells[i];
            return 1;
        }
        const int owner = ownerAt(st, objIdAt(st, cells[i]));
        if (owner != 0 && owner != cur + 1 && n < 256) {
            cand[n++] = cells[i];
        }
    }
    if (n == 0) {
        return 0;
    }
    st.aiItemTarget = cand[dbg::roll(dbg::SlotAi, n)];
    return 1;
}

// [RE 0x421574] 定時炸彈：候选 = 回溯路线(6) ∩ 视口 **任意格**（不限他人产业）
int tgtBomb(GameState& st) {
    const int cur = st.currentPlayer;
    uint16_t back[8];
    predictPathBack(st, cur, 6, back);
    uint16_t cells[512];
    const int nc = viewportCells(st, cells, 512);
    int cand[256];
    int n = 0;
    for (int i = 0; i < nc; ++i) {
        if (!inList(back, 6, cells[i])) {
            continue;
        }
        if ((cells[i] == st.jailCellEntId && st.jailFlags[0] != 0) ||
            (cells[i] == st.hospitalCellEntId && st.hospitalFlags[0] != 0)) {
            st.aiItemTarget = cells[i];
            return 1;
        }
        if (n < 256) {
            cand[n++] = cells[i];
        }
    }
    if (n == 0) {
        return 0;
    }
    st.aiItemTarget = cand[dbg::roll(dbg::SlotAi, n)];
    return 1;
}

// [RE 0x421717] 飛彈：债主/随机者 → 其所在格；**全图目标不做距离限制**（用户决策 2026-09-28，
//   道具效果端发射时视口即移到目标）。溅射保护 [RE sub_40A0B1(债主坐标,100)]：画布只登记
//   当前玩家（stateFlags==0 时）+ 产业 → 自己将被波及 或 自家产业在 ±100 内 → 放弃。
int tgtMissile(GameState& st) {
    const int cur = st.currentPlayer;
    int target = findMaxCreditor(st, cur);
    if (target == -1) {
        target = randomOpponent(st, cur);
    }
    if (target < 0) {
        return 0;
    }
    const int cid = st.players[target].cellEntId;
    if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
        return 0;
    }
    const Player& pl = st.players[cur];
    const int tx = st.players[target].spriteX;
    const int ty = st.players[target].spriteY;
    if (pl.stateFlags == 0 && nearIso(tx, ty, pl.spriteX, pl.spriteY, 100)) {
        return 0;  // 自己恰在爆炸圈（原版仅登记发射者玩家位）
    }
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& es = st.estates[i];
        if (es.owner == static_cast<uint8_t>(cur + 1) && nearIso(tx, ty, es.x, es.y, 100)) {
            return 0;  // [RE sub_4216AB] 溅射含自家产业 → 放弃
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        const Corp& cp = st.corps[i];
        if (cp.owner == static_cast<uint8_t>(cur + 1) && nearIso(tx, ty, cp.x, cp.y, 100)) {
            return 0;
        }
    }
    st.aiItemTarget = cid;
    return 1;
}

int tgtDice(GameState& st) {  // id8 遙控骰子 [RE 0x421827]：前方6步（无分叉）可达格 → 点数=索引+1
    const int cur = st.currentPlayer;
    const Player& pl = st.players[cur];
    // 原版入口拒绝组：附身 7/8/15 ∨ 状态 byte_496BA1(fixedStep) ∨ 钱不够 ∨ luckB<0
    if (pl.cellTableIdx == 7 || pl.cellTableIdx == 8 || pl.cellTableIdx == 15 ||
        pl.fixedStep != 0 || pl.bank + pl.cash < 10000 || pl.luckB < 0) {
        return 0;
    }
    uint16_t path[8];
    bool branch = false;
    aiItemPredictPath(st, cur, 6, path, &branch);
    if (branch) {
        return 0;  // [RE 0x421877] sub_40B221 返回真（路径不确定）→ 不用骰子
    }
    for (int i = 0; i < 6; ++i) {
        const int cid = path[i];
        if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
            continue;
        }
        const CellEnt& ce = st.cellEnts[cid];
        if ((ce.occMask & 0xF000u) != 0) {
            continue;  // 神明/事件占用 → 跳过
        }
        const int slot = (ce.occMask & 0x3F0000u) >> 16;
        if (slot >= 1 && slot <= 46) {
            const int t = st.cellTable[24 * (slot - 1)];
            if (t == 5 || t == 6 || t == 7 || t == 8 || t == 10 || t == 11 || t == 16 ||
                t == 17 || t == 18) {
                continue;  // [RE 0x421901] 陷阱/挂身物件格 → 跳过
            }
        }
        // 原版仅**无主** estate/corp 分支置 v16 命中返回（0x4219C4/0x421B39）；
        //   自家可加盖/可升设施分支不置 v16 → 永不命中（目标值残留无影响）
        const uint16_t oid = ce.special;
        if (oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            if (es.owner == 0 && mineCountOnRoute(st, cur, es.name) > 1 &&
                kCostMul * st.moneyMul * es.priceAdd < pl.cash) {
                st.aiItemTarget = i + 1;
                return 1;
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == 0 && kCostMul * st.moneyMul * cp.buildPrice < pl.cash) {
                st.aiItemTarget = i + 1;
                return 1;
            }
        }
    }
    return 0;
}

int tgtRobotWorker(GameState& st) {  // id9 機器工人 [RE 0x421BA6]：视口实体列表中自己收益最高的地加盖
    const int cur = st.currentPlayer;
    int best = 0;
    int bestFee = 0;
    for (const auto& h : st.mapHitRegions) {
        const uint16_t oid = h.id;
        if (oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            if (es.owner == static_cast<uint8_t>(cur + 1) && es.type == 0 && es.level < 5) {
                const int fee = es.fees[es.level < 6 ? es.level : 0];
                if (fee > bestFee) {
                    bestFee = fee;
                    best = oid;  // 对象 id（useItemRobotWorker → angelUpgrade/getObjectPosition）
                }
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == static_cast<uint8_t>(cur + 1) && cp.type < 5 &&
                cp.sub < kFacilityMaxLevel[cp.type]) {
                const int fee = cp.feeTable[cp.sub < 6 ? cp.sub : 0];
                if (fee > bestFee) {
                    bestFee = fee;
                    best = oid;
                }
            }
        }
    }
    if (best != 0) {
        st.aiItemTarget = best;
        return 1;
    }
    return 0;
}

int tgtTeleport(GameState& st) {  // id11 傳送機 [RE 0x421CB6]：视口无主高等级地（自己去占）
    const int cur = st.currentPlayer;
    const int64_t cash = st.players[cur].cash;
    int best = 0;
    int bestLv = 0;
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        if (cid == 0) {
            continue;
        }
        const uint16_t oid = objIdAt(st, cid);
        if (oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            if (es.owner == 0 && es.type == 0 && es.level >= 3 && es.level > bestLv &&
                static_cast<double>(st.moneyMul) * es.priceBase < cash) {
                bestLv = es.level;
                best = cid;
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == 0 && cp.type != 0 && cp.sub >= 3 && cp.sub > bestLv &&
                static_cast<double>(st.moneyMul) * cp.feeTable[0] < cash) {
                bestLv = cp.sub;
                best = cid;
            }
        }
    }
    // 原版门槛在遍历后（v1 非 0  ∧ bank+cash>10000 ∧ luckB≥0）
    if (best != 0 && st.players[cur].bank + cash > 10000 && st.players[cur].luckB >= 0) {
        st.aiItemTarget = best;
        return 1;
    }
    return 0;
}

// id13 核子飛彈 [RE 0x421E62]：全图他人有建筑地（level/sub≠0）；**不做距离限制**
//   （原版 collectAroundPoint(obj,-1) 以目标重投影全画布 ±220）。每次试投评估：
//   自己（stateFlags==0 登记玩家位）在画布内 → 放弃；否则 自家块数/他人块数 ∧
//   自家Σlevel/他人Σlevel 均 < 1/(存活玩家+2) 才命中（不炸自家圈）。
int tgtNuke(GameState& st) {
    const int cur = st.currentPlayer;
    int list[256];
    int n = 0;
    for (size_t i = 1; i < st.estates.size() && n < 256; ++i) {
        const Estate& es = st.estates[i];
        if (es.owner != 0 && es.owner != static_cast<uint8_t>(cur + 1) && es.level != 0) {
            list[n++] = 2000 + static_cast<int>(i);
        }
    }
    for (size_t i = 1; i < st.corps.size() && n < 256; ++i) {
        const Corp& cp = st.corps[i];
        if (cp.owner != 0 && cp.owner != static_cast<uint8_t>(cur + 1) && cp.sub != 0) {
            list[n++] = 4000 + static_cast<int>(i);
        }
    }
    if (n == 0) {
        return 0;
    }
    const double limit = 1.0 / (alivePlayerCount(st) + 2);  // [RE v22 = 1/(sub_40D2B4+2)]
    const Player& pl = st.players[cur];
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int oid = list[dbg::roll(dbg::SlotAi, n)];
        int tx = 0;
        int ty = 0;
        getObjectPosition(st, oid, tx, ty);
        if (pl.stateFlags == 0 && nearIso(tx, ty, pl.spriteX, pl.spriteY, 220)) {
            continue;  // [RE 0x421FE5] 画布含玩家位（仅登记发射者自己）→ 放弃
        }
        int ownCnt = 0;
        int ownSum = 0;
        int otherCnt = 0;
        int otherSum = 0;
        for (size_t i = 1; i < st.estates.size(); ++i) {
            const Estate& es = st.estates[i];
            if (es.owner == 0 || !nearIso(tx, ty, es.x, es.y, 220)) {
                continue;
            }
            if (es.owner == static_cast<uint8_t>(cur + 1)) {
                ownSum += es.level;
                ++ownCnt;
            } else {
                otherSum += es.level;
                ++otherCnt;
            }
        }
        for (size_t i = 1; i < st.corps.size(); ++i) {
            const Corp& cp = st.corps[i];
            if (cp.owner == 0 || !nearIso(tx, ty, cp.x, cp.y, 220)) {
                continue;
            }
            if (cp.owner == static_cast<uint8_t>(cur + 1)) {
                ownSum += cp.sub;
                ++ownCnt;
            } else {
                otherSum += cp.sub;
                ++otherCnt;
            }
        }
        const double rCnt = (otherCnt != 0) ? static_cast<double>(ownCnt) / otherCnt : INFINITY;
        const double rSum = (otherSum != 0) ? static_cast<double>(ownSum) / otherSum : INFINITY;
        if (rCnt < limit && rSum < limit) {  // [RE 0x422138] 双比例保护（不炸自家圈）
            st.aiItemTarget = oid;
            return 1;
        }
    }
    return 0;
}

}  // namespace

int aiItemSelect(Application& app, int itemId) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= kPlayerCount4 || itemId < 1 || itemId > 13) {
        return 0;
    }
    // [RE 0x420E9A] 性格判定
    const int diff = static_cast<int>(kItemAiPersona[itemId]) -
                     static_cast<int>(st.players[cur].aiPersonality);
    if (diff >= 2) {
        return 0;
    }
    if (diff == 1 && dbg::roll(dbg::SlotAi, 3) != 0) {
        return 0;
    }
    st.aiItemTarget = 0;
    int r = 0;
    switch (itemId) {
        case 1:  r = tgtDoll(st); break;
        case 2:  r = tgtRoadblock(st); break;
        case 3:  r = tgtMine(st); break;
        case 4:  r = tgtBomb(st); break;
        case 5:  r = ((st.players[cur].travel & 3) == 0 && dbg::roll(dbg::SlotAi, 4) == 0) ? 1 : 0; break;
        case 6:  r = ((st.players[cur].travel & 3) < 2 && dbg::roll(dbg::SlotAi, 4) == 0) ? 1 : 0; break;
        case 7:  r = tgtMissile(st); break;
        case 8:  r = tgtDice(st); break;
        case 9:  r = tgtRobotWorker(st); break;
        case 10: return 0;  // [RE 0x420EDF] 時光機：AI 不用
        case 11: r = tgtTeleport(st); break;
        case 12: r = (dbg::roll(dbg::SlotAi, 15) <= st.players[cur].aiPersonality) ? 1 : 0; break;
        case 13: r = tgtNuke(st); break;
        default: return 0;
    }
    if (r == 1) {
        RICH4_LOGI("aiItemSelect: p%d item=%d target=%d curCell=%u (RE 0x420E9A/table)", cur,
                   itemId, st.aiItemTarget, st.players[cur].cellEntId);
        return 1;
    }
    // 诊断：persona 已过但目标函数无候选（实机核对候选域用，低频日志）
    RICH4_LOGI("aiItemSelect: p%d item=%d rejected (no candidate; persona=%u ai=%u cell=%u)",
               cur, itemId, kItemAiPersona[itemId], st.players[cur].aiPersonality,
               st.players[cur].cellEntId);
    return 0;
}

// [RE 0x4221C0] 回合骰子数动态调整（beginPlayerTurn 0x418E70，状态守卫后、移动前）。
// 依据: 0x4221C0 反编译——travel(+17)：2 汽车基数3 / 1 机车基数2 / 0 步行直接返回；
//   挂道具 g_playerCellNo(+64)≠0 → 寿命 g_cellLife[槽]=cellTable[24*(cellNo-1)+4]：
//   汽车 <15→1、15..20→保持3 返回、>20→3 返回；机车 ≥15→返回、否则→1。
//   无道具：aiPathForward(cur,5) 前方5格——无主/自家计入 ownFree（0x4223F9/0x42240C），
//   他人产业计入 other；汽车 `!ownFree∧other>2`→2+(rand&1)，`ownFree≥2∧other≤1`→1；
//   机车 `!ownFree∧other>2`→2（重赋基数），`ownFree≥2∧other≤1`→1，其余保持 2。
// 语义：自家地产密集段收骰子（1 骰守段收租），他人产业段放开（2~3 骰闯段）。
void aiDiceAdjustPerTurn(Application& app) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= kPlayerCount4) {
        return;
    }
    Player& pl = st.players[p];
    if (pl.travel == 0 || pl.travel > 2) {
        return;  // 步行不调整（0x42231C v1∉{1,2} return）
    }
    pl.diceCount = (pl.travel == 2) ? 3 : 2;
    if (pl.cellNo != 0) {
        const size_t off = 24 * (static_cast<size_t>(pl.cellNo) - 1);
        if (off + 4 >= st.cellTable.size()) {
            return;
        }
        const int life = st.cellTable[off + 4];
        if (pl.travel == 2) {
            if (life < 15) {
                pl.diceCount = 1;
            } else if (life > 20) {
                pl.diceCount = 3;
            }
        } else {
            if (life < 15) {
                pl.diceCount = 1;
            }
        }
        return;
    }
    uint16_t path[8];
    bool branch = false;
    aiItemPredictPath(st, p, 5, path, &branch);
    int ownFree = 0;
    int other = 0;
    for (int i = 0; i < 5; ++i) {
        const int cid = path[i];
        if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
            continue;
        }
        const uint16_t oid = st.cellEnts[cid].special;
        int o = 0;
        if (oid > 2000 && oid < 4000) {
            o = st.estates[oid - 2000].owner;
        } else if (oid > 4000 && oid < 6000) {
            o = st.corps[oid - 4000].owner;
        } else {
            continue;  // 普通格不计数
        }
        if (o == 0 || o == static_cast<uint8_t>(p + 1)) {
            ++ownFree;
        } else {
            ++other;
        }
    }
    if (pl.travel == 2) {
        if (ownFree == 0 && other > 2) {
            pl.diceCount = static_cast<uint8_t>(2 + dbg::roll(dbg::SlotAi, 2));
        } else if (ownFree >= 2 && other <= 1) {
            pl.diceCount = 1;
        }
    } else {
        if (ownFree == 0 && other > 2) {
            pl.diceCount = 2;
        } else if (ownFree >= 2 && other <= 1) {
            pl.diceCount = 1;
        }
    }
    RICH4_LOGI("aiDiceAdjust: p%d travel=%u dice=%u ownFree=%d other=%d (RE 0x4221C0)", p,
               pl.travel, pl.diceCount, ownFree, other);
}

}  // namespace rich4
