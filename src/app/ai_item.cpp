#include "game/app/ai_item.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "game/app/economy.h"
#include "game/app/map_objects.h"
#include "game/app/map_tables.h"
#include "game/app/target_select_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/debug_hooks.h"
#include "game/game_state.h"

namespace rich4 {
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

// ===== 候选域公共 [RE word_48B8B4 / word_48B8C4] =====

// [RE sub_40B221] 玩家**前方** steps 步预测路径 → out[steps]（≤8，0=无）。
// 逐步从当前格选出口：排除来路 prevCellEnt、排除 occMask bit30-33 占用；
// 多出口 → 随机选一并置 *hasBranch（原版 v12）；无出口 → 回填来路继续（原地折返）。
// 用途：娃娃/路障主（要求 !hasBranch——路径唯一才赌）、遙控骰子（点数=索引+1）。
void predictPathFwd(const GameState& st, int p, int steps, uint16_t out[8], bool* hasBranch) {
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

// [RE sub_40A0B1] 以目标（objId）为中心的采集列表：重写以当前视口拾取记录
//   （mapHitRegions）按屏幕坐标差 ≤ radius 过滤，三类实体与原登记一致——
//   玩家（→ 0x8000|(1<<p)，normalizeHitId 同式）/ 有主 estate（2000+）/ 有主 corp
//   （4000+）；radius = -1 → 全视口。目标不在视口（无 anchor）→ 返回 -1（放弃）。
//   [RE] 与 expireAssets 屏幕口径统一，保证"AI 评估范围 = 实际爆炸范围"。
int collectAround(GameState& st, int objId, int radius, uint16_t out[], int cap) {
    const GameState::MapHitRegion* tgt = nullptr;
    for (const auto& h : st.mapHitRegions) {
        if (h.id == static_cast<uint16_t>(objId)) {
            tgt = &h;
            break;
        }
    }
    if (tgt == nullptr) {
        return -1;
    }
    int n = 0;
    for (const auto& h : st.mapHitRegions) {
        if (h.id == tgt->id) {
            continue;
        }
        if (radius >= 0 &&
            (std::abs(h.anchorX - tgt->anchorX) > radius ||
             std::abs(h.anchorY - tgt->anchorY) > radius)) {
            continue;
        }
        const uint16_t id = static_cast<uint16_t>(normalizeHitId(h.id));
        const bool isPlayer = (id & 0x8000) != 0 && (id & 0x00FF) != 0;
        const bool isEstate = id > 2000 && id < 4000;
        const bool isCorp = id > 4000 && id < 6000;
        if ((isPlayer || isEstate || isCorp) && n < cap) {
            out[n++] = id;
        }
    }
    return n;
}

// [RE sub_4216AB] 该采集项是否为 cur 的产业
bool isOwnEstate(const GameState& st, int cur, uint16_t id) {
    if (id > 2000 && id < 4000) {
        return st.estates[id - 2000].owner == static_cast<uint8_t>(cur + 1);
    }
    if (id > 4000 && id < 6000) {
        return st.corps[id - 4000].owner == static_cast<uint8_t>(cur + 1);
    }
    return false;
}

// ===== 逐道具目标（[RE funcs_420EE6 @0x4753A0]）=====

int tgtDoll(GameState& st) {  // id1 機器娃娃 [RE 0x420EFA]：前方4步路径（无分叉）有可踢障碍
    const int cur = st.currentPlayer;
    uint16_t path[8];
    bool branch = false;
    predictPathFwd(st, cur, 4, path, &branch);
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
    predictPathFwd(st, cur, 4, path, &branch);
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

// [RE 0x4213C5] 地雷：候选 = 回溯路线(6) ∩ 视口 ∩ 他人产业；监狱/医院格（在押）命中即选
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

int tgtMissile(GameState& st) {  // id7 飛彈 [RE 0x421717]：债主须在视口；溅射(±100)无玩家/自家产业
    const int cur = st.currentPlayer;
    int target = findMaxCreditor(st, cur);
    if (target == -1) {
        target = randomOpponent(st, cur);
    }
    if (target < 0) {
        return 0;
    }
    // 债主须出现在当前视口拾取列表（玩家命中 0xF000|p）
    const uint16_t pHit = static_cast<uint16_t>(0xF000 | target);
    const GameState::MapHitRegion* th = nullptr;
    for (const auto& h : st.mapHitRegions) {
        if (h.id == pHit) {
            th = &h;
            break;
        }
    }
    if (th == nullptr) {
        return 0;  // 债主不在屏幕内 → 原版不发
    }
    const int cid = st.players[target].cellEntId;
    if (cid <= 0 || cid >= static_cast<int>(st.cellEnts.size())) {
        return 0;
    }
    st.aiItemTarget = cid;
    // 溅射区 [RE 0x4217B5..0x421815]：债主周围 ±100 屏 px 含任意玩家或自家产业 → 放弃
    uint16_t list[256];
    const int nl = collectAround(st, static_cast<int>(pHit), 100, list, 256);
    for (int i = 0; i < nl; ++i) {
        const uint16_t id = list[i];
        if ((id & 0x8000) != 0 && (id & 0xFF) != 0) {
            return 0;  // 玩家（含自己/债主）
        }
        if (isOwnEstate(st, cur, id)) {
            return 0;
        }
    }
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
    predictPathFwd(st, cur, 6, path, &branch);
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

int tgtNuke(GameState& st) {  // id13 核子飛彈 [RE 0x421E62]：他人有建筑地；溅射无玩家 ∧ 自家/他人 块数·等级比 < 1/(玩家数+2)
    const int cur = st.currentPlayer;
    int list[256];
    int n = 0;
    for (size_t i = 1; i < st.estates.size() && n < 256; ++i) {
        const Estate& es = st.estates[i];
        if (es.owner != 0 && es.owner != static_cast<uint8_t>(cur + 1) && es.level != 0) {
            list[n++] = 2000 + static_cast<int>(i);  // [RE 0x421ECE] 有建筑（level≠0，非 type）
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
    uint16_t buf[256];
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int oid = list[dbg::roll(dbg::SlotAi, n)];
        const int nl = collectAround(st, oid, -1, buf, 256);
        if (nl < 0) {
            continue;  // 目标不在视口 → 该候选无法评估
        }
        int ownCnt = 0;
        int ownSum = 0;
        int otherCnt = 0;
        int otherSum = 0;
        bool hasPlayer = false;
        for (int i = 0; i < nl; ++i) {
            const uint16_t id = buf[i];
            if ((id & 0x8000) != 0 && (id & 0xFF) != 0) {
                hasPlayer = true;  // [RE 0x421FE5] 含玩家位掩码 → 放弃
                break;
            }
            int lv = 0;
            if (id > 2000 && id < 4000) {
                lv = st.estates[id - 2000].level;
            } else if (id > 4000 && id < 6000) {
                lv = st.corps[id - 4000].sub;
            }
            if (isOwnEstate(st, cur, id)) {
                ownSum += lv;
                ++ownCnt;
            } else {
                otherSum += lv;
                ++otherCnt;
            }
        }
        if (hasPlayer) {
            continue;
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
    return 0;
}

}  // namespace rich4
