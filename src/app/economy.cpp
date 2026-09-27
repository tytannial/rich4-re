#include "game/app/economy.h"

#include <cstdlib>
#include <vector>

#include "game/app/game_panel.h"
#include "game/app/map_objects.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/core/log.h"

namespace rich4 {

// [RE 0x40D293] bitScanPlayer
// 依据: 0x40D293 反编译; 掩码低 8 位中第一个置位的玩家号
int bitScanPlayer(uint32_t mask) {
    if ((mask & 0xFFu) == 0) {
        return -1;
    }
    for (int i = 0; i < 8; ++i) {
        if (mask & (1u << i)) {
            return i;
        }
    }
    return -1;
}

// [RE 0x4239B9] playerTotalAssets
// 依据: 0x4239B9 反编译; bank + cash - loan + Σ(12股×现价 flt_496994) +
//   地产(priceAdd(+28) + [type(+24)? priceBase(+30) : level(+26)? priceBase×level]) +
//   公司(buildPrice(+34) + feeTable[0](+36) × sub(+26))；胜利判定输入
int32_t playerTotalAssets(Application& app, int player) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4) {
        return 0;
    }
    int32_t total =
        st.players[player].bank + st.players[player].cash - st.players[player].loan;
    for (int i = 0; i < 12; ++i) {
        total += static_cast<int32_t>(static_cast<double>(st.playerShares[player][i]) *
                                      st.stocks[i][5]);
    }
    for (size_t i = 1; i < st.estates.size(); ++i) {
        const Estate& es = st.estates[i];
        if (es.owner != player + 1) {
            continue;
        }
        total += es.priceAdd;
        if (es.type != 0) {
            total += es.priceBase;
        } else if (es.level != 0) {
            total += static_cast<int32_t>(es.priceBase) * es.level;
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        const Corp& cp = st.corps[i];
        if (cp.owner == player + 1) {
            total += cp.buildPrice + static_cast<int32_t>(cp.feeTable[0]) * cp.sub;
        }
    }
    return total;
}

// [RE 0x423ACF] updateMoneyIndex
// 依据: 0x423ACF 反编译; g_moneyMul = max(旧值, Σ存活 playerTotalAssets / 存活数 / g_startMoneyVal)
//   （只升不降；advanceDay 每日调用 0x41CFBF）
// 差异: 存活数为 0 / 起始资金 ≤0 时保持原值（原版未防护除零）
void updateMoneyIndex(Application& app) {
    GameState& st = app.gameState();
    int64_t sum = 0;
    int alive = 0;
    for (int i = 0; i < st.playerCount && i < 8; ++i) {
        if (st.players[i].alive) {
            sum += playerTotalAssets(app, i);
            ++alive;
        }
    }
    if (alive == 0 || st.startMoneyVal <= 0) {
        return;
    }
    const int32_t avg = static_cast<int32_t>(sum / alive);
    const int32_t idx = avg / st.startMoneyVal;
    if (idx > st.moneyMul) {
        st.moneyMul = idx;
    }
    RICH4_LOGI("money index: avg %d start %d -> mul %d (RE 0x423ACF)", avg, st.startMoneyVal,
               st.moneyMul);
}

// [RE 0x433BD8] payFromBank
// 依据: 0x433BD8 反编译; bank -= amount；不足 → cash += bank(负值) → bank = 0；
//   cash < 0 → cash = 0 + eliminatePlayer（0x40CD87）
void payFromBank(Application& app, int player, int32_t amount) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 9 || amount <= 0) {
        return;
    }
    Player& pl = st.players[player];
    pl.bank -= amount;
    if (pl.bank < 0) {
        pl.cash += pl.bank;
        pl.bank = 0;
        if (pl.cash < 0) {
            pl.cash = 0;
            eliminatePlayer(app, player);
        }
    }
    RICH4_LOGI("payFromBank: player %d amount %d -> bank %d cash %d (RE 0x433BD8)", player, amount,
               pl.bank, pl.cash);
}

// [RE 0x41D2C6] transferMoney
// 依据: 0x41D2C6 反编译; 详见 economy.h 注释
void transferMoney(Application& app, int from, int to, int32_t amount, int flags) {
    GameState& st = app.gameState();
    int32_t paid = amount;
    constexpr int kPlayerSlots = 9; // g_players 固定 9 槽（0..3 玩家 / 4..8 事件槽）
    const int playerCount = kPlayerSlots;

    if (from > 100) {
        // 特殊点付款（索引 = from - 100，原版 52*(a1-100) 表项 +40/+44）
        const int idx = from - 100;
        if (idx >= 0 && idx < static_cast<int>(st.specPts.size())) {
            st.specPts[idx].fund -= amount;
            st.specPts[idx].fundPaid -= amount;
        }
    } else if (from >= 0 && from < playerCount) {
        Player& payer = st.players[from];
        if ((flags & 4) != 0) {
            // 从银行扣，不足由现金补
            payer.bank -= amount;
            if (payer.bank < 0) {
                payer.cash += payer.bank;
                payer.bank = 0;
                if (payer.cash < 0) {
                    paid = payer.cash + amount;
                    payer.cash = 0;
                    if (paid < 0) {
                        paid = 0;
                    }
                    eliminatePlayer(app, from); // [RE 0x41D376] sub_40CD87
                }
            }
        } else {
            // 从现金扣，不足由银行存款补
            payer.cash -= amount;
            if (payer.cash < 0) {
                payer.bank += payer.cash;
                payer.cash = 0;
                if (payer.bank < 0) {
                    paid = payer.bank + amount;
                    payer.bank = 0;
                    if (paid < 0) {
                        paid = 0;
                    }
                    eliminatePlayer(app, from);
                }
            }
        }
        // [RE 0x41D2C6] 付款方"本月意外損失"累计（结息悲情人物排名 sub_437D1A）
        payer.monthSettleA += paid;
    }

    if (to == -1) {
        // [RE 0x41D38C] 系统公库：卖股入系统 / 意外损失 / 乐透奖金池（帮助 idx36）
        st.publicFund += paid;
    } else if (to <= 100) {
        if (to >= 0 && to < playerCount) {
            if ((flags & 1) != 0) {
                st.players[to].cash += paid;
            } else {
                st.players[to].bank += paid;
            }
            // [RE 0x41D2C6] 收款方"本月意外之財"累计（结息悲情人物排名 sub_437D1A）
            st.players[to].monthSettleB += paid;
        }
    } else {
        const int idx = to - 100;
        if (idx >= 0 && idx < static_cast<int>(st.specPts.size())) {
            st.specPts[idx].fund += paid;
            st.specPts[idx].fundPaid += paid;
        }
    }
    RICH4_LOGI("transferMoney: %d -> %d amount %d paid %d flags %d (RE 0x41D2C6)", from, to, amount,
               paid, flags);
    // [RE 0x41D2C6 尾] 付款方=当前玩家（0..8）且存活 → 立即重绘玩家面板
    //   （0x41D433；内部 modalDepth<=1 守卫，事件模态内跳过）
    if (from == st.currentPlayer && from >= 0 && from < 9 && st.players[from].alive != 0) {
        refreshPlayerPanelFor(app, from);
    }
}

// [RE 0x40DF69] addPlayerDebt：欠款矩阵累加 debt[creditor][debtor] += amount（clamp ≥0）
// 依据: 0x40DF69 反编译；creditor==debtor 跳过；amount<0 且原债为 0 跳过；
//   amount>0 且 creditor 的同盟 == debtor+1 → clearAllyPair(creditor) 解除同盟
//   （清双方 ally 与 allyActive/+61）；**不直接扣现金**（清偿点=破产 sub_40CD87 [TODO]）
void addPlayerDebt(GameState& st, int creditor, int debtor, int32_t amount) {
    if (creditor == debtor || creditor < 0 || creditor > 7 || debtor < 0 || debtor > 7) {
        return;
    }
    if (amount < 0 && st.playerDebt[creditor][debtor] == 0) {
        return;
    }
    int32_t v = st.playerDebt[creditor][debtor] + amount;
    if (v < 0) {
        v = 0;
    }
    st.playerDebt[creditor][debtor] = v;
    // [RE 0x40DFCC] 记债命中同盟 → 解除（sub_40CC1A：清双方 +65/+61）
    if (amount > 0 && creditor < 9 && debtor < 9 &&
        st.players[creditor].ally == static_cast<uint8_t>(debtor + 1)) {
        clearAllyPair(st, creditor);
        RICH4_LOGI("addPlayerDebt: ally %d/%d dissolved by debt (RE 0x40CC1A)", creditor, debtor);
    }
    RICH4_LOGI("addPlayerDebt: %d owes %d += %d (total %d, RE 0x40DF69)", debtor, creditor, amount,
               v);
}

// [RE 0x40CC1A] clearAllyPair：解除 p 的同盟（清 p 与对象双方 ally/allyActive）
void clearAllyPair(GameState& st, int p) {
    if (p < 0 || p >= 9) {
        return;
    }
    const int a = st.players[p].ally;
    if (a == 0) {
        return;
    }
    const int other = a - 1;
    if (other >= 0 && other < 9 && st.players[other].ally == static_cast<uint8_t>(p + 1)) {
        st.players[other].ally = 0;
        st.players[other].allyActive = 0;
    }
    st.players[p].ally = 0;
    st.players[p].allyActive = 0;
}

// [RE 0x40D2D3] findMaxCreditor：p 行欠款矩阵（[p][i]，即"欠 p 最多"）中最大且存活的玩家
int findMaxCreditor(const GameState& st, int player) {
    if (player < 0 || player >= 8) {
        return -1;
    }
    int best = -1;
    int32_t maxDebt = 0;
    const int n = st.playerCount < 8 ? st.playerCount : 8;
    for (int i = 0; i < n; ++i) {
        if (i != player && st.players[i].alive != 0 && st.playerDebt[player][i] > maxDebt) {
            maxDebt = st.playerDebt[player][i];
            best = i;
        }
    }
    return best;
}

// [RE 0x40D31C] pickRandomActiveTarget：随机存活且无状态的非自己玩家
int pickRandomActiveTarget(const GameState& st, int exclude) {
    int ids[8];
    int n = 0;
    const int count = st.playerCount < 8 ? st.playerCount : 8;
    for (int i = 0; i < count; ++i) {
        if (i != exclude && st.players[i].alive != 0 && st.players[i].stateFlags == 0) {
            ids[n++] = i;
        }
    }
    if (n == 0) {
        return -1;
    }
    return ids[std::rand() % n];
}

// [RE 0x40E14D] releaseCellTableSlot = deleteMapObject
// 依据: 0x40E14D 反编译；详见 map-object-refresh.md §2.1/§6。流程：
//   type<16 → 拥有者属性回退（cellTableIdx 清 + luckA/B/C −= kLuck*[type]）
//   type16/17 → 回收计数 trapStock +1（**不走**属性回退分支）
//   type18 → trapStock+1 + 拥有者 cellNo 清
//   无主 → cellEnt+38 物件占用清；清槽（cellEnt/life/owner）
//   slot<12 → **配对轮替**：在对向槽以 randomCellEnt(旧格,距>300) 重建配对类型
// 差异: 原版 type16/17 也跳回退（trap 表恒 0，等价）；原版 a1（距离参数）传垃圾值，忽略
void releaseCellTableSlot(Application& app, int slot) {
    GameState& st = app.gameState();
    if (slot <= 0) {
        return;
    }
    const int idx = slot - 1;
    const size_t off = static_cast<size_t>(24 * idx);
    if (off + 24 > st.cellTable.size()) {
        return;
    }
    const uint8_t type = st.cellTable[off];
    const uint16_t entId =
        static_cast<uint16_t>(st.cellTable[off + 2] | (st.cellTable[off + 3] << 8));
    const uint8_t occ = st.cellTable[off + 5];
    if (type < 16) {
        if (occ != 0 && occ - 1 < 9) {
            Player& owner = st.players[occ - 1];
            owner.cellTableIdx = 0;
            if (type < 18) {
                owner.luckA -= kLuckA[type];
                owner.luckB -= kLuckB[type];
                owner.luckC -= kLuckC[type];
            }
        }
    } else if (type == 16) {
        ++st.trapStock[0];
    } else if (type == 17) {
        ++st.trapStock[1];
    } else if (type == 18) {
        ++st.trapStock[2];
        if (occ != 0 && occ - 1 < 9) {
            st.players[occ - 1].cellNo = 0;
        }
    }
    if (occ == 0 && entId != 0 && entId < st.cellEnts.size()) {
        st.cellEnts[entId].occMask &= ~0x00FF0000u;  // [RE 0x40E243] cellEnt+38 = 0
    }
    st.cellTable[off + 2] = 0;
    st.cellTable[off + 3] = 0;
    st.cellTable[off + 4] = 0;
    st.cellTable[off + 5] = 0;
    // [RE 0x40E278] 槽 0..11 配对轮替（小↔大、福神↔窮神按对、惡犬↔土地公…）
    if (idx < 12) {
        const int pairType = ((idx & 1) != 0) ? idx + 1 : idx + 2;
        const int newEnt = randomCellEnt(st, entId);
        if (newEnt > 0) {
            createMapObject(st, pairType, newEnt, 0, 0);
        }
    }
    RICH4_LOGI("deleteMapObject: slot %d type %u ent %u occ %u (RE 0x40E14D)", slot, type, entId,
               occ);
}

// [RE 0x40AC7B] expireAssets：范围资产清算（飞弹 expireAssets(100,38,0,cur) /
//   核弹 expireAssets(-1,38,1,cur)）
// 依据: 0x40AC7B 反编译；范围采集 = sub_40A45C 读 pickBuffer（每物件 1 像素）——重写按
//   世界坐标 (±radius，-1=全图) 遍历 cellEnt 等价收集（玩家掩码 0x8000|mask、物件槽
//   BYTE1、estate/corp/specPt 段 id）；
//   flags&2 住宅 / flags&4 商業：dump≠0 归公（owner 欠 30×M×level，owner/level/type 清零）；
//   dump=0 降级（owner 欠 30×M，level/sub--，连锁 type 归零）；
//   flags&0x20（id&0x8000）：低 4 位玩家 damagePlayer / bit4-7 事件槽 hospitalize /
//   BYTE1&0x7F 物件 deleteMapObject（重写 releaseCellTableSlot）
void expireAssets(Application& app, int cx, int cy, int radius, int flags, int dump, int payer) {
    GameState& st = app.gameState();
    std::vector<uint16_t> ids;
    const int n = static_cast<int>(st.cellEnts.size());
    for (int i = 1; i < n; ++i) {
        const CellEnt& ce = st.cellEnts[i];
        if (radius != -1 &&
            (std::abs(ce.x - cx) >= radius || std::abs(ce.y - cy) >= radius)) {
            continue;
        }
        const uint8_t pmask = static_cast<uint8_t>((ce.occMask >> 8) & 0xFF);
        if (pmask != 0) {
            ids.push_back(static_cast<uint16_t>(0x8000 | pmask));
        }
        const uint8_t slotHi = static_cast<uint8_t>((ce.occMask >> 16) & 0xFF);
        if ((flags & 0x20) != 0 && slotHi != 0) {
            ids.push_back(static_cast<uint16_t>(0x8000 | (slotHi << 8)));
        }
        if (ce.special >= 2000) {
            ids.push_back(ce.special);
        }
    }
    for (const uint16_t v13 : ids) {
        if ((flags & 2) != 0 && v13 > 2000 && v13 < 4000) {
            const int ei = v13 - 2000;
            if (ei > 0 && ei < static_cast<int>(st.estates.size())) {
                Estate& es = st.estates[ei];
                if (dump != 0) {
                    if (payer != -1 && es.owner != 0) {
                        addPlayerDebt(st, es.owner - 1, payer,
                                      st.moneyMul * 30 * es.level);
                    }
                    es.owner = 0;
                    es.level = 0;
                    es.type = 0;
                    es.expireDate = 0;
                    buildMiniMapMarks(app);  // [RE 0x40AD80] rebuildMiniMap(0)
                } else {
                    if (payer != -1 && es.owner != 0) {
                        addPlayerDebt(st, es.owner - 1, payer, 30 * st.moneyMul);
                    }
                    if (es.level != 0) {
                        --es.level;
                    }
                    if (es.type != 0) {
                        es.level = 0;
                        es.type = 0;
                    }
                }
            }
        }
        if ((flags & 4) != 0 && v13 > 4000 && v13 < 6000) {
            const int ci = v13 - 4000;
            if (ci > 0 && ci < static_cast<int>(st.corps.size())) {
                Corp& cp = st.corps[ci];
                if (dump != 0) {
                    if (payer != -1 && cp.owner != 0) {
                        addPlayerDebt(st, cp.owner - 1, payer,
                                      st.moneyMul * 30 * cp.sub);
                    }
                    cp.owner = 0;
                    cp.sub = 0;
                    cp.type = 0;
                    cp.expireDate = 0;
                    buildMiniMapMarks(app);
                } else {
                    if (payer != -1 && cp.owner != 0) {
                        addPlayerDebt(st, cp.owner - 1, payer, 30 * st.moneyMul);
                    }
                    if (cp.sub != 0) {
                        --cp.sub;
                    }
                    if (cp.sub == 0) {
                        cp.type = 0;
                    }
                }
            }
        }
        if ((flags & 0x20) != 0 && (v13 & 0x8000) != 0) {
            const uint8_t lo = static_cast<uint8_t>(v13 & 0xF);
            if (lo != 0) {
                for (int j = 0; j < 4; ++j) {
                    if ((lo & (1 << j)) != 0) {
                        damagePlayer(app, j);  // [RE 0x40AEA2]
                    }
                }
            }
            const uint8_t hi = static_cast<uint8_t>((v13 >> 4) & 0xF);
            if (hi != 0) {
                for (int k = 4; k < 8; ++k) {
                    if ((hi & (1 << (k - 4))) != 0) {
                        hospitalizePlayer(app, k, 0);  // [RE 0x40AED6] 事件槽
                    }
                }
            }
            const uint8_t slot = static_cast<uint8_t>((v13 >> 8) & 0x7F);
            if (slot != 0) {
                releaseCellTableSlot(app, slot);  // [RE 0x40AEFC] deleteMapObject
            }
        }
    }
    RICH4_LOGI("expireAssets: range(%d,%d,r%d) flags %d dump %d payer %d -> %zu ids (RE 0x40AC7B)",
               cx, cy, radius, flags, dump, payer, ids.size());
}

// [RE 0x40CD87] eliminatePlayer
// 依据: 0x40CD87 反编译; 详见 economy.h 注释
void eliminatePlayer(Application& app, int p) {
    GameState& st = app.gameState();
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    if (!pl.alive) {
        return;
    }
    // 原版 cellEnt +36 |= 256<<p（死亡占用位；重写用于出生点排除）
    if (pl.cellEntId < st.cellEnts.size()) {
        st.cellEnts[pl.cellEntId].occMask |= (256u << p);
    }
    pl.alive = 0;
    // [RE 0x40CE22/0x40CE28] 清破产者監獄/醫院标志
    st.jailFlags[p] = 0;
    st.hospitalFlags[p] = 0;
    // [RE 0x40CE74] 破产者若有同盟 → clearAllyPair 解除（清双方 +65/+61）
    if (pl.ally != 0) {
        clearAllyPair(st, p);
        RICH4_LOGI("eliminatePlayer: ally of p%d dissolved (RE 0x40CE74)", p);
    }
    // [RE 0x40CE3E..0x40CEE8] 保释人破产 → 其保释的事件槽 NPC 自动关回原生建筑
    //   条件 = 自由（busy==0，原版 !byte_498E32）且 bailer==破产者（byte_498E30）:
    //   槽 0/1（小偷/強盜）→ jailPlayer；槽 2/3（流氓/間諜）→ hospitalizePlayer
    for (int i = 0; i < 2; ++i) {
        if (st.npcSlots[i].busy == 0 && st.npcSlots[i].bailer == p) {
            jailPlayer(app, i + 4, 0);
        }
    }
    for (int i = 2; i < 4; ++i) {
        if (st.npcSlots[i].busy == 0 && st.npcSlots[i].bailer == p) {
            hospitalizePlayer(app, i + 4, 0);
        }
    }
    if (pl.cellTableIdx) {
        releaseCellTableSlot(app, pl.cellTableIdx);
    }
    if (pl.cellNo) {
        releaseCellTableSlot(app, pl.cellNo);
    }
    // 清空名下资产（原版 g_estates/g_corps/g_specPts 遍历）
    for (auto& e : st.estates) {
        if (e.owner == p + 1) {
            e.owner = 0;
            e.expireDate = 0;
        }
    }
    for (auto& c : st.corps) {
        if (c.owner == p + 1) {
            c.owner = 0;
            c.expireDate = 0;
        }
    }
    for (auto& s : st.specPts) {
        if (s.owner == p + 1) {
            s.owner = 0;
        }
    }
    // [RE 0x40D1AA] 破产者所购乐透号码作废（g_miscTable36A[n] == p+1 → 0，不退款）
    for (int n = 0; n < 36; ++n) {
        if (st.lotteryNumbers[n] == p + 1) {
            st.lotteryNumbers[n] = 0;
        }
    }
    // 失败流程（原版：无存活玩家 → 1；仅剩人类 → 2/3；重写仅在全灭时回主菜单）
    int aliveCount = 0;
    for (int i = 0; i < st.playerCount && i < 9; ++i) {
        if (st.players[i].alive) {
            ++aliveCount;
        }
    }
    if (aliveCount == 0) {
        st.sceneRequest = 1;
    }
    RICH4_LOGI("eliminatePlayer: %d alive %d scene %d (RE 0x40CD87, simplified)", p, aliveCount,
               st.sceneRequest);
}

} // namespace rich4
