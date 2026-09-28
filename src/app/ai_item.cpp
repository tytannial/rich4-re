#include "game/app/ai_item.h"

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
namespace {

constexpr int kPlayerCount4 = 4;

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

// ===== 逐道具目标（[RE funcs_420EE6 @0x4753A0]）=====

int tgtDoll(GameState& st) {  // id1 機器娃娃 [RE 0x420EFA]：视口有可踢障碍（负面神/自地雷/他人高租路障）
    const int cur = st.currentPlayer;
    for (const auto& h : st.mapHitRegions) {
        if ((h.id & 0xFF00) != 0xA100) {
            continue;
        }
        const int slot = h.id & 0xFF;
        if (slot < 1 || slot > 46) {
            continue;
        }
        const int type = st.cellTable[24 * (slot - 1)];
        if (type == 5 || type == 6 || type == 7 || type == 8 || type == 11) {
            return 1;
        }
        const int cellId = st.cellTable[24 * (slot - 1) + 2] |
                           (st.cellTable[24 * (slot - 1) + 3] << 8);
        const uint16_t oid = objIdAt(st, cellId);
        if (type == 17 && ownerAt(st, oid) == cur + 1) {
            return 1;  // 自己地上的地雷
        }
        if (type == 16 && oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            if (es.owner != 0 && es.owner != cur + 1 &&
                estateRouteRent(st, es.owner, es) > 3000 * st.moneyMul) {
                return 1;  // 他人地上的高租路障
            }
        }
    }
    return 0;
}

int tgtRoadblock(GameState& st) {  // id2 路障 [RE 0x42107F]
    const int cur = st.currentPlayer;
    if (st.players[cur].fixedStep != 0) {
        return 0;
    }
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        if (cid == 0) {
            continue;
        }
        const uint16_t oid = objIdAt(st, cid);
        if (oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            if (es.owner != 0) {
                continue;
            }
            // 原版：无建筑 且 同段自己 <2 块 → 略过（有建筑或同段≥2 块才卡）
            if (es.level == 0 && mineCountOnRoute(st, cur, es.name) < 2) {
                continue;
            }
            if (st.moneyMul * es.priceAdd < st.players[cur].cash) {
                st.aiItemTarget = cid;
                return 1;
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == 0 && st.moneyMul * cp.buildPrice < st.players[cur].cash) {
                st.aiItemTarget = cid;
                return 1;
            }
        }
    }
    // 兜底：自己同段≥2 块且租金 >6000×M 的最高租格
    int best = 0;
    int64_t bestRent = 0;
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        const uint16_t oid = objIdAt(st, cid);
        if (cid == 0 || oid <= 2000 || oid >= 4000) {
            continue;
        }
        const Estate& es = st.estates[oid - 2000];
        if (es.owner != cur + 1 || mineCountOnRoute(st, cur, es.name) < 2) {
            continue;
        }
        const int64_t rent = estateRouteRent(st, es.owner, es);
        if (rent > 6000 * st.moneyMul && rent > bestRent) {
            bestRent = rent;
            best = cid;
        }
    }
    if (best != 0) {
        st.aiItemTarget = best;
        return 1;
    }
    return 0;
}

int tgtMineLike(GameState& st) {  // id3 地雷 [RE 0x4213C5] / id4 炸彈 [RE 0x421574]
    const int cur = st.currentPlayer;
    // 监獄/醫院格（有在押）优先
    if (st.jailCellEntId != 0 && st.jailFlags[0] != 0) {
        st.aiItemTarget = st.jailCellEntId;
        return 1;
    }
    if (st.hospitalCellEntId != 0 && st.hospitalFlags[0] != 0) {
        st.aiItemTarget = st.hospitalCellEntId;
        return 1;
    }
    int cand[128];
    int n = 0;
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        if (cid == 0) {
            continue;
        }
        const int owner = ownerAt(st, objIdAt(st, cid));
        if (owner != 0 && owner != cur + 1 && n < 128) {
            cand[n++] = cid;
        }
    }
    if (n == 0) {
        return 0;
    }
    st.aiItemTarget = cand[dbg::roll(dbg::SlotAi, n)];
    return 1;
}

int tgtMissile(GameState& st) {  // id7 飛彈 [RE 0x421717]：债主/随机者所在格
    const int cur = st.currentPlayer;
    int target = findMaxCreditor(st, cur);
    if (target == -1) {
        target = randomOpponent(st, cur);
    }
    if (target < 0) {
        return 0;
    }
    const int cid = st.players[target].cellEntId;
    if (cid == 0 || cid >= static_cast<int>(st.cellEnts.size())) {
        return 0;
    }
    st.aiItemTarget = cid;
    return 1;
}

int tgtDice(GameState& st) {  // id8 遙控骰子 [RE 0x421827]：有可升级/可占候选 → 点数 1..6
    const int cur = st.currentPlayer;
    const int64_t cash = st.players[cur].cash;
    bool cand = false;
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        if (cid == 0) {
            continue;
        }
        const uint16_t oid = objIdAt(st, cid);
        if (oid > 2000 && oid < 4000) {
            const Estate& es = st.estates[oid - 2000];
            if (es.owner == 0 && mineCountOnRoute(st, cur, es.name) > 1 &&
                2.5 * st.moneyMul * es.priceAdd < static_cast<double>(cash)) {
                cand = true;
                break;
            }
            if (es.owner == static_cast<uint8_t>(cur + 1) && es.type == 0 && es.level < 5 &&
                mineCountOnRoute(st, cur, es.name) > 1 &&
                2.5 * st.moneyMul * es.priceBase < static_cast<double>(cash)) {
                cand = true;
                break;
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == 0 && 2.5 * st.moneyMul * cp.buildPrice < static_cast<double>(cash)) {
                cand = true;
                break;
            }
            if (cp.owner == static_cast<uint8_t>(cur + 1) && cp.type != 0 && cp.type != 3 &&
                cp.sub < kFacilityMaxLevel[cp.type < 5 ? cp.type : 0] &&
                2.5 * st.moneyMul * cp.feeTable[0] < static_cast<double>(cash)) {
                cand = true;
                break;
            }
        }
    }
    if (cand) {
        st.aiItemTarget = dbg::roll(dbg::SlotAi, 6) + 1;  // [RE] 点数 1..6（原版 word_48B8B4 索引+1）
        return 1;
    }
    return 0;
}

int tgtRobotWorker(GameState& st) {  // id9 機器工人 [RE 0x421BA6]：自己收益最高的地加盖
    const int cur = st.currentPlayer;
    int best = 0;
    int bestFee = 0;
    for (const auto& h : st.mapHitRegions) {
        const int cid = cellIdOfHit(h);
        if (cid == 0) {
            continue;
        }
        const uint16_t oid = objIdAt(st, cid);
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
                    best = oid;  // 对象 id
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

int tgtTeleport(GameState& st) {  // id11 傳送機 [RE 0x421CB6]：无主高等级地（自己去占）
    const int cur = st.currentPlayer;
    const int64_t cash = st.players[cur].cash;
    if (st.players[cur].bank + cash <= 10000 || st.players[cur].luckB < 0) {
        return 0;
    }
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
            if (es.owner == 0 && es.type == 0 && es.level >= 3 &&
                st.moneyMul * es.priceBase < cash && es.level > bestLv) {
                bestLv = es.level;
                best = cid;
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == 0 && cp.type != 0 && cp.sub >= 3 &&
                st.moneyMul * cp.feeTable[0] < cash && cp.sub > bestLv) {
                bestLv = cp.sub;
                best = cid;
            }
        }
    }
    if (best != 0) {
        st.aiItemTarget = best;
        return 1;
    }
    return 0;
}

int tgtNuke(GameState& st) {  // id13 核子飛彈 [RE 0x421E62]：他人有建筑地（3×3 无玩家）
    const int cur = st.currentPlayer;
    int list[256];
    int n = 0;
    for (size_t i = 1; i < st.estates.size() && n < 256; ++i) {
        const Estate& es = st.estates[i];
        if (es.owner != 0 && es.owner != static_cast<uint8_t>(cur + 1) && es.type != 0) {
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
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int oid = list[dbg::roll(dbg::SlotAi, n)];
        int x = 0;
        int y = 0;
        getObjectPosition(st, oid, x, y);
        bool hasPlayer = false;
        for (int i = 0; i < st.playerCount && i < 9; ++i) {
            if (st.players[i].spriteX == 0 && st.players[i].spriteY == 0) {
                continue;
            }
            if (std::abs(st.players[i].spriteX - x) < 120 &&
                std::abs(st.players[i].spriteY - y) < 60) {
                hasPlayer = true;  // 约 3×3 格内有玩家 → 放弃该目标
                break;
            }
        }
        if (!hasPlayer) {
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
        case 3:  r = tgtMineLike(st); break;
        case 4:  r = tgtMineLike(st); break;
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
        RICH4_LOGI("aiItemSelect: p%d item=%d target=%d (RE 0x420E9A/table)", cur, itemId,
                   st.aiItemTarget);
        return 1;
    }
    return 0;
}

}  // namespace rich4
