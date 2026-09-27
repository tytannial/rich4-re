#include "game/app/economy.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/app/auction_dialog.h"
#include "game/app/game_loop.h"
#include "game/app/game_panel.h"
#include "game/app/magic_house_dialog.h"
#include "game/app/map_objects.h"
#include "game/app/stock_system.h"
#include "game/app/target_select_dialog.h"
#include "game/app/turn_system.h"
#include "game/application.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/core/log.h"
#include "game/render/blit.h"

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
            createMapObject(app, pairType, newEnt, 0, 0);
        }
    }
    RICH4_LOGI("deleteMapObject: slot %d type %u ent %u occ %u (RE 0x40E14D)", slot, type, entId,
               occ);
}

// [RE 0x40AC7B] expireAssets：范围资产清算（飞弹 expireAssets(100,38,0,cur) /
//   核弹 expireAssets(-1,38,1,cur)）
// 依据: 0x40AC7B 反编译；范围采集 = sub_40A45C：
//   ① sub_409DE7 清 pickBuffer 后用 **g_drawList（上次 sub_40829D 的绘制列表）按对象
//      **锚点像素**重建（每对象 1 像素，屏幕坐标）；
//   ② 扫 (220±radius)² 方块（radius==-1 = 全 440×440 缓冲 = 屏幕 y40..480）收集非零 id；
//   ③ rebuildPickBuffer(1) 恢复。
//   重写 mapHitRegions = g_drawList 等价（上次 renderGameFrame 的绘制记录，屏幕坐标；
//   anchorX/anchorY 即 drawListX/Y）。**调用方必须先做 refreshGameUi(x,y,0) 等价的重绘把
//   视口对准目标**（launchMissile 的 renderGameFrame / news focusObjId 已做——原版
//   refreshGameUi(x,y,0) 经 sub_415E70→sub_40829D 即重绘新视口，非"只设视口"）。
//   拾取编码经 normalizeHitId 转原版（玩家 0x8000|位掩码 / 物件 0x8000|槽<<8）。
//   2026-09-27 由"世界坐标 ±radius 正方形"口径改回屏幕空间忠实复刻。
//   flags&2 住宅 / flags&4 商業：dump≠0 归公（owner 欠 30×M×level，owner/level/type 清零）；
//   dump=0 降级（owner 欠 30×M，level/sub--，连锁 type 归零）；
//   flags&0x20（id&0x8000）：低 4 位玩家 damagePlayer / bit4-7 事件槽 hospitalize /
//   BYTE1&0x7F 物件 deleteMapObject（重写 releaseCellTableSlot）
void expireAssets(Application& app, int cx, int cy, int radius, int flags, int dump, int payer) {
    GameState& st = app.gameState();
    (void)cx;
    (void)cy;
    const int c0 = 220 - radius;
    const int c1 = 220 + radius;
    const int r0 = 260 - radius;
    const int r1 = 260 + radius;
    std::vector<uint16_t> ids;
    for (const GameState::MapHitRegion& hit : st.mapHitRegions) {
        if (hit.id == 0) {
            continue;
        }
        if (radius != -1 &&
            !(hit.anchorX >= c0 && hit.anchorX < c1 && hit.anchorY >= r0 && hit.anchorY < r1)) {
            continue;
        }
        const int v = normalizeHitId(hit.id);
        if (v == 0) {
            continue;
        }
        // [RE 0x409DE7] 有主挂身物件（g_cellOwner != 0，如附身神明/炸弹）不写入拾取
        //   缓冲 → 不参与范围清算（原版 `!g_cellOwner[8*(slot-1)]` 才收集）
        if ((v & 0x8000) != 0 && (v & 0x7F00) != 0) {
            const int slot = (v >> 8) & 0x7F;
            if (slot >= 1 && slot <= 46 && st.cellTable[24 * (slot - 1) + 5] != 0) {
                continue;
            }
        }
        ids.push_back(static_cast<uint16_t>(v));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
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
// 依据: 0x40CD87 反编译; 详见 economy.h 注释。完整流程：
//   ① 占用位/监医标志/解除同盟/保释 NPC 关回/释放附身·挂身槽；
//   ② 清财务与状态字段（原版 sub_456F60 +28..+103）、角色遗留标记、死亡行走资源、清欠款列；
//   ③ 淘汰演出：musicPlayScene(2) + 重绘（视口临时切破产者）+ data.mkf[555] FLC + 停留 2s；
//   ④ 终局判定（原版顺序）：单人局且破产者人类 → defeatFlow（重写近似 sceneRequest=1，界面 M3）；
//      无存活人类 → sceneRequest=1；仅剩 1 个存活玩家 → 胜利 sceneRequest=2/3（结算 M3，
//      不做清算）；总存活 >1 → 资产归公收集（estate/corp）、小地图、清仓持股、没收道具/卡片、
//      乐透作废、资产 >3 件随机拍卖 3 件（卖方=系统）、棋子精灵灰度
// 注: 存活统计直接用 alive（未入场玩家 alive=0，与原版 g_playerAlive 跳伞落地才置位一致）
void eliminatePlayer(Application& app, int p) {
    GameState& st = app.gameState();
    if (p < 0 || p >= 9) {
        return;
    }
    Player& pl = st.players[p];
    if (!pl.alive) {
        return;
    }
    const uint8_t charIndex = pl.charIndex;
    const uint8_t wasAlive = pl.alive; // [RE v32] 破产前存活标志（defeatFlow 条件用）
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
    // [RE 0x40CEF0] 清财务/状态字段（原版 sub_456F60(&g_players[26*p+7], 0, 76)：
    //   字节 +28..+103；保留 name/颜色/位置/朝向/charIndex/alive/AI 配置等 +0..+27）
    pl.cash = 0;
    pl.bank = 0;
    pl.loan = 0;
    pl.bankAdvance = 0;
    pl.loanDate = 0;
    pl.points = 0;
    pl.stateFlags = 0;
    pl.byte54 = 0;
    pl.state37 = 0;
    pl.skipMove = 0;
    pl.fixedStep = 0;
    pl.byte58 = 0;
    pl.bankRefuseDays = 0;
    pl.bankFinanceFlags = 0;
    pl.allyActive = 0;
    pl.insuranceDays = 0;
    pl.cellTableIdx = 0;
    pl.cellNo = 0;
    pl.ally = 0;
    pl.byte66 = 0;
    pl.pad67 = 0;
    pl.luckA = 0;
    pl.luckB = 0;
    pl.luckC = 0;
    pl.stayCorpIdx = 0;
    std::memset(pl.pad76, 0, sizeof(pl.pad76));
    pl.monthSettleA = 0;
    pl.monthSettleB = 0;
    pl.kind = 0;
    pl.vehicleRestore = 0;
    pl.diceRestore = 1;
    pl.pad103 = 0;
    // [RE 0x40CF30] g_charState[角色] = 2：下局选人灰度不可选（重写 = newGameConfig.aiUsed）
    if (charIndex < 12) {
        st.newGameConfig.aiUsed[charIndex] = true;
    }
    loadWalkResources(st, p); // [RE 0x40CF35] alive=0 → 死亡站立资源
    // [RE 0x40CF3E] 清 p 的欠款列（原版 g_playerDebts[26*k + p] = 0，k ≠ p）
    for (int k = 0; k < 8; ++k) {
        if (k != p) {
            st.playerDebt[k][p] = 0;
        }
    }
    // [RE 0x40CFA8..0x40CFC9] 淘汰演出：切视口到破产者 → musicPlayScene(2) → 重绘 →
    //   data.mkf[555] FLC(440×440 10 帧) @(0,40) sound 100 → sub_45285E(2000) 停留 2 秒
    {
        const int oldCur = st.currentPlayer;
        st.currentPlayer = p;
        app.audio().playSceneMusic(2);
        renderGameFrame(app);
        playEventFlc(app, 555, 0, 40, 100);
        for (int t = 0; t < 2000; t += 5) {
            app.audio().update();
            SDL_Delay(5);
        }
        st.currentPlayer = oldCur;
    }
    // [RE 0x40CFB0] 单人局且破产者是人类 → defeatFlow 失败界面（返回 1=回主菜单 / 4=读档，
    //   见 victory-flow.md；界面属 M3）——重写近似为 sceneRequest=1 回主菜单
    if (st.humanCount == 1 && (wasAlive & 1) != 0) {
        st.sceneRequest = 1;
        RICH4_LOGI("eliminatePlayer: p%d single-human defeat -> scene 1 (RE 0x40CD87/defeatFlow)",
                   p);
        return;
    }
    // [RE 0x40D0A9] 淘汰台词 off_4808AE[角色] → [TODO(P4 语音专项)]
    // ---- 存活统计（原版 v13=存活人类数 / result=总存活数；alive 未入场=0 → 自动只算已入场）----
    int aliveHumans = 0;
    int aliveTotal = 0;
    for (int i = 0; i < st.playerCount && i < 9; ++i) {
        if (st.players[i].alive != 0) {
            ++aliveTotal;
            if ((st.players[i].alive & 1) != 0) {
                ++aliveHumans;
            }
        }
    }
    if (aliveHumans == 0) {
        // 无存活人类 → 回主菜单（原版 sceneRequest=1）
        st.sceneRequest = 1;
        RICH4_LOGI("eliminatePlayer: p%d no human alive -> scene 1 (RE 0x40CD87)", p);
        return;
    }
    if (aliveTotal == 1) {
        // [RE 0x40D3E0] 仅剩一个存活玩家 → 该玩家直接胜利：sceneRequest = 2（单人）/3（多人）；
        //   不做资产清算/拍卖（原版 result==1 分支）；胜利演出（胜利台词 sub_44EF41 + 通关结算
        //   gameClearFlow）属 M3
        st.sceneRequest = (st.humanCount == 1) ? 2 : 3;
        RICH4_LOGI("eliminatePlayer: p%d eliminated -> last player wins, scene %d (RE 0x40CD87)",
                   p, st.sceneRequest);
        return;
    }
    // ---- [RE 0x40D0E5..0x40D3F6] 资产归公 + 拍卖（仅当总存活 > 1）----
    std::vector<uint16_t> auctionIds;
    for (size_t i = 1; i < st.estates.size(); ++i) {
        Estate& e = st.estates[i];
        if (e.owner == p + 1) {
            e.owner = 0;
            e.expireDate = 0;
            auctionIds.push_back(static_cast<uint16_t>(2000 + i));
        }
    }
    for (size_t i = 1; i < st.corps.size(); ++i) {
        Corp& c = st.corps[i];
        if (c.owner == p + 1) {
            c.owner = 0;
            c.expireDate = 0;
            auctionIds.push_back(static_cast<uint16_t>(4000 + i));
        }
    }
    for (auto& s : st.specPts) {
        if (s.owner == p + 1) {
            s.owner = 0;
        }
    }
    buildMiniMapMarks(app); // [RE 0x40D1D4] rebuildMiniMap(0)
    // [RE 0x40D1E0] 卖出全部持股（toBank=false → 入系统/公库）
    for (int m = 0; m < 12; ++m) {
        const int32_t hold = st.playerShares[p][m];
        if (hold != 0) {
            sellStock(app, p, m, hold, false);
        }
    }
    confiscateItems(app, p); // [RE 0x40D2A4]
    confiscateCards(app, p); // [RE 0x40D2A9]
    // [RE 0x40D1AA] 破产者所购乐透号码作废（不退款）
    for (int n = 0; n < 36; ++n) {
        if (st.lotteryNumbers[n] == p + 1) {
            st.lotteryNumbers[n] = 0;
        }
    }
    // [RE 0x40D2D5] 资产 > 3 件 → musicPlayScene(5) + 随机抽 3 件拍卖（卖方 = -1 系统）
    if (auctionIds.size() > 3) {
        app.audio().playSceneMusic(5);
        for (int t = 0; t < 3; ++t) {
            size_t pick = 0;
            do {
                pick = static_cast<size_t>(std::rand()) % auctionIds.size();
            } while (auctionIds[pick] == 0);
            runAuction(app, -1, auctionIds[pick], false);
            auctionIds[pick] = 0;
        }
    }
    // [RE 0x40D2C4] 棋子精灵 frame0 灰度（淘汰者棋子变灰；0 像素保持）
    if (st.pieceAnim[p].frameCount() > 0) {
        UiImage& img = st.pieceAnim[p];
        std::vector<uint8_t> copy = img.data();
        const UiFrameView& f = img.frame(0);
        const size_t off = static_cast<size_t>(
            reinterpret_cast<const uint8_t*>(f.pixels) - copy.data());
        grayscaleImage(reinterpret_cast<uint16_t*>(copy.data() + off),
                       static_cast<size_t>(f.width) * f.height);
        img.load(std::move(copy));
    }
    renderGameFrame(app); // [RE 0x40D3F0 sub_41906A(1)]
    RICH4_LOGI("eliminatePlayer: p%d aliveHumans=%d aliveTotal=%d auction=%zu (RE 0x40CD87)", p,
               aliveHumans, aliveTotal, auctionIds.size());
}

// [RE 0x411AE0] surrenderPlayer：認輸投降 = 淘汰自己 +（多人类时）召唤死神送他人复仇
// 依据: 0x411AE0 反编译; ① sub_40CD87 淘汰当前玩家；② g_humanCount > 1 且存活人类数
//   （sub_40DFDA）非 0 时弹死神对话框（0x4339D9）；③ 选中目标 → 临时切 currentPlayer 到目标 →
//   重绘（sub_41906A(1)）→ createMapObject(15, 0, 7, target)（死神对象创建即附身 target-1）→
//   还原 currentPlayer
// 差异: 单人投降的战败界面 defeatFlow 属 M3；回合推进由调用方 nextPlayer 承担（原版无显式调用）
void surrenderPlayer(Application& app) {
    GameState& st = app.gameState();
    const int cur = st.currentPlayer;
    if (cur < 0 || cur >= 4) {
        return;
    }
    eliminatePlayer(app, cur); // [RE 0x411AE9 sub_40CD87]
    st.playerActionState[cur] = 0; // [迁移] 清回合状态机残留（重写原投降分支行为）
    if (st.sceneRequest != 0) {
        // 终局（单人 defeatFlow 近似 / 无存活人类 / 唯一存活者胜利）→ 不再召唤死神
        //   （原版靠候选不足兜底返回 0，等价）
        RICH4_LOGI("surrender: p%d eliminated -> scene %d, no death god (RE 0x411AE0)", cur,
                   st.sceneRequest);
        return;
    }
    // [RE 0x40DFDA] 存活人类数（未入场玩家 alive=0，与原版 g_playerAlive 一致；
    //   spriteX/Y 双保险：跳伞未落地者即便被异常置 alive 也不计）
    int aliveHumans = 0;
    for (int i = 0; i < st.playerCount && i < 9; ++i) {
        const Player& pl = st.players[i];
        if ((pl.alive & 1) != 0 && (pl.spriteX != 0 || pl.spriteY != 0)) {
            ++aliveHumans;
        }
    }
    if (st.humanCount <= 1 || aliveHumans == 0) { // [RE 0x411AF8/0x411B01]
        return;
    }
    const int target = deathGodSummonDialog(app, cur); // [RE 0x411B0A → 0x4339D9]
    if (target == 0) {
        return;
    }
    const int old = st.currentPlayer;
    // [防御] 渲染会经 0x40829D 对未落地玩家触发出生点分配+跳伞（pendingSpawnPlayer）；
    //   候选已排除未入场者，仍暂存/恢复以防副作用（原版 g_playerAlive 天然排除）
    const int savedPending = st.pendingSpawnPlayer;
    st.currentPlayer = target - 1; // [RE 0x411B1F]
    renderGameFrame(app);          // [RE 0x411B26 sub_41906A(1)]
    createMapObject(app, 15, 0, 7, static_cast<uint8_t>(target)); // [RE 0x411B35]
    st.pendingSpawnPlayer = savedPending;
    st.currentPlayer = old; // [RE 0x411B3D]
    RICH4_LOGI("surrender: p%d eliminated, death god attached to p%d (RE 0x411AE0)", cur, target - 1);
}

} // namespace rich4
