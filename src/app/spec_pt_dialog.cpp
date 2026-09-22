#include "game/app/spec_pt_dialog.h"

#include <cstdio>

#include "game/app/confirm_dialog.h"
#include "game/app/message_dialog.h"
#include "game/app/number_input_dialog.h"
#include "game/app/stock_system.h"
#include "game/application.h"
#include "game/core/log.h"

namespace rich4 {

// [RE 0x41D1A9] specPtBuyStockDialog
// 依据: 0x41D1A9 反编译;
//   ① 守卫：!state37 && alive && specPt+48（sharesLeft）!= 0；
//   ② 每股价格 v3 = feeBase(+36)/10000；可买 v4 = cash/v3，上限 1000，再取 min(sharesLeft)；
//   ③ 人类：sprintf(aS_26 "%s\n\n每股售價%d\n\n是否認購股份？") → askDialog → sub_453544 股数输入；
//      AI：sub_41D839（保留 g_startMoneyVal × dbl_463CD0(0.3) × M 现金，余额全买）；
//   ④ sub_428D2A(玩家, specPt.index, 股数, 0) 购买 → 返回 1（经营権变化）→
//      showMessage("恭喜您成為幫主！"（index==12）/ "恭喜您獲得經營權！"）→ sub_41D433 刷新
void specPtBuyStockDialog(Application& app, int specPtIdx) {
    GameState& st = app.gameState();
    const int p = st.currentPlayer;
    if (p < 0 || p >= 4 || specPtIdx <= 0 || specPtIdx >= static_cast<int>(st.specPts.size())) {
        return;
    }
    SpecPt& sp = st.specPts[specPtIdx];
    if (st.players[p].state37 != 0 || st.players[p].alive == 0 || sp.sharesLeft == 0) {
        return;
    }
    const int per = sp.capital / 10000; // [RE 0x41D1A9] 每股价格 = 股本(+36) / 10000
    if (per <= 0) {
        return;
    }
    int maxBuy = st.players[p].cash / per;
    if (maxBuy > 1000) {
        maxBuy = 1000; // 原版上限 1000（帮助"2000 张"单位不同，以 IDA 为准）
    }
    if (maxBuy > sp.sharesLeft) {
        maxBuy = sp.sharesLeft;
    }
    if (maxBuy <= 0) {
        return;
    }
    const int stock = sp.stockNo; // +25 股票号（word_496984[18*stockNo] = specPt 索引）
    if (stock < 0 || stock >= 12) {
        return;
    }
    int count = 0;
    if (st.players[p].alive == 1) {
        // 人类：确认 → 股数输入
        char name[24] = {};
        int n = 0;
        while (n < 20 && sp.pad4[n]) {
            name[n] = static_cast<char>(sp.pad4[n]);
            ++n;
        }
        name[n] = '\0';
        char text[192];
        std::snprintf(text, sizeof(text), "%s\n\n每股售價%d\n\n是否認購股份？", name, per);
        if (!confirmDialog(app, text)) {
            return;
        }
        const int v = numberInputDialog(app, maxBuy);
        if (v > 0) {
            count = v;
        }
    } else {
        // AI：保留初始资金 30% × M，其余全买（[RE 0x41D839]）
        const double reserve = static_cast<double>(st.startMoneyVal) * 0.3 * st.moneyMul;
        const int32_t available = st.players[p].cash - static_cast<int32_t>(reserve);
        if (available > 0) {
            count = (available <= maxBuy * per) ? (available / per) : maxBuy;
        }
    }
    if (count <= 0) {
        return;
    }
    const int gotControl = buyStock(app, p, stock, count, false);
    if (gotControl == 1) {
        showMessage(app, (sp.costType == 12) ? "恭喜您成為幫主！" : "恭喜您獲得經營權！", 1500);
    }
    RICH4_LOGI("specPt buy: player %d specPt %d stock %d count %d control=%d (RE 0x41D1A9)", p,
               specPtIdx, stock, count, gotControl);
}

} // namespace rich4
