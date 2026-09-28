#include "microtest.h"

#include "game/app/trade_market.h"
#include "game/game_state.h"

// [NEW] L0 单测：公佈欄掛單純函數（0x4246C5/0x4247D5/0x428475/0x42483E，Q-W 純函數路線）。
//   type 常量文件內未導出：1=股票 2=地產 3=道具 4=卡片（g_miscTable336 語義）。

MT_TEST(trade_queue_writes_fields) {
    rich4::GameState st;
    st.playerCount = 4;
    rich4::queueTradeOrder(st, 0, 1, 2, 150, 10);
    const rich4::TradeSlot& t = st.tradeSlots[0][0];
    MT_EQ(static_cast<int>(t.type), 1);
    MT_EQ(static_cast<int>(t.objId), 2);
    MT_EQ(t.price, 150);
    MT_EQ(static_cast<int>(t.count), 10);
    MT_EQ(static_cast<int>(t.age), 0);
}

MT_TEST(trade_queue_same_obj_overwrites_price) {
    // 同 (type,objId) 命中同槽改價，不新增（0x4246C5 while 條件）
    rich4::GameState st;
    st.playerCount = 4;
    rich4::queueTradeOrder(st, 0, 3, 5, 900, 0);
    rich4::queueTradeOrder(st, 0, 3, 5, 1200, 0);
    MT_EQ(st.tradeSlots[0][0].price, 1200);
    MT_EQ(static_cast<int>(st.tradeSlots[0][1].type), 0);
}

MT_TEST(trade_queue_board_full_rejects) {
    rich4::GameState st;
    st.playerCount = 4;
    for (int i = 0; i < 8; ++i) {
        rich4::queueTradeOrder(st, 0, 3, static_cast<int>(i + 1), 100, 0);
    }
    MT_EQ(static_cast<int>(st.tradeSlots[0][7].type), 0);  // 第 8 筆被拒（槽 0..6 已滿）
}

MT_TEST(trade_remove_shifts_forward) {
    rich4::GameState st;
    st.playerCount = 4;
    rich4::queueTradeOrder(st, 1, 3, 1, 100, 0);
    rich4::queueTradeOrder(st, 1, 3, 2, 200, 0);
    rich4::removeTradeOrder(st, 1, 0);
    MT_EQ(static_cast<int>(st.tradeSlots[1][0].objId), 2);
    MT_EQ(static_cast<int>(st.tradeSlots[1][1].type), 0);
}

MT_TEST(trade_age_increments_and_skips_dead) {
    rich4::GameState st;
    st.playerCount = 4;
    st.players[0].alive = 1;
    st.players[1].alive = 0;
    rich4::queueTradeOrder(st, 0, 3, 7, 300, 0);
    rich4::queueTradeOrder(st, 1, 3, 7, 300, 0);
    rich4::ageTradeOrders(st);
    MT_EQ(static_cast<int>(st.tradeSlots[0][0].age), 1);
    MT_EQ(static_cast<int>(st.tradeSlots[1][0].age), 0);  // 亡者掛單不計齡
}

MT_TEST(trade_clear_invalid_drops_bare_stock) {
    // 股票掛單：持股歸零 → 撤單（0x42483E「持股>掛單才留」）
    rich4::GameState st;
    st.playerCount = 4;
    st.players[0].alive = 1;
    rich4::queueTradeOrder(st, 0, 1, 2, 150, 10);
    MT_EQ(static_cast<int>(st.tradeSlots[0][0].type), 1);
    rich4::clearInvalidTradeOrders(st);
    MT_EQ(static_cast<int>(st.tradeSlots[0][0].type), 0);
}
