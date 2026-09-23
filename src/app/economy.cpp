#include "game/app/economy.h"

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
        // 银行/系统（原版 dword_499080 总资金，重写未建模）
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
