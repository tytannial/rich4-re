#include "game/app/stock_system.h"

#include <algorithm>
#include <cstdlib>

#include "game/app/map_render.h"
#include "game/application.h"
#include "game/core/log.h"

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
// 依据: 0x428CAF 反编译; 遍历 12 支股票，对已有映射者（stocks[s][1] != 0）按
//       specPt.index == 股票号 反查 specPt 索引并回写
void refreshStockSpecPtMap(Application& app) {
    GameState& st = app.gameState();
    for (int s = 0; s < 12; ++s) {
        if (static_cast<int>(st.stocks[s][1]) == 0) {
            continue; // 非上市公司
        }
        for (size_t i = 1; i < st.specPts.size(); ++i) {
            if (st.specPts[i].index == s) {
                st.stocks[s][1] = static_cast<float>(i);
            }
        }
    }
}

// [RE 0x4291D6] stockTick
// 依据: 0x4291D6 反编译; ① 停市判定 sub_428D01（dword_4990DC || isSpecialDate）→ 跳过；
//   ② 全局扰动 dword_4990EC = (rand()-0x4000)/4097；
//   ③ 每支：昨收 = 当前价 → 停牌（动量 = 0）/新闻（bit4 ? +10 : -10）/正常（个股扰动
//      (rand()-0x4000)/1171 → 动量 += 扰动×波动率 + 全局扰动；价格远离基准（上市=面值/10000、
//      非上市=基准价）时动量 ×0.5 或 ×2）→ clamp ±10 → 当前价 = sub_428EC5(昨收,动量) →
//      stockHistory[144*i + turnCounter]；
//   ④ turnCounter = (+1) % 144；大盘 = Σ价格 × 10
// 差异: isSpecialDate 停市未接入（暂不停市）
void stockTick(Application& app) {
    GameState& st = app.gameState();
    st.stockGlobalDrift = static_cast<float>((std::rand() - 0x4000) / 4097.0);
    double sum = 0.0;
    for (int i = 0; i < 12; ++i) {
        float* s = st.stocks[i];
        s[4] = s[5]; // 昨收 = 当前价
        if (st.stockHalted[i]) {
            s[7] = 0.0f; // 停牌：动量归零
        } else if (st.stockNews[i]) {
            s[7] = ((st.stockNews[i] & 0xF0) != 0) ? 10.0f : -10.0f;
        } else {
            const float noise = static_cast<float>((std::rand() - 0x4000) / 1171.0);
            s[8] = noise;
            s[7] += noise * s[6];        // 动量 += 个股扰动 × 波动率
            s[7] += st.stockGlobalDrift; // 动量 += 全局扰动
            // 边界修正：价格远离基准时缩放动量（上市公司基准 = specPt.feeBase/10000）
            if (static_cast<int>(s[1]) != 0) {
                const int si = static_cast<int>(s[1]);
                const float base = (si > 0 && si < static_cast<int>(st.specPts.size()))
                                       ? st.specPts[si].feeBase / 10000.0f
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
        st.stockHistory[i][st.turnCounter] =
            *reinterpret_cast<int32_t*>(&s[5]); // 位模式存 float
        sum += s[5];
    }
    st.turnCounter = (st.turnCounter + 1) % 144;
    st.marketIndex = static_cast<int32_t>(sum * 10.0);
    RICH4_LOGI("stockTick: market=%d turn=%d (RE 0x4291D6)", st.marketIndex, st.turnCounter);
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
//   sharesLeft -= count，现金扣款；viaBank=true：金额 = 当前价 × count，银行扣款（市场股数未建模）；
//   末尾：持股 += count；均价 = (旧持股×旧均价 + 金额) / 新持股；再调 updateSpecPtControl
//   返回 1 表示经营権（owner）变化
int buyStock(Application& app, int player, int stock, int count, bool viaBank) {
    GameState& st = app.gameState();
    if (player < 0 || player >= 4 || stock < 0 || stock >= 12 || count <= 0) {
        return 0;
    }
    int32_t amount = 0;
    if (viaBank) {
        amount = static_cast<int32_t>(static_cast<double>(count) * st.stocks[stock][2]);
        st.players[player].bank -= amount;
    } else {
        const int si = static_cast<int>(st.stocks[stock][1]);
        if (si > 0 && si < static_cast<int>(st.specPts.size())) {
            const int32_t per = st.specPts[si].feeBase / 10000;
            amount = per * count;
            st.specPts[si].sharesLeft -= count;
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

} // namespace rich4
