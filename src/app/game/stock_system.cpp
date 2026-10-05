#include "game/app/stock_system.h"
#include "game/core/bit_cast.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/app/date_util.h"
#include "game/app/event_common.h"
#include "game/app/game_panel.h"
#include "game/app/map_render.h"
#include "game/app/map_tables.h"
#include "game/app/message_dialog.h"
#include "game/application.h"
#include "game/core/log.h"
#include "game/core/trace.h"
#include "game/core/debug_hooks.h"

namespace rich4 {

namespace {

// [RE 0x428EC5] stockPriceUpdate：价格 = 昨收 × (1 + 动量/100)，按价格档位取精度，clamp [1, 9999]
// 依据: 0x428EC5 反编译; 档位阈值 5/15/50/150 → 精度 0.01/0.05/0.1/0.5/1.0
//   （涨则减档位、跌则加档位，形成"四舍五入到档位"）
float stockPriceUpdate(float prev, float momentum) {
    const double raw = (static_cast<double>(momentum) + 100.0) / 100.0 * static_cast<double>(prev);
    double v = 0.0;
    if (momentum > 0.0f) {
        double tick = 0.01; // flt_463FB4
        if (raw >= 150.0) {
            tick = 1.0;
        } else if (raw >= 50.0) {
            tick = 0.5; // dbl_463F9C
        } else if (raw >= 15.0) {
            tick = 0.1; // dbl_463FA4
        } else if (raw >= 5.0) {
            tick = 0.05; // dbl_463FAC
        }
        v = raw - tick;
    } else {
        double tick = 1.0;
        if (raw < 5.0) {
            tick = 0.01;
        } else if (raw < 15.0) {
            tick = 0.05;
        } else if (raw < 50.0) {
            tick = 0.1;
        } else if (raw < 150.0) {
            tick = 0.5;
        }
        v = raw + tick;
    }
    if (v < 1.0) {
        v = 1.0;
    }
    if (v > 9999.0) {
        v = 9999.0;
    }
    return static_cast<float>(v);
}

} // namespace

// [RE 0x428CAF] refreshStockSpecPtMap
// 依据: 0x428CAF 反编译; 遍历 12 支股票，对"上市公司"（kStockInit[s][1] != 0）按
//       specPt.stockNo == 股票号 反查 specPt 索引并回写 stocks[s][1]
// 迁移: 上市判断改用 kStockInit 表（不依赖运行时初值，幂等）；
//       股票表未初始化（如读档路径未经过 newGameInit）时用 kStockInit 重建
void refreshStockSpecPtMap(Application& app) {
    GameState& st = app.gameState();
    const auto& init = kStockInit[st.gameMode * 4 + st.mapIndex];
    // 确保股票表已初始化（基准价 [3] 全 0 视为未初始化）
    bool empty = true;
    for (int s = 0; s < 12; ++s) {
        if (st.stocks[s][3] != 0.0f) {
            empty = false;
            break;
        }
    }
    if (empty) {
        std::memcpy(st.stocks, init, sizeof(st.stocks));
        RICH4_LOGI("stock table rebuilt from kStockInit (mode=%d map=%d)", st.gameMode,
                   st.mapIndex);
    }
    for (int s = 0; s < 12; ++s) {
        if (init[s][1] == 0) {
            st.stocks[s][1] = 0.0f; // 非上市公司：无 specPt 映射
            continue;
        }
        st.stocks[s][1] = 0.0f;
        for (size_t i = 1; i < st.specPts.size(); ++i) {
            if (st.specPts[i].stockNo == s) {
                st.stocks[s][1] = static_cast<float>(i);
            }
        }
    }
    RICH4_LOGI("stock map: s0->%d s1->%d s2->%d s3->%d (specPts=%zu, RE 0x428CAF)",
               static_cast<int>(st.stocks[0][1]), static_cast<int>(st.stocks[1][1]),
               static_cast<int>(st.stocks[2][1]), static_cast<int>(st.stocks[3][1]),
               st.specPts.size());
}

// [RE 0x4291D6] stockTick
// 依据: 0x4291D6 反编译; ① 停市判定 sub_428D01（dword_4990DC || isSpecialDate）→ 跳过；
//   ② 全局扰动 dword_4990EC = (rand()-0x4000)/4097；
//   ③ 每支：昨收 = 当前价 → 停牌（动量 = 0）/新闻（bit4 ? +10 : -10）/正常（个股扰动
//      (rand()-0x4000)/1171 → 动量 += 扰动×波动率 + 全局扰动；价格远离基准（上市=面值/10000、
//      非上市=基准价）时动量 ×0.5 或 ×2）→ clamp ±10 → 当前价 = sub_428EC5(昨收,动量) →
//      stockHistory[144*i + turnCounter]；
//   ④ turnCounter = (+1) % 144；大盘 = Σ价格 × 10
// 差异: 无（休市事件源 = 新闻 idx26 newsEvt26_stockHaltAll 0x44B0A0 → st.stockMarketClosed=10，
//   news_dialog.cpp；advanceDay 递减/bit7 见 turn_system.cpp）
void stockTick(Application& app) {
    GameState& st = app.gameState();
    // [RE 0x428D01] 休市：stockMarketClosed（事件设的剩余天数）或周末/节日 → 跳过行情
    if (st.stockMarketClosed != 0 || stockIsHoliday(st)) {
        return;
    }
    st.stockGlobalDrift = static_cast<float>((dbg::raw(dbg::SlotStock) - 0x4000) / 4097.0);
    double sum = 0.0;
    for (int i = 0; i < 12; ++i) {
        float* s = st.stocks[i];
        s[4] = s[5]; // 昨收 = 当前价
        if (st.stockHalted[i]) {
            s[7] = 0.0f; // 停牌：动量归零
        } else if (st.stockNews[i]) {
            s[7] = ((st.stockNews[i] & 0xF0) != 0) ? 10.0f : -10.0f;
        } else {
            const float noise = static_cast<float>((dbg::raw(dbg::SlotStock) - 0x4000) / 1171.0);
            s[8] = noise;
            s[7] += noise * s[6];        // 动量 += 个股扰动 × 波动率
            s[7] += st.stockGlobalDrift; // 动量 += 全局扰动
            // 边界修正：价格远离基准时缩放动量（上市公司基准 = specPt.capital(+36)/10000）
            if (static_cast<int>(s[1]) != 0) {
                const int si = static_cast<int>(s[1]);
                const float base = (si > 0 && si < static_cast<int>(st.specPts.size()))
                                       ? st.specPts[si].capital / 10000.0f
                                       : 0.0f;
                if (base > 0.0f) {
                    if (s[4] <= base) {
                        if (s[4] < base * 0.85f) {
                            s[7] *= (s[7] <= 0.0f) ? 0.5f : 2.0f;
                        }
                    } else if (s[4] > base * 3.0f) {
                        s[7] *= (s[7] > 0.0f) ? 0.5f : 2.0f;
                    }
                }
            } else {
                const float base = s[3];
                if (s[4] <= base) {
                    if (s[4] < base * 0.5f) {
                        s[7] *= (s[7] <= 0.0f) ? 0.5f : 2.0f;
                    }
                } else if (s[4] > base * 8.0f) {
                    s[7] *= (s[7] > 0.0f) ? 0.5f : 2.0f;
                }
            }
            s[7] = std::clamp(s[7], -10.0f, 10.0f);
        }
        s[5] = stockPriceUpdate(s[4], s[7]);
        trace::logf("stock idx=%d prev=%.1f new=%.1f delta=%.1f", i, s[4], s[5], s[7]);
        st.stockHistory[i][st.turnCounter] =
            floatBits(s[5]); // 位模式存 float（memcpy 避免 strict aliasing）
        sum += s[5];
    }
    st.turnCounter = (st.turnCounter + 1) % 144;
    st.marketIndex = static_cast<int32_t>(sum * 10.0);
    RICH4_LOGI("stockTick: market=%d turn=%d (RE 0x4291D6)", st.marketIndex, st.turnCounter);
}

// [RE 0x429040] stockNewsApply
// 依据: 0x429040 反编译; 新闻设置 byte_496987[36*股] 后立即调用（全部 12 支或单支）：
//   高 4 位 != 0 → 动量 flt_49699C = +10.0 否则 -10.0 → 现价 = stockPriceUpdate(昨收, 动量)
//   → g_stockHistory[144*股 + (turnCounter-1 环形)] 记录；byte_496987 == 0 的股票跳过。
//   与 stockTick 的差异：本函数不做"昨收 = 现价"，直接以既有昨收计算（新闻当日冲击）。
void stockNewsApply(Application& app, int stock) {
    GameState& st = app.gameState();
    const int slot = (st.turnCounter - 1 + 144) % 144;
    const auto applyOne = [&st, slot](int i) {
        if (st.stockNews[i] == 0) {
            return;
        }
        st.stocks[i][7] = ((st.stockNews[i] & 0xF0) != 0) ? 10.0f : -10.0f;
        st.stocks[i][5] = stockPriceUpdate(st.stocks[i][4], st.stocks[i][7]);
        st.stockHistory[i][slot] = floatBits(st.stocks[i][5]);
    };
    if (stock != 0) {
        const int i = stock - 1;
        if (i >= 0 && i < 12) {
            applyOne(i);
        }
    } else {
        for (int i = 0; i < 12; ++i) {
            applyOne(i);
        }
    }
    RICH4_LOGI("stockNewsApply: stock=%d slot=%d (RE 0x429040)", stock, slot);
}

// [RE 0x4294D5] updateSpecPtControl
// 依据: 0x4294D5 反编译; ① 从 shareholders[4] 移除该玩家（memmove 压缩）；
//   ② 若持股 > 0：从索引 2 倒序比较（空位跳过、持股 >= 我则停、否则后移）→ 插入；
//   ③ owner = shareholders[0]（第一大股东）；owner 变化 → rebuildMiniMap 并返回 1
int updateSpecPtControl(Application& app, int player, int stock) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || stock < 0 || stock >= 12) {
        return 0;
    }
    const int si = static_cast<int>(st.stocks[stock][1]);
    if (si <= 0 || si >= static_cast<int>(st.specPts.size())) {
        return 0;
    }
    SpecPt& sp = st.specPts[si];
    // ① 移除
    for (int k = 0; k < 4; ++k) {
        if (sp.shareholders[k] == player + 1) {
            for (int m = k; m < 3; ++m) {
                sp.shareholders[m] = sp.shareholders[m + 1];
            }
            sp.shareholders[3] = 0;
            break;
        }
    }
    // ② 按持股降序插入（平手归先拥有者）
    const int32_t mine = st.playerShares[player][stock];
    if (mine > 0) {
        int pos = 0;
        for (int k = 2; k >= 0; --k) {
            const uint8_t other = sp.shareholders[k];
            if (other == 0) {
                continue; // 空位：继续前移
            }
            if (st.playerShares[other - 1][stock] >= mine) {
                pos = k + 1;
                break;
            }
            sp.shareholders[k + 1] = other; // 后移一位
            pos = k;
        }
        sp.shareholders[pos] = static_cast<uint8_t>(player + 1);
    }
    // ③ owner = 第一大股东
    const uint8_t top = sp.shareholders[0];
    if (sp.owner != top) {
        sp.owner = top;
        buildMiniMapMarks(app);
        RICH4_LOGI("specPt control: stock %d owner -> %u (player %d, RE 0x4294D5)", stock, top,
                   player);
        return 1;
    }
    return 0;
}

// [RE 0x428D2A] buyStock
// 依据: 0x428D2A 反编译; viaBank=false（现金）：价格 = specPt.feeBase/10000 × count，
//   sharesLeft -= count，现金扣款；viaBank=true：金额 = 当前价 × count，银行扣款，
//   交易量/保留股份(word_49698A/88) -= count；
//   末尾：持股 += count；均价 = (旧持股×旧均价 + 金额) / 新持股；再调 updateSpecPtControl
//   返回 1 表示经营権（owner）变化
int buyStock(Application& app, int player, int stock, int count, bool viaBank) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || stock < 0 || stock >= 12 || count <= 0) {
        return 0;
    }
    int32_t amount = 0;
    if (viaBank) {
        amount = static_cast<int32_t>(static_cast<double>(count) * st.stocks[stock][5]); // 当前价
        st.players[player].bank -= amount;
        st.stockVolume[stock] -= count;   // [RE 0x428D2A] word_49698A 交易量回归
        st.stockReserved[stock] -= count; // [RE 0x428D2A] word_496988 保留股份
    } else {
        const int si = static_cast<int>(st.stocks[stock][1]);
        if (si > 0 && si < static_cast<int>(st.specPts.size())) {
            const int32_t per = st.specPts[si].capital / 10000; // 股本 / 10000 = 每股价格
            amount = per * count;
            st.specPts[si].sharesLeft -= count;
            RICH4_LOGI("buyStock: si=%d capital=%d per=%d sharesLeft=%d", si,
                       st.specPts[si].capital, per, st.specPts[si].sharesLeft);
        } else {
            RICH4_LOGI("buyStock: si=%d invalid (stocks[%d][1]=%.1f, specPts=%zu)", si, stock,
                       st.stocks[stock][1], st.specPts.size());
        }
        st.players[player].cash -= amount;
    }
    const int32_t oldShares = st.playerShares[player][stock];
    const float oldCost = static_cast<float>(oldShares) * st.playerAvgCost[player][stock];
    st.playerShares[player][stock] = oldShares + count;
    st.playerAvgCost[player][stock] =
        static_cast<float>((static_cast<double>(amount) + oldCost) / (oldShares + count));
    RICH4_LOGI("buyStock: player %d stock %d count %d amount %d bank=%d (RE 0x428D2A)", player,
               stock, count, amount, viaBank ? 1 : 0);
    return updateSpecPtControl(app, player, stock);
}

// [RE 0x428E23] sellStock
// 依据: 0x428E23 反编译; 持股 -= count（归零则均价清 0）；收入 = 现价(flt_496994) × count；
//   word_49698A/88 += count（回归市场）；toBank 入 g_playerBank 否则入 dword_499080 公库；
//   末尾 updateSpecPtControl；返回 1 表示经营権变化
int sellStock(Application& app, int player, int stock, int count, bool toBank) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || stock < 0 || stock >= 12 || count <= 0) {
        return 0;
    }
    int32_t& shares = st.playerShares[player][stock];
    shares -= count;
    if (shares < 0) {
        shares = 0;
    }
    if (shares == 0) {
        st.playerAvgCost[player][stock] = 0.0f;
    }
    const int32_t amount =
        static_cast<int32_t>(static_cast<double>(count) * st.stocks[stock][5]); // 现价
    st.stockVolume[stock] += count;   // [RE 0x428E23] word_49698A
    st.stockReserved[stock] += count; // [RE 0x428E23] word_496988
    if (toBank) {
        st.players[player].bank += amount;
    } else {
        st.publicFund += amount;
    }
    RICH4_LOGI("sellStock: player %d stock %d count %d amount %d toBank=%d (RE 0x428E23)", player,
               stock, count, amount, toBank ? 1 : 0);
    return updateSpecPtControl(app, player, stock);
}

// [RE 0x42915A] refreshBankStockShares
// 依据: 0x42915A 反编译; 每支：保留股份 word_496988 <= 1000 → 交易量 = 保留；
//   否则 交易量 = 保留 × (rand()%2000 + 1000) / 10000（即 10%~30%）
// 调用点: loadMapData 初始化 + advanceDay 每日
void refreshBankStockShares(Application& app) {
    GameState& st = app.gameState();
    for (int i = 0; i < 12; ++i) {
        const int32_t res = st.stockReserved[i];
        if (res <= 1000) {
            st.stockVolume[i] = res;
        } else {
            st.stockVolume[i] =
                static_cast<int32_t>(static_cast<double>(res) * ((dbg::roll(dbg::SlotStock, 2000) + 1000) / 10000.0));
        }
    }
}

// ===== AI 买/卖股票（beginPlayerTurn 0x418DE6/0x418DF4，418c55-ai-turn-actions.md §2/§3）=====
namespace {

// 历史环形窗均价（[RE 0x42C3A2] g_stockHistory[144*i+slot]，0x7FFFFFFF 掩码=有效位）
float histAvg(const GameState& st, int s, int days) {
    int idx = st.turnCounter - days;
    if (idx < 0) {
        idx += 144;
    }
    float sum = 0.0f;
    int n = 0;
    for (int k = 0; k < days; ++k) {
        if (idx == 144) {
            idx = 0;
        }
        const int32_t raw = st.stockHistory[s][idx];
        if ((raw & 0x7FFFFFFF) != 0) {
            float v = 0.0f;
            std::memcpy(&v, &raw, 4);
            sum += v;
            ++n;
        }
        ++idx;
    }
    return n != 0 ? sum / static_cast<float>(n) : 0.0f;
}

float histMin(const GameState& st, int s) {  // [RE 0x42CBEA] 144 期最低
    float minv = 10000.0f;
    for (int k = 0; k < 144; ++k) {
        const int32_t raw = st.stockHistory[s][k];
        if ((raw & 0x7FFFFFFF) != 0) {
            float v = 0.0f;
            std::memcpy(&v, &raw, 4);
            if (v < minv) {
                minv = v;
            }
        }
    }
    return minv;
}

int listedSpecPt(const GameState& st, int s) {  // [1]=specPt 索引（0=未上市）
    return static_cast<int>(st.stocks[s][1]);
}

// [RE 0x4295EA] 0涨 1涨停 2跌 3跌停 4平（同 stock_market_dialog.cpp stockStatus，
//   该定义在匿名 ns 不导出，AI 侧本地复刻避免符号冲突）
int stockUpDown(const GameState& st, int s) {
    const float prev = st.stocks[s][4];
    const float cur = st.stocks[s][5];
    if (cur > prev) {
        return cur >= stockPriceUpdate(prev, 10.0f) ? 1 : 0;
    }
    if (cur >= prev) {
        return 4;
    }
    return cur <= stockPriceUpdate(prev, -10.0f) ? 3 : 2;
}

}  // namespace

// [RE 0x42BF03] AI 买股票。评分常量：1.2/0.85/0.7（发行价折扣档）、2.5/0.6/0.5（参考价/均线比）。
void aiStockBuy(Application& app, int player) {
    GameState& st = app.gameState();
    Player& pl = st.players[player];
    // 门槛：1/3 概率、aiStockPct、休市、贷款剩余 <15 天（0x42BF1F..0x42BF68）
    if (dbg::roll(dbg::SlotAi, 3) != 0 || pl.aiStockPct == 0 ||
        st.stockMarketClosed != 0 || stockIsHoliday(st) ||
        (pl.loanDate != 0 && dateDiff(st.gameDate, pl.loanDate) < 15)) {
        return;
    }
    int64_t mval = 0;  // 持仓市值
    for (int i = 0; i < 12; ++i) {
        if (st.playerShares[player][i] != 0) {
            mval += static_cast<int64_t>(st.playerShares[player][i]) *
                    static_cast<int64_t>(st.stocks[i][5]);
        }
    }
    int64_t gap = static_cast<int64_t>(pl.aiStockPct) * (pl.cash + pl.bank + mval) / 100 - mval;
    if (gap <= 0) {
        return;
    }
    if (gap > pl.bank) {
        gap = pl.bank;  // 只用存款补差（0x42C05E）
    }
    int score[12] = {};
    for (int i = 0; i < 12; ++i) {
        // 剔除：停牌 / 涨停 / 无在售量（0x42C5BD）
        if (st.stockHalted[i] != 0 || stockUpDown(st, i) == 1 || st.stockVolume[i] == 0) {
            continue;
        }
        const float price = st.stocks[i][5];
        const float ref = st.stocks[i][3];   // 参考价（涨跌界限基准）
        const int sp = listedSpecPt(st, i);
        if (sp == 0) {
            // 未上市组（0x42C379）：bank ≤ 30000×M 不玩；技术面 = 6/24 日均线形态
            if (30000 * st.moneyMul >= pl.bank) {
                continue;
            }
            const float a24 = histAvg(st, i, 24);
            const float a6 = histAvg(st, i, 6);
            if (price < ref * 2.5f && st.stocks[i][7] > 2.0f && a6 > a24) {
                score[i] += 2;
            }
            if (price < ref * 0.6f && a6 > a24) {
                score[i] += 4;
            }
            if (a6 < a24 * 0.5f) {
                score[i] += 2;
            }
        } else if (20000 * st.moneyMul < pl.bank &&
                   sp < static_cast<int>(st.specPts.size())) {
            // 上市组（0x42C5F8）：公库**每期支出**比 = fundPaid(+44)/stockDivPeriod(+84 dword_499084)
            //   + 发行价（capital/10000）折价 + 抢控制权
            const SpecPt& pt = st.specPts[sp];
            const double fundR = st.stockDivPeriod != 0
                                     ? static_cast<double>(pt.fundPaid) / st.stockDivPeriod
                                     : static_cast<double>(pt.fundPaid);
            const double issue = pt.capital / 10000.0;
            if (fundR > 0.0) {
                if (5000 * st.moneyMul > fundR) {
                    ++score[i];
                } else if (10000 * st.moneyMul > fundR) {
                    score[i] += 2;
                } else {
                    score[i] += 3;
                }
            }
            if (pt.fund > 0 && st.playerShares[player][i] < 5000 &&
                issue * 1.2 >= static_cast<double>(price)) {
                ++score[i];
                if (pt.owner != 0 && pt.owner != static_cast<uint8_t>(player + 1)) {
                    const int owner = pt.owner - 1;
                    if (st.playerShares[player][i] + st.stockReserved[i] + pt.sharesLeft >
                        st.playerShares[owner][i]) {
                        ++score[i];  // 可超经营者持股
                    }
                    if (st.stockVolume[i] + st.playerShares[player][i] > st.playerShares[owner][i]) {
                        score[i] += 2;
                    }
                }
            }
            if (issue * 0.85 > price) {
                score[i] += 3;
            }
            if (issue * 0.7 > price) {
                score[i] += 5;
            }
        }
    }
    // 评分降序（同分保持股票序 [RE 0x42C64E qsort 仅比 low16]）
    int order[12];
    for (int i = 0; i < 12; ++i) {
        order[i] = i;
    }
    std::stable_sort(order, order + 12,
                     [&score](int a, int b) { return score[a] > score[b]; });
    int pick = -1;
    for (int k = 0; k < 12; ++k) {
        if (score[order[k]] != 0 && dbg::roll(dbg::SlotAi, 24) <= 12 - k) {
            pick = order[k];  // 越靠前越易买（0x42C6AF）
            break;
        }
    }
    if (pick < 0) {
        return;
    }
    const float price = st.stocks[pick][5];
    if (price <= 0.0f) {
        return;
    }
    int64_t n = gap / static_cast<int64_t>(price);
    if (n == 0) {
        return;
    }
    if (n > st.stockVolume[pick]) {
        n = st.stockVolume[pick];
    }
    buyStock(app, player, pick, static_cast<int>(n), true);
    refreshPlayerPanelFor(app, player);
    char text[96];
    std::snprintf(text, sizeof text, "%s\n\n買進%s%d張",
                  pl.name != nullptr ? pl.name : "", // [RE 0x42C769 sprintf 玩家名直引不去空格]
                  nameNoSpaces(kStockNames[st.gameMode * 4 + st.mapIndex][pick]).c_str(), // [RE 0x42C755 copyNameNoSpaces=公司名]
                  static_cast<int>(n));
    showMessage(app, text, 1500);
    RICH4_LOGI("aiStockBuy: p%d stock=%d n=%d (RE 0x42BF03)", player, pick, static_cast<int>(n));
}

// [RE 0x42C79F] AI 卖股票。强制=贷款到期 ≤6 天且现金+存款 < 贷款（评分×2/+1 且循环卖至
//   资产 ≥ loan×1.1）；否则 1/3 概率。剔除：无持仓 / 停牌 / 跌停。
void aiStockSell(Application& app, int player) {
    GameState& st = app.gameState();
    Player& pl = st.players[player];
    const bool forced = dateDiff(st.gameDate, pl.loanDate) <= 6 && pl.loan != 0 &&
                        pl.cash + pl.bank < pl.loan;
    if (!forced && dbg::roll(dbg::SlotAi, 3) != 0) {
        return;
    }
    if (st.stockMarketClosed != 0 || stockIsHoliday(st)) {
        return;
    }
    const int day = static_cast<int>(st.gameDate & 0xFF);  // (u8)dword_497160 = 日
    int best = -1;
    do {
        best = -1;
        int bestScore = 0;
        for (int i = 0; i < 12; ++i) {
            int sc = 0;
            if (st.playerShares[player][i] == 0 || st.stockHalted[i] != 0 ||
                stockUpDown(st, i) == 3) {
                // 不计（0x42CF7E score=0）
            } else {
                const float price = st.stocks[i][5];
                const float prev = st.stocks[i][4];
                const float cost = st.playerAvgCost[player][i];
                const int sp = listedSpecPt(st, i);
                if (sp != 0 && sp < static_cast<int>(st.specPts.size())) {
                    const SpecPt& pt = st.specPts[sp];
                    // [RE 0x42CFE8] 每期公库支出 = fundPaid(+44)/stockDivPeriod(dword_499084)
                    const double fundR =
                        st.stockDivPeriod != 0
                            ? static_cast<double>(pt.fundPaid) / st.stockDivPeriod
                            : static_cast<double>(pt.fundPaid);
                    const double issue = pt.capital / 10000.0;
                    int64_t allShares = 0;
                    for (int j = 0; j < st.playerCount && j < 4; ++j) {
                        allShares += st.playerShares[j][i];
                    }
                    const double conc = st.playerShares[player][i] != 0
                                            ? static_cast<double>(allShares) /
                                                  st.playerShares[player][i]
                                            : 0.0;
                    if (pt.fund <= -10000 * st.moneyMul && conc > 0.6 && day > 10 &&
                        day < 15) {
                        sc += 3;  // 公库巨亏(+40)+高集中：除权前抛（0x42C919）
                    }
                    if (pt.fund <= -6000 * st.moneyMul &&
                        static_cast<double>(price) > cost * 1.2 && pt.owner != player + 1 &&
                        day > 8 && day < 15) {
                        sc += 2;
                    }
                    if (fundR <= 10000 * st.moneyMul && price >= static_cast<float>(issue) * 2.0f &&
                        static_cast<double>(price) > cost * 1.3 && conc < 0.4) {
                        ++sc;
                    }
                    if (pt.owner != 0 && pt.owner != static_cast<uint8_t>(player + 1)) {
                        const int owner = pt.owner - 1;
                        if (st.playerShares[player][i] + st.stockReserved[i] + pt.sharesLeft <
                                st.playerShares[owner][i] &&
                            pt.fund <= 0 &&
                            static_cast<double>(price) >=
                                static_cast<double>(cost) * 1.5) {
                            ++sc;  // 追不上经营者且价高 → 出（0x42CAC8）
                        }
                    }
                    if (price > static_cast<float>(issue) * 2.0f && price > cost * 2.0f &&
                        price < prev && pt.owner != static_cast<uint8_t>(player + 1)) {
                        sc += 2;
                    }
                    if (price > static_cast<float>(issue) * 3.0f && price > cost * 3.0f &&
                        price < prev && pt.owner == static_cast<uint8_t>(player + 1)) {
                        sc += 2;  // 自持公司超发行价回落 → 减磅（0x42CB9D）
                    }
                    if (forced) {
                        ++sc;
                    }
                } else {
                    const double ratio = cost > 0.0f ? static_cast<double>(price) / cost : 0.0;
                    const float lo144 = histMin(st, i);
                    const float a24 = histAvg(st, i, 24);
                    const float a6 = histAvg(st, i, 6);
                    if (ratio > 1.6 && st.stocks[i][7] < 1.0f) {
                        sc += 2;  // 涨幅高但动能弱（0x42CD7D）
                    }
                    const float floor8 = lo144 * 8.0f;
                    if (price > floor8 && floor8 > static_cast<double>(cost) * 1.25) {
                        sc += 2;  // 近 8 倍谷底且高于成本 1.25 → 获利了结
                    }
                    if (a24 > a6 && price < prev) {
                        sc += 2;  // 短均下穿
                    }
                    if (ratio >= 2.0) {
                        sc += static_cast<int>((ratio + 1.5) / 0.5 + 1.0);  // 超额收益加速卖
                    }
                    if (pl.bank + pl.cash < 30000 * st.moneyMul && ratio > 0.0) {
                        sc += static_cast<int>(ratio / 0.5 + 1.0);  // 缺现金
                    }
                    if (pl.bank + pl.cash < 16000 * st.moneyMul && ratio > 0.0) {
                        sc += static_cast<int>(ratio / 0.5 + 1.0);
                    }
                    if (forced) {
                        sc *= 2;
                    }
                }
            }
            if (sc > bestScore) {
                bestScore = sc;
                best = i;
            }
        }
        if (best >= 0) {
            const int cnt = st.playerShares[player][best];
            sellStock(app, player, best, cnt, true);
            refreshPlayerPanelFor(app, player);
            char text[96];
            std::snprintf(text, sizeof text, "%s\n\n賣出%s%d張",
                          pl.name != nullptr ? pl.name : "", // [RE 0x42D06F sprintf 玩家名直引不去空格]
                          nameNoSpaces(kStockNames[st.gameMode * 4 + st.mapIndex][best]).c_str(), // [RE 0x42D05B copyNameNoSpaces=公司名]
                          cnt);
            showMessage(app, text, 1500);
            RICH4_LOGI("aiStockSell: p%d stock=%d n=%d forced=%d (RE 0x42C79F)", player, best,
                       cnt, forced ? 1 : 0);
        }
        if (!forced || best < 0) {
            break;
        }
    } while (pl.loan * 1.1 > pl.cash + pl.bank);  // [RE dbl_464234] 强制模式卖到够还贷
}

} // namespace rich4
