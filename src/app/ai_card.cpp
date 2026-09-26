#include "game/app/ai_card.h"

#include <cstdlib>
#include <cstring>

#include "game/app/economy.h"
#include "game/app/game_panel.h"
#include "game/app/map_objects.h"
#include "game/app/map_tables.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/game_state.h"

namespace rich4 {
namespace {

constexpr int kPlayerCount4 = 4;

uint16_t standingObjId(const GameState& st) {
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= kPlayerCount4) {
        return 0;
    }
    const Player& pl = st.players[cur];
    if (pl.cellEntId == 0 || pl.cellEntId >= st.cellEnts.size()) {
        return 0;
    }
    return st.cellEnts[pl.cellEntId].special;
}

int32_t landPriceOf(const GameState& st, int objId) {
    if (objId > 2000 && objId < 4000) {
        const int i = objId - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        const Estate& es = st.estates[i];
        return static_cast<int32_t>((es.level * es.priceBase + es.priceAdd) * st.moneyMul);
    }
    if (objId > 4000 && objId < 6000) {
        const int i = objId - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        const Corp& cp = st.corps[i];
        return static_cast<int32_t>((cp.sub * cp.feeTable[0] + cp.buildPrice) * st.moneyMul);
    }
    return 0;
}

// [RE sub_41970F] owner 名下有建筑的 estate 数
int builtEstatesOf(const GameState& st, int owner) {
    int n = 0;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        if (st.estates[i].type != 0 && st.estates[i].owner == static_cast<uint8_t>(owner)) {
            ++n;
        }
    }
    return n;
}

// [RE 0x41E8E6] AI 愿为该地花钱：他人有主有建筑，且（自己在该路段有地 或 属欠债人的 ≥2 级地）
bool aiWorthSpending(const GameState& st, int creditor, int objId) {
    const int cur = st.currentPlayer;
    if (creditor == -1) {
        return false;
    }
    if (objId > 2000 && objId < 4000) {
        const int i = objId - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return false;
        }
        const Estate& es = st.estates[i];
        if (es.owner != 0 && es.owner != cur + 1 && es.level != 0) {
            for (size_t j = 1; j < st.estates.size(); ++j) {
                if (static_cast<int>(j) != i && st.estates[j].owner == static_cast<uint8_t>(cur + 1) &&
                    std::strcmp(st.estates[j].name, es.name) == 0) {
                    return true;
                }
            }
        }
        return es.owner == static_cast<uint8_t>(creditor + 1) && es.level >= 2;
    }
    if (objId > 4000 && objId < 6000) {
        const int i = objId - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return false;
        }
        const Corp& cp = st.corps[i];
        return cp.owner != 0 && cp.owner != cur + 1 && cp.sub != 0;
    }
    return false;
}

// [RE 0x40A45C/word_48B8C4] 视野内对手（mapHitRegions 玩家命中 0xF000|p）
int visibleOpponents(const GameState& st, int out[8]) {
    bool seen[9] = {};
    int n = 0;
    for (const auto& hit : st.mapHitRegions) {
        if ((hit.id & 0xF000) != 0xF000) {
            continue;
        }
        const int p = hit.id & 0x0F;
        if (p == st.currentPlayer || p < 0 || p >= kPlayerCount4 || seen[p] ||
            st.players[p].alive == 0) {
            continue;
        }
        seen[p] = true;
        out[n++] = p;
    }
    return n;
}

int visibleEstates(const GameState& st, int out[256]) {
    bool seen[4096] = {};
    int n = 0;
    for (const auto& hit : st.mapHitRegions) {
        if (hit.id > 2000 && hit.id < 4000) {
            const int i = hit.id - 2000;
            if (i > 0 && i < static_cast<int>(st.estates.size()) && !seen[i]) {
                seen[i] = true;
                out[n++] = i;
            }
        }
    }
    return n;
}

int visibleCorps(const GameState& st, int out[256]) {
    bool seen[4096] = {};
    int n = 0;
    for (const auto& hit : st.mapHitRegions) {
        if (hit.id > 4000 && hit.id < 6000) {
            const int i = hit.id - 4000;
            if (i > 0 && i < static_cast<int>(st.corps.size()) && !seen[i]) {
                seen[i] = true;
                out[n++] = i;
            }
        }
    }
    return n;
}

bool stockClosed(const GameState& st) {
    return st.stockMarketClosed != 0 || stockIsHoliday(st);
}
bool haltedAt(const GameState& st, int i) {
    return i >= 0 && i < 12 && st.stockHalted[i] != 0;
}
bool limitUp(const GameState& st, int i) {
    return i >= 0 && i < 12 && st.stocks[i][7] >= 10.0f;
}
bool limitDown(const GameState& st, int i) {
    return i >= 0 && i < 12 && st.stocks[i][7] <= -10.0f;
}

// ===== 各卡目标选择（不含性格判定；[RE dword_475324[卡]]）=====

int tgtEqualRich(GameState& st) {  // [RE 0x41E6FE]
    int64_t sum = 0;
    int n = 0;
    for (int i = 0; i < st.playerCount && i < kPlayerCount4; ++i) {
        if (st.players[i].alive != 0) {
            sum += st.players[i].cash;
            ++n;
        }
    }
    if (n == 0) {
        return 0;
    }
    const int32_t avg = static_cast<int32_t>(sum / n);
    const int cur = st.currentPlayer;
    if (avg > 10 * st.players[cur].cash && 3000 * st.moneyMul > st.players[cur].cash) {
        return 1;
    }
    return 0;
}

// [RE 0x41E779 均贫 / 0x4202D2 查税]（同构：视野内优先债主，否则最富可见者）
int tgtVisibleByCash(GameState& st, bool preferCreditor) {
    const int cur = st.currentPlayer;
    int vis[8];
    const int n = visibleOpponents(st, vis);
    const int creditor = findMaxCreditor(st, cur);
    if (creditor != -1) {
        for (int i = 0; i < n; ++i) {
            if (vis[i] == creditor && st.players[creditor].cash > 30000 * st.moneyMul &&
                2 * st.players[cur].cash < st.players[creditor].cash) {
                st.aiCardTarget = 0x8000 | (1 << creditor);
                return 1;
            }
        }
    }
    if (preferCreditor) {
        int best = -1;
        int32_t bestCash = 0;
        for (int i = 0; i < n; ++i) {
            const int32_t c = st.players[vis[i]].cash;
            if (c > 50000 * st.moneyMul && c > bestCash && 3 * st.players[cur].cash < c) {
                bestCash = c;
                best = vis[i];
            }
        }
        if (best >= 0) {
            st.aiCardTarget = 0x8000 | (1 << best);
            return 1;
        }
    }
    return 0;
}

int tgtBuyLand(GameState& st) {  // [RE 0x41E9E2]
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    if (objId <= 2000 || objId >= 6000) {
        return 0;
    }
    if (!aiWorthSpending(st, findMaxCreditor(st, cur), objId)) {
        return 0;
    }
    if (landPriceOf(st, objId) >= st.players[cur].cash) {
        return 0;
    }
    return 1;  // 无目标（脚下格）
}

int tgtSwapLand(GameState& st) {  // [RE 0x41EAE2]
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    const int creditor = findMaxCreditor(st, cur);
    if (objId > 2000 && objId < 4000) {
        const int i = objId - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        const Estate& mine = st.estates[i];
        if (mine.owner != cur + 1 || mine.level > 1) {
            return 0;
        }
        int vis[256];
        const int n = visibleEstates(st, vis);
        for (int k = 0; k < n; ++k) {
            const int j = vis[k];
            if (j == i) {
                continue;
            }
            const Estate& es = st.estates[j];
            if (std::strcmp(es.name, mine.name) == 0) {
                if (es.owner == static_cast<uint8_t>(cur + 1)) {
                    return 0;  // 同段自己还有地 → 不换
                }
                continue;
            }
            if (es.priceAdd <= mine.priceAdd || es.level <= mine.level) {
                continue;
            }
            if (aiWorthSpending(st, creditor, 2000 + j)) {
                st.aiCardTarget = 2000 + j;
                return 1;
            }
        }
        return 0;
    }
    if (objId > 4000 && objId < 6000) {
        const int i = objId - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        const Corp& mine = st.corps[i];
        if (mine.owner != cur + 1 || mine.sub > 1) {
            return 0;
        }
        int vis[256];
        const int n = visibleCorps(st, vis);
        for (int k = 0; k < n; ++k) {
            const int j = vis[k];
            if (j == i) {
                continue;
            }
            const Corp& cp = st.corps[j];
            if (cp.buildPrice <= mine.buildPrice || cp.sub <= mine.sub) {
                continue;
            }
            if (aiWorthSpending(st, creditor, 4000 + j)) {
                st.aiCardTarget = 4000 + j;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

int tgtRebuild(GameState& st) {  // [RE 0x41ED3E]
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    const int creditor = findMaxCreditor(st, cur);
    if (objId > 2000 && objId < 4000) {
        const int i = objId - 2000;
        if (i <= 0 || i >= static_cast<int>(st.estates.size())) {
            return 0;
        }
        const Estate& es = st.estates[i];
        if (es.owner != cur + 1) {
            return 0;
        }
        if (es.type != 0) {  // 连锁店：同段还有自己地 → 可改回（返回1）
            for (size_t j = 1; j < st.estates.size(); ++j) {
                if (static_cast<int>(j) != i && st.estates[j].owner == static_cast<uint8_t>(cur + 1) &&
                    std::strcmp(st.estates[j].name, es.name) == 0) {
                    return 1;
                }
            }
            return 0;
        }
        if (es.level != 1) {
            return 0;
        }
        if (st.players[cur].aiPersonality == 0) {
            return 1;  // 乖寶寶：空地直接改连锁
        }
        for (size_t j = 1; j < st.estates.size(); ++j) {
            if (static_cast<int>(j) != i && std::strcmp(st.estates[j].name, es.name) == 0) {
                const uint8_t o = st.estates[j].owner;
                if (o == 0 || o == static_cast<uint8_t>(cur + 1)) {
                    return 0;
                }
            }
        }
        return 1;
    }
    if (objId > 4000 && objId < 6000) {
        const int i = objId - 4000;
        if (i <= 0 || i >= static_cast<int>(st.corps.size())) {
            return 0;
        }
        const Corp& cp = st.corps[i];
        if (cp.owner == static_cast<uint8_t>(cur + 1) && cp.type == 0 && cp.sub == 1) {
            st.aiCardTarget = std::rand() % 4 + 1;  // [RE] 随机设施类型
            return 1;
        }
        if (cp.owner == 0 || cp.owner == static_cast<uint8_t>(cur + 1) || cp.type == 0) {
            return 0;
        }
        if (cp.sub < 3 && (cp.owner != static_cast<uint8_t>(creditor + 1) || cp.sub < 2)) {
            return 0;
        }
        return 1;
    }
    return 0;
}

int tgtAuction(GameState& st) {  // [RE 0x41EF26]
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    const int creditor = findMaxCreditor(st, cur);
    if (objId > 2000 && objId < 4000) {
        const Estate& es = st.estates[objId - 2000];
        if (es.owner != 0 && es.owner != cur + 1 && es.level >= 3) {
            return 1;
        }
        if (es.owner == static_cast<uint8_t>(creditor + 1) && es.level >= 2) {
            return 1;
        }
        return 0;
    }
    if (objId > 4000 && objId < 6000) {
        const Corp& cp = st.corps[objId - 4000];
        if (cp.owner != 0 && cp.owner != cur + 1 && cp.sub >= 3) {
            return 1;
        }
        if (cp.owner == static_cast<uint8_t>(creditor + 1) && cp.sub >= 2) {
            return 1;
        }
        return 0;
    }
    return 0;
}

int tgtAngel(GameState& st) {  // [RE 0x41F037] 自己同段 ≥3 块（type0 level<5）的路段随机一段
    const int cur = st.currentPlayer;
    int vis[256];
    const int n = visibleEstates(st, vis);
    int first[64];
    int cnt[64];
    int m = 0;
    for (int k = 0; k < n; ++k) {
        const int i = vis[k];
        const Estate& es = st.estates[i];
        if (es.owner != cur + 1 || es.type != 0 || es.level >= 5) {
            continue;
        }
        int g = -1;
        for (int t = 0; t < m; ++t) {
            if (std::strcmp(st.estates[first[t]].name, es.name) == 0) {
                g = t;
                break;
            }
        }
        if (g < 0) {
            first[m] = i;
            cnt[m] = 1;
            ++m;
        } else {
            ++cnt[g];
        }
    }
    int cand[64];
    int nc = 0;
    for (int t = 0; t < m; ++t) {
        if (cnt[t] >= 3) {
            cand[nc++] = first[t];
        }
    }
    if (nc == 0) {
        return 0;
    }
    st.aiCardTarget = 2000 + cand[std::rand() % nc];
    return 1;
}

int tgtDevil(GameState& st) {  // [RE 0x41F1B3] 他人路段（债主段 level 和≥2 块≥7 自己≤1 / 否则 level 和≥3 块≥9）
    const int cur = st.currentPlayer;
    const int creditor = findMaxCreditor(st, cur);
    int vis[256];
    const int n = visibleEstates(st, vis);
    // 按路段聚合
    int first[128];
    int m = 0;
    int levelSum[128][9] = {};
    int count[128][9] = {};
    int total[128] = {};
    for (int k = 0; k < n; ++k) {
        const int i = vis[k];
        const Estate& es = st.estates[i];
        if (es.type != 0 || es.owner == 0) {
            continue;
        }
        int g = -1;
        for (int t = 0; t < m; ++t) {
            if (std::strcmp(st.estates[first[t]].name, es.name) == 0) {
                g = t;
                break;
            }
        }
        if (g < 0) {
            first[m] = i;
            g = m;
            ++m;
        }
        const int o = es.owner - 1;
        levelSum[g][o] += es.level;
        ++count[g][o];
        ++total[g];
    }
    for (int t = 0; t < m; ++t) {
        if (creditor != -1) {
            if (levelSum[t][creditor] >= 2 && count[t][creditor] >= 7 && count[t][cur] <= 1) {
                st.aiCardTarget = 2000 + first[t];
                return 1;
            }
        }
        int otherLevel = 0;
        int otherCount = 0;
        for (int o = 0; o < kPlayerCount4; ++o) {
            if (o == cur) {
                continue;
            }
            otherLevel += levelSum[t][o];
            otherCount += count[t][o];
        }
        if (otherLevel >= 3 && otherCount >= 9) {
            st.aiCardTarget = 2000 + first[t];
            return 1;
        }
    }
    return 0;
}

int tgtMonster(GameState& st) {  // [RE 0x41F400] 等级最高的他人建筑（债主优先）
    const int cur = st.currentPlayer;
    const int creditor = findMaxCreditor(st, cur);
    int visE[256];
    int visC[256];
    const int ne = visibleEstates(st, visE);
    const int nc = visibleCorps(st, visC);
    int bestEstateOwnerLevel[9] = {};
    int bestEstateObj[9] = {};
    int bestCorpLevel[9] = {};
    int bestCorpObj[9] = {};
    for (int k = 0; k < ne; ++k) {
        const Estate& es = st.estates[visE[k]];
        if (es.owner == 0 || es.owner == cur + 1 || es.level < 3) {
            continue;
        }
        const int o = es.owner - 1;
        if (es.level > bestEstateOwnerLevel[o]) {
            bestEstateOwnerLevel[o] = es.level;
            bestEstateObj[o] = 2000 + visE[k];
        }
    }
    for (int k = 0; k < nc; ++k) {
        const Corp& cp = st.corps[visC[k]];
        if (cp.owner == 0 || cp.owner == cur + 1 || cp.sub < 3) {
            continue;
        }
        const int o = cp.owner - 1;
        if (cp.sub > bestCorpLevel[o]) {
            bestCorpLevel[o] = cp.sub;
            bestCorpObj[o] = 4000 + visC[k];
        }
    }
    if (creditor != -1) {
        if (bestEstateOwnerLevel[creditor] >= 3) {
            st.aiCardTarget = bestEstateObj[creditor];
            return 1;
        }
        if (bestCorpLevel[creditor] >= 3) {
            st.aiCardTarget = bestCorpObj[creditor];
            return 1;
        }
    }
    int bestLevel = 0;
    int bestObj = 0;
    for (int o = 0; o < kPlayerCount4; ++o) {
        if (o == cur) {
            continue;
        }
        if (bestEstateOwnerLevel[o] >= 4 && bestEstateOwnerLevel[o] > bestLevel) {
            bestLevel = bestEstateOwnerLevel[o];
            bestObj = bestEstateObj[o];
        }
        if (bestCorpLevel[o] >= 3 && bestCorpLevel[o] > bestLevel) {
            bestLevel = bestCorpLevel[o];
            bestObj = bestCorpObj[o];
        }
    }
    if (bestObj != 0) {
        st.aiCardTarget = bestObj;
        return 1;
    }
    return 0;
}

int tgtDemolish(GameState& st) {  // [RE 0x41F6A9] 怪兽优先；否则路面道具/高威胁他人地产
    if (tgtMonster(st) == 1) {
        return 1;
    }
    const int cur = st.currentPlayer;
    // 路面道具（cellTable 16 路障 / 17 地雷；无主或他人）
    for (const auto& hit : st.mapHitRegions) {
        if ((hit.id & 0xFF00) != 0xA100) {
            continue;
        }
        const int slot = hit.id & 0xFF;
        if (slot < 1 || slot > 46) {
            continue;
        }
        const int type = st.cellTable[24 * (slot - 1)];
        if (type != 16 && type != 17) {
            continue;
        }
        const uint16_t ent = static_cast<uint16_t>(st.cellTable[24 * (slot - 1) + 2]);
        if (ent >= st.cellEnts.size()) {
            continue;
        }
        const uint16_t oid = st.cellEnts[ent].special;
        if (oid > 2000 && oid < 4000) {
            const uint8_t o = st.estates[oid - 2000].owner;
            if (o != static_cast<uint8_t>(cur + 1)) {
                st.aiCardTarget = hit.id;
                return 1;
            }
        } else if (oid > 4000 && oid < 6000) {
            const uint8_t o = st.corps[oid - 4000].owner;
            if (o != static_cast<uint8_t>(cur + 1)) {
                st.aiCardTarget = hit.id;
                return 1;
            }
        }
    }
    // 他人有建筑 estate 且该 owner 建筑数 ≥4（personality 非 0）
    if (st.players[cur].aiPersonality != 0) {
        int vis[256];
        const int n = visibleEstates(st, vis);
        for (int k = 0; k < n; ++k) {
            const Estate& es = st.estates[vis[k]];
            if (es.owner == 0 || es.owner == cur + 1 || es.type == 0) {
                continue;
            }
            if (builtEstatesOf(st, es.owner - 1) >= 4) {
                st.aiCardTarget = 2000 + vis[k];
                return 1;
            }
        }
    }
    return 0;
}

int tgtSteal(GameState& st) {  // [RE 0x41F901] 债主/可见对手的最贵卡（目标卡写 aiCardTarget2）
    const int cur = st.currentPlayer;
    int vis[8];
    const int n = visibleOpponents(st, vis);
    const int creditor = findMaxCreditor(st, cur);
    auto bestCardOf = [&st](int owner, int personaWant) -> int {
        int best = 0;
        int bestPrice = 0;
        for (int s = 0; s < 15; ++s) {
            const int card = st.cardState60[15 * owner + s];
            if (card == 0 || kCardAiPersona[card] != personaWant) {
                continue;
            }
            if (kCardPrices[card] > bestPrice) {
                bestPrice = kCardPrices[card];
                best = card;
            }
        }
        return best;
    };
    for (int i = 0; i < n; ++i) {
        if (vis[i] != creditor) {
            continue;
        }
        const int card = bestCardOf(vis[i], 1);  // 债主：persona==1 的最贵卡
        if (card != 0) {
            st.aiCardTarget = 0x8000 | (1 << vis[i]);
            st.aiCardTarget2 = card;
            return 1;
        }
    }
    int bestOwner = -1;
    int bestCard = 0;
    int bestPrice = 0;
    for (int i = 0; i < n; ++i) {
        const int card = bestCardOf(vis[i], 2);  // 其他：persona==2
        if (card != 0 && kCardPrices[card] > bestPrice) {
            bestPrice = kCardPrices[card];
            bestCard = card;
            bestOwner = vis[i];
        }
    }
    if (bestOwner >= 0) {
        st.aiCardTarget = 0x8000 | (1 << bestOwner);
        st.aiCardTarget2 = bestCard;
        return 1;
    }
    return 0;
}

int tgtStay(GameState& st) {  // [RE 0x41FACC] 自己升级位（跳过回合保地）或站在自己产业上的对手
    const int cur = st.currentPlayer;
    const uint16_t objId = standingObjId(st);
    const Player& pl = st.players[cur];
    if (pl.fixedStep == 0) {
        if (objId > 2000 && objId < 4000) {
            const Estate& es = st.estates[objId - 2000];
            const int32_t price = static_cast<int32_t>(es.priceBase) * st.moneyMul;
            if (es.owner == cur + 1 && es.type == 0 && es.level < 5 && es.level < 1 &&
                price < pl.cash && (pl.bank + pl.cash) > 10000 && pl.luckB >= 0) {
                bool sameRouteOther = false;
                for (size_t j = 1; j < st.estates.size(); ++j) {
                    if (static_cast<int>(j) != objId - 2000 && st.estates[j].owner == static_cast<uint8_t>(cur + 1) &&
                        std::strcmp(st.estates[j].name, es.name) == 0) {
                        sameRouteOther = true;
                        break;
                    }
                }
                if (sameRouteOther || es.level >= 2) {
                    st.aiCardTarget = 0x8000 | (1 << cur);
                    return 1;
                }
            }
        } else if (objId > 4000 && objId < 6000) {
            const Corp& cp = st.corps[objId - 4000];
            if (cp.owner == cur + 1 && cp.type != 0 && cp.type != 3 && cp.sub < 5 && pl.luckB >= 0 &&
                (pl.bank + pl.cash) > 10000) {
                st.aiCardTarget = 0x8000 | (1 << cur);
                return 1;
            }
        }
    }
    // 对手站在自己的 specPt / 有设施 corp 上 → 令其停留
    int vis[8];
    const int n = visibleOpponents(st, vis);
    for (int i = 0; i < n; ++i) {
        const Player& q = st.players[vis[i]];
        if (q.cellEntId == 0 || q.cellEntId >= st.cellEnts.size()) {
            continue;
        }
        const uint16_t oid = st.cellEnts[q.cellEntId].special;
        if (oid > 6000 && oid < 8000) {
            const int si = oid - 6000;
            if (si > 0 && si < static_cast<int>(st.specPts.size()) &&
                st.specPts[si].owner == static_cast<uint8_t>(cur + 1)) {
                st.aiCardTarget = 0x8000 | (1 << vis[i]);
                return 1;
            }
        } else if (oid > 4000 && oid < 6000) {
            const Corp& cp = st.corps[oid - 4000];
            if (cp.owner == static_cast<uint8_t>(cur + 1) && cp.type != 0 && cp.sub >= 2) {
                st.aiCardTarget = 0x8000 | (1 << vis[i]);
                return 1;
            }
        }
    }
    return 0;
}

int tgtSleepOrFrame(GameState& st) {  // [RE 0x41FE6F] 可见对手（非冬眠/无復仇卡），债主优先
    const int cur = st.currentPlayer;
    int vis[8];
    const int n = visibleOpponents(st, vis);
    const int creditor = findMaxCreditor(st, cur);
    int cand[8];
    int nc = 0;
    int creditorIdx = -1;
    for (int i = 0; i < n; ++i) {
        const int p = vis[i];
        bool hasRevenge = false;
        for (int s = 0; s < 15; ++s) {
            if (st.cardState60[15 * p + s] == 18) {
                hasRevenge = true;
                break;
            }
        }
        if (st.players[p].byte54 != 0 || hasRevenge) {  // 已冬眠 / 持復仇卡 → 跳过
            continue;
        }
        if (p == creditor) {
            creditorIdx = nc;
        }
        cand[nc++] = p;
    }
    if (nc == 0) {
        return 0;
    }
    const int pick = (creditorIdx >= 0) ? cand[creditorIdx] : cand[std::rand() % nc];
    st.aiCardTarget = 0x8000 | (1 << pick);
    return 1;
}

int tgtBanish(GameState& st) {  // [RE 0x41FF77]
    const Player& pl = st.players[st.currentPlayer];
    if (pl.cellTableIdx != 0) {
        const int type = st.cellTable[24 * (pl.cellTableIdx - 1)];
        if (type == 5 || type == 6 || type == 7 || type == 8 || type == 10 || type == 15) {
            return 1;
        }
        return 0;
    }
    if (pl.cellNo != 0 && st.cellTable[24 * (pl.cellNo - 1) + 4] < 13) {
        return 1;
    }
    return 0;
}

int tgtInvite(Application& app) {  // [RE 0x41FFF8]
    GameState& st = app.gameState();
    const int a = st.players[st.currentPlayer].cellTableIdx;
    if (a == 1 || a == 2 || a == 3 || a == 4 || a == 12) {
        return 0;  // 已有正面神
    }
    const int obj = pickNearestAttachable(app);
    if (obj == 0) {
        return 0;
    }
    st.aiCardTarget = obj;
    return 1;
}

int tgtStockRed(GameState& st) {  // [RE 0x420055] 自己市值最大的未停牌/未涨持股
    const int cur = st.currentPlayer;
    if (stockClosed(st)) {
        return 0;
    }
    int best = -1;
    int64_t bestVal = 0;
    for (int i = 0; i < 12; ++i) {
        const int64_t val = static_cast<int64_t>(st.playerShares[cur][i]) *
                            static_cast<int64_t>(st.playerAvgCost[cur][i]);
        if (st.playerShares[cur][i] != 0 && val > bestVal && !haltedAt(st, i) && !limitUp(st, i)) {
            bestVal = val;
            best = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    st.aiCardTarget = best;  // 0-based 股票索引
    return 1;
}

int tgtStockBlack(GameState& st) {  // [RE 0x4200EA] 持股最多对手/债主的未持股股票
    const int cur = st.currentPlayer;
    if (stockClosed(st)) {
        return 0;
    }
    // 各玩家持有的上市公司数
    int hold[4] = {};
    for (int o = 0; o < kPlayerCount4; ++o) {
        for (int i = 0; i < 12; ++i) {
            if (st.stocks[i][1] != 0.0f && st.playerShares[o][i] != 0) {
                ++hold[o];
            }
        }
    }
    int top = -1;
    for (int o = 0; o < kPlayerCount4; ++o) {
        if (o != cur && st.players[o].alive != 0 && (top < 0 || hold[o] > hold[top])) {
            top = o;
        }
    }
    auto bestOf = [&st, cur](int owner) -> int {
        int best = -1;
        int64_t bestVal = 0;
        for (int i = 0; i < 12; ++i) {
            const int64_t val = static_cast<int64_t>(st.playerShares[owner][i]) *
                                static_cast<int64_t>(st.playerAvgCost[owner][i]);
            if (st.playerShares[owner][i] != 0 && st.stocks[i][1] != 0.0f && !haltedAt(st, i) &&
                !limitDown(st, i) && st.playerShares[cur][i] == 0 && val > bestVal) {
                bestVal = val;
                best = i;
            }
        }
        return best;
    };
    int pick = -1;
    const int creditor = findMaxCreditor(st, cur);
    if (creditor != -1) {
        pick = bestOf(creditor);
    }
    if (pick < 0 && top >= 0) {
        pick = bestOf(top);
    }
    if (pick < 0) {
        return 0;
    }
    st.aiCardTarget = pick;
    return 1;
}

int tgtPriceUp(GameState& st) {  // [RE 0x42040E] 自己占比 ≥0.68 的路段 / 自己高级 corp
    const int cur = st.currentPlayer;
    const int creditor = findMaxCreditor(st, cur);
    int visC[256];
    const int nc = visibleCorps(st, visC);
    for (int k = 0; k < nc; ++k) {
        const Corp& cp = st.corps[visC[k]];
        if (cp.owner == static_cast<uint8_t>(cur + 1) && cp.type != 0 && cp.type != 4 &&
            cp.sub >= 3) {
            st.aiCardTarget = 4000 + visC[k];
            return 1;
        }
    }
    int vis[256];
    const int n = visibleEstates(st, vis);
    int first[128];
    int m = 0;
    for (int k = 0; k < n; ++k) {
        const int i = vis[k];
        int g = -1;
        for (int t = 0; t < m; ++t) {
            if (std::strcmp(st.estates[first[t]].name, st.estates[i].name) == 0) {
                g = t;
                break;
            }
        }
        if (g >= 0) {
            continue;
        }
        first[m] = i;
        g = m;
        ++m;
        // 聚合该段
        int ownLevel = 0;
        int ownBlocks = 0;
        int otherLevel = 0;
        int total = 0;
        bool creditorHere = false;
        for (size_t j = 1; j < st.estates.size(); ++j) {
            if (std::strcmp(st.estates[j].name, st.estates[i].name) != 0) {
                continue;
            }
            ++total;
            const uint8_t o = st.estates[j].owner;
            if (o == static_cast<uint8_t>(cur + 1)) {
                ownLevel += st.estates[j].level;
                ++ownBlocks;
            } else if (o != 0) {
                otherLevel += st.estates[j].level;
            }
            if (creditor != -1 && o == static_cast<uint8_t>(creditor + 1)) {
                creditorHere = true;
            }
        }
        if (!creditorHere && ownLevel >= 7 && otherLevel <= 3 && total > 0 &&
            static_cast<double>(ownBlocks) / total >= 0.682) {
            st.aiCardTarget = 2000 + i;
            return 1;
        }
    }
    return 0;
}

int tgtSeal(GameState& st) {  // [RE 0x42062B] 债主高级设施 / 他人 level 和 ≥7 路段
    const int cur = st.currentPlayer;
    const int creditor = findMaxCreditor(st, cur);
    int visC[256];
    const int nc = visibleCorps(st, visC);
    for (int k = 0; k < nc; ++k) {
        const Corp& cp = st.corps[visC[k]];
        if (creditor != -1 && cp.owner == static_cast<uint8_t>(creditor + 1) && cp.type != 0 &&
            cp.sub >= 3) {
            st.aiCardTarget = 4000 + visC[k];
            return 1;
        }
    }
    int vis[256];
    const int n = visibleEstates(st, vis);
    int first[128];
    int m = 0;
    for (int k = 0; k < n; ++k) {
        const int i = vis[k];
        int g = -1;
        for (int t = 0; t < m; ++t) {
            if (std::strcmp(st.estates[first[t]].name, st.estates[i].name) == 0) {
                g = t;
                break;
            }
        }
        if (g >= 0) {
            continue;
        }
        first[m] = i;
        ++m;
        int otherLevel = 0;
        bool own = false;
        for (size_t j = 1; j < st.estates.size(); ++j) {
            if (std::strcmp(st.estates[j].name, st.estates[i].name) != 0) {
                continue;
            }
            const uint8_t o = st.estates[j].owner;
            if (o == static_cast<uint8_t>(cur + 1)) {
                own = true;
            } else if (o != 0) {
                otherLevel += st.estates[j].level;
            }
        }
        if (!own && otherLevel >= 7) {
            st.aiCardTarget = 2000 + i;
            return 1;
        }
    }
    return 0;
}

int tgtAlly(GameState& st) {  // [RE 0x4207CC] 可见（非债主、非已盟）地产最多者结盟
    const int cur = st.currentPlayer;
    int vis[8];
    const int n = visibleOpponents(st, vis);
    const int creditor = findMaxCreditor(st, cur);
    int best = -1;
    int bestCount = 0;
    for (int i = 0; i < n; ++i) {
        const int p = vis[i];
        if (p == creditor || st.players[p].ally == static_cast<uint8_t>(cur + 1)) {
            continue;
        }
        int cnt = 0;
        for (size_t j = 1; j < st.estates.size(); ++j) {
            if (st.estates[j].owner == static_cast<uint8_t>(p + 1)) {
                ++cnt;
            }
        }
        for (size_t j = 1; j < st.corps.size(); ++j) {
            if (st.corps[j].owner == static_cast<uint8_t>(p + 1)) {
                ++cnt;
            }
        }
        if (cnt > bestCount) {
            bestCount = cnt;
            best = p;
        }
    }
    if (best < 0) {
        return 0;
    }
    st.aiCardTarget = 0x8000 | (1 << best);
    return 1;
}

int tgtTurtle(GameState& st) {  // [RE 0x420970] 自己路段产值高 → 自己；否则踩在自己高产值段的对手
    const int cur = st.currentPlayer;
    // 自己：视口内自己的产业租金和
    int64_t myValue = 0;
    int myBlocks = 0;
    for (const auto& hit : st.mapHitRegions) {
        if (hit.id > 2000 && hit.id < 4000) {
            const Estate& es = st.estates[hit.id - 2000];
            if (es.owner == static_cast<uint8_t>(cur + 1)) {
                myValue += es.fees[es.level < 6 ? es.level : 0];
                ++myBlocks;
            }
        }
    }
    const Player& pl = st.players[cur];
    // [RE 0x463D40 = 1.5] 自己产值×1.5 ≥ 现金 → 对自己用（原版 dbl_463D40）
    if (static_cast<double>(myValue) * 1.5 >= static_cast<double>(pl.cash) && myBlocks >= 2 &&
        (pl.bank + pl.cash) > 10000 && pl.luckB >= 0) {
        st.aiCardTarget = 0x8000 | (1 << cur);
        return 1;
    }
    int vis[8];
    const int n = visibleOpponents(st, vis);
    for (int i = 0; i < n; ++i) {
        if (st.players[vis[i]].byte54 != 0) {
            continue;
        }
        st.aiCardTarget = 0x8000 | (1 << vis[i]);
        return 1;
    }
    return 0;
}

}  // namespace

int aiCardSelect(Application& app, int cardId) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= kPlayerCount4 || cardId < 1 || cardId > 30) {
        return 0;
    }
    // [RE 0x41E69E] 性格判定：persona - aiPersonality >= 2 → 不用；== 1 时 1/3 概率
    const int diff = static_cast<int>(kCardAiPersona[cardId]) -
                     static_cast<int>(st.players[cur].aiPersonality);
    if (diff >= 2) {
        return 0;
    }
    if (diff == 1 && (std::rand() % 3) != 0) {
        return 0;
    }
    st.aiCardTarget = 0;
    st.aiCardTarget2 = 0;
    int r = 0;
    switch (cardId) {
        case 1:  r = tgtEqualRich(st); break;
        case 2:  r = tgtVisibleByCash(st, true); break;
        case 3:  r = tgtBuyLand(st); break;
        case 4:  r = tgtSwapLand(st); break;
        case 5:  return 0;  // [RE 表项 0x41E6E3] 换屋：AI 不用
        case 6:  return 0;  // [RE 表项 0x41E6E3] 转向：AI 不用
        case 7:  r = tgtRebuild(st); break;
        case 8:  r = tgtAuction(st); break;
        case 9:  r = tgtAngel(st); break;
        case 10: r = tgtDevil(st); break;
        case 11: r = tgtMonster(st); break;
        case 12: r = tgtDemolish(st); break;
        case 13: r = tgtSteal(st); break;
        case 14: r = tgtStay(st); break;
        case 15: r = (std::rand() % 4 == 0) ? 1 : 0; break;  // [RE 0x41FE4E]
        case 16: case 17: r = tgtSleepOrFrame(st); break;
        case 18: case 19: case 20: case 21:
            return 0;  // [RE 表项 0x41E6E3] 被动卡：AI 不主动使用
        case 22: r = tgtBanish(st); break;
        case 23: r = tgtInvite(app); break;
        case 24: r = tgtStockRed(st); break;
        case 25: r = tgtStockBlack(st); break;
        case 26: r = tgtVisibleByCash(st, true); break;
        case 27: r = tgtPriceUp(st); break;
        case 28: r = tgtSeal(st); break;
        case 29: r = tgtAlly(st); break;
        case 30: r = tgtTurtle(st); break;
        default: return 0;
    }
    if (r == 1) {
        RICH4_LOGI("aiCardSelect: p%d card=%d target=0x%X target2=%d (RE 0x41E69E/table)",
                   cur, cardId, st.aiCardTarget, st.aiCardTarget2);
        return 1;
    }
    return 0;
}

}  // namespace rich4
